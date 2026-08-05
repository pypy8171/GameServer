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
#include "game_logic/login/login_handlers.h"
#include "game_logic/world/world.h"
#include "login.pb.h"

using namespace game::core;
using namespace game::logic;
using namespace game::proto;
using asio::ip::tcp;

namespace {

constexpr uint16_t TestPort = 39218;

struct Frame {
  uint16_t id;
  std::vector<uint8_t> body;
};

Frame ReadFrame(tcp::socket& s) {
  std::vector<uint8_t> hdr(kHeaderSize);
  asio::read(s, asio::buffer(hdr.data(), kHeaderSize));
  PacketHeader h;
  std::memcpy(&h, hdr.data(), kHeaderSize);
  std::vector<uint8_t> body(h.size - kHeaderSize);
  if (!body.empty()) {
    asio::read(s, asio::buffer(body.data(), body.size()));
  }
  return Frame{h.id, std::move(body)};
}

tcp::socket Connect(asio::io_context& io) {
  tcp::socket sock(io);
  tcp::resolver resolver(io);
  asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(TestPort)));
  return sock;
}

}  // namespace

// 유효 로그인 → LoginResponse(ok, player_id) 직후 WorldEnteredNotify(스폰)를 받는다.
//   와이어 순서: 핸들러가 LoginResponse 를 먼저 큐잉하고, 성공 훅이 World strand 로
//   진입해 WorldEnteredNotify 를 나중에 큐잉하므로 세션 send 큐에서 이 순서가 보장된다.
TEST(GameEntry, LoginEntersWorldAndReceivesSpawn) {
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
