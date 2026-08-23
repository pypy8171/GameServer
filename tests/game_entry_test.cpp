// 게임 입장 종단간(loopback) 통합 테스트.
//   실제 Server + TCP 클라 소켓으로 로그인 → 월드 입장 → 스폰 스냅샷 수신을 검증한다.
//   유닛 테스트가 건드리지 않는 비동기 경로(로그인 성공 훅 → World strand hop →
//   WorldEnteredNotify 실제 송신)가 소켓 위에서 도는지 확인한다. [MG-⑥]
#include <gtest/gtest.h>

#include <asio.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/dispatch/dispatcher.h"
#include "core/net/server.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"
#include "game.pb.h"
#include "game_logic/account/in_memory_account_repository.h"
#include "game_logic/game/game_handlers.h"
#include "game_logic/login/login_handlers.h"
#include "game_logic/world/world.h"
#include "login.pb.h"

using namespace game::core;
using namespace game::logic;
using namespace game::proto;
using asio::ip::tcp;

namespace
{

constexpr uint16_t TestPort = 39218;
constexpr uint16_t MovePort = 39219;   // 이동 왕복 테스트 전용(포트 재사용 회피)
constexpr uint16_t MultiPort = 39220;  // 멀티클라 브로드캐스트 테스트 전용

struct Frame
{
  uint16_t id;
  std::vector<uint8_t> body;
};

Frame ReadFrame(tcp::socket& s)
{
  std::vector<uint8_t> hdr(kHeaderSize);
  asio::read(s, asio::buffer(hdr.data(), kHeaderSize));
  PacketHeader h;
  std::memcpy(&h, hdr.data(), kHeaderSize);
  std::vector<uint8_t> body(h.size - kHeaderSize);
  if (!body.empty())
  {
    asio::read(s, asio::buffer(body.data(), body.size()));
  }
  return Frame{h.id, std::move(body)};
}

tcp::socket Connect(asio::io_context& io, uint16_t port = TestPort)
{
  tcp::socket sock(io);
  tcp::resolver resolver(io);
  asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(port)));
  return sock;
}

}  // namespace

// 유효 로그인 → LoginResponse(ok, player_id) 직후 WorldEnteredNotify(스폰)를 받는다.
//   와이어 순서: 핸들러가 LoginResponse 를 먼저 큐잉하고, 성공 훅이 World strand 로
//   진입해 WorldEnteredNotify 를 나중에 큐잉하므로 세션 send 큐에서 이 순서가 보장된다.
TEST(GameEntry, LoginEntersWorldAndReceivesSpawn)
{
  asio::io_context server_io;
  SessionRegistry registry;
  InMemoryAccountRepository accounts;
  accounts.AddAccount("hero", "pw", /*player_id=*/7);

  World world(server_io);
  Dispatcher dispatcher;
  RegisterLoginHandlers(dispatcher, accounts,
                        [&world](const SessionPtr& s, PlayerId pid) {
                          world.PostEnter(s, pid);
                        });
  Server server(server_io, TestPort, dispatcher, registry);
  server.set_on_disconnect(
      [&world](const SessionPtr& s) { world.PostLeave(s->id()); });
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket sock = Connect(cio);

  LoginRequest req;
  req.set_account("hero");
  req.set_password("pw");
  asio::write(sock, asio::buffer(MakePacket(PacketId::LoginRequest, req)));

  // 1) LoginResponse(ok=true, player_id=7)
  Frame f1 = ReadFrame(sock);
  EXPECT_EQ(f1.id, static_cast<uint16_t>(PacketId::LoginResponse));
  {
    LoginResponse resp;
    ASSERT_TRUE(resp.ParseFromArray(f1.body.data(),
                                    static_cast<int>(f1.body.size())));
    EXPECT_TRUE(resp.ok());
    EXPECT_EQ(resp.player_id(), 7u);
  }

  // 2) WorldEnteredNotify(player_id=7, 원점 스폰)
  Frame f2 = ReadFrame(sock);
  EXPECT_EQ(f2.id, static_cast<uint16_t>(PacketId::WorldEnteredNotify));
  {
    WorldEnteredNotify wen;
    ASSERT_TRUE(wen.ParseFromArray(f2.body.data(),
                                   static_cast<int>(f2.body.size())));
    EXPECT_EQ(wen.player_id(), 7u);
    EXPECT_FLOAT_EQ(wen.x(), 0.0f);
    EXPECT_FLOAT_EQ(wen.y(), 0.0f);
  }

  sock.close();
  server_io.stop();
  server_thread.join();
}

// 멀티클라이언트: 한 클라의 MoveRequest 로 생성된 MoveNotify 를 '다른' 클라도 받는다.
//   단일 클라 테스트는 본인 되받기만 본다 — 여기서는 코어 Room 멤버 Group 팬아웃이
//   입장한 다른 세션의 실제 소켓으로 나가는 크로스세션 브로드캐스트를 검증한다. [U-7]
//   (ADR-X 결정 B: 팬아웃이 registry 전역이 아니라 World 가 포함한 Room 을 통과.)
TEST(GameEntry, MoveNotifyBroadcastsToOtherClients)
{
  asio::io_context server_io;
  SessionRegistry registry;
  InMemoryAccountRepository accounts;
  accounts.AddAccount("hero", "pw", /*player_id=*/7);
  accounts.AddAccount("mate", "pw", /*player_id=*/8);

  World world(server_io);
  Dispatcher dispatcher;
  RegisterLoginHandlers(dispatcher, accounts,
                        [&world](const SessionPtr& s, PlayerId pid) {
                          world.PostEnter(s, pid);
                        });
  RegisterGameHandlers(dispatcher, world);
  Server server(server_io, MultiPort, dispatcher, registry);
  server.set_on_disconnect(
      [&world](const SessionPtr& s) { world.PostLeave(s->id()); });
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket c1 = Connect(cio, MultiPort);
  tcp::socket c2 = Connect(cio, MultiPort);

  // 두 클라 모두 로그인 → 입장 응답(LoginResponse + WorldEnteredNotify)을 소진.
  //   c2 가 WorldEnteredNotify 를 받은 시점 = registry 등록 완료 → 이후 팬아웃 대상.
  auto login = [](tcp::socket& s, const std::string& account) {
    LoginRequest req;
    req.set_account(account);
    req.set_password("pw");
    asio::write(s, asio::buffer(MakePacket(PacketId::LoginRequest, req)));
    (void)ReadFrame(s);  // LoginResponse
    (void)ReadFrame(s);  // WorldEnteredNotify(원점 스폰)
  };
  login(c1, "hero");
  login(c2, "mate");

  // c1(hero, player 7)이 이동 → MoveNotify 전원 팬아웃. '다른' 클라 c2 가 받아야 한다.
  MoveRequest mv;
  mv.set_x(3.0f);
  mv.set_y(-4.0f);
  asio::write(c1, asio::buffer(MakePacket(PacketId::MoveRequest, mv)));

  Frame fm = ReadFrame(c2);  // 크로스세션 수신
  EXPECT_EQ(fm.id, static_cast<uint16_t>(PacketId::MoveNotify));
  {
    MoveNotify mn;
    ASSERT_TRUE(
        mn.ParseFromArray(fm.body.data(), static_cast<int>(fm.body.size())));
    EXPECT_EQ(mn.player_id(), 7u);  // 이동 주체는 c1(hero) — 서버가 신원에서 채움
    EXPECT_FLOAT_EQ(mn.x(), 3.0f);
    EXPECT_FLOAT_EQ(mn.y(), -4.0f);
  }

  c1.close();
  c2.close();
  server_io.stop();
  server_thread.join();
}

// 입장한 인증세션의 MoveRequest → World strand 위치 변이 → MoveNotify 를 방 멤버(본인
//   포함) 팬아웃으로 되받는다. 유닛이 못 건드리는 비동기 경로(디스패치 → World strand
//   hop → 코어 Room 멤버 Group 팬아웃)가 소켓 위에서 도는지 검증한다. [MG-⑦]
TEST(GameEntry, MoveRequestBroadcastsMoveNotify)
{
  asio::io_context server_io;
  SessionRegistry registry;
  InMemoryAccountRepository accounts;
  accounts.AddAccount("hero", "pw", /*player_id=*/7);

  World world(server_io);
  Dispatcher dispatcher;
  RegisterLoginHandlers(dispatcher, accounts,
                        [&world](const SessionPtr& s, PlayerId pid) {
                          world.PostEnter(s, pid);
                        });
  RegisterGameHandlers(dispatcher, world);
  Server server(server_io, MovePort, dispatcher, registry);
  server.set_on_disconnect(
      [&world](const SessionPtr& s) { world.PostLeave(s->id()); });
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket sock = Connect(cio, MovePort);

  LoginRequest req;
  req.set_account("hero");
  req.set_password("pw");
  asio::write(sock, asio::buffer(MakePacket(PacketId::LoginRequest, req)));

  // 로그인/입장 응답 2개(LoginResponse, WorldEnteredNotify)를 소진한 뒤 이동한다.
  (void)ReadFrame(sock);  // LoginResponse
  (void)ReadFrame(sock);  // WorldEnteredNotify(원점 스폰)

  MoveRequest mv;
  mv.set_x(3.0f);
  mv.set_y(-4.0f);
  asio::write(sock, asio::buffer(MakePacket(PacketId::MoveRequest, mv)));

  // MoveNotify(player_id=7, 이동 좌표). player_id 는 서버가 세션 신원에서 채운 값 —
  //   MoveRequest 에는 sender 가 없다(스푸핑 차단). 본인도 브로드캐스트를 되받는다.
  Frame fm = ReadFrame(sock);
  EXPECT_EQ(fm.id, static_cast<uint16_t>(PacketId::MoveNotify));
  {
    MoveNotify mn;
    ASSERT_TRUE(
        mn.ParseFromArray(fm.body.data(), static_cast<int>(fm.body.size())));
    EXPECT_EQ(mn.player_id(), 7u);
    EXPECT_FLOAT_EQ(mn.x(), 3.0f);
    EXPECT_FLOAT_EQ(mn.y(), -4.0f);
  }

  sock.close();
  server_io.stop();
  server_thread.join();
}
