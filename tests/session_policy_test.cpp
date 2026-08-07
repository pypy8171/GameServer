// 세션 보안 정책(S-1 타임아웃 / S-3 접속상한 / S-4 rate-limit)의 종단간(loopback)
//   통합 테스트. 순수 유닛(token_bucket_test·accept_policy_test)이 못 건드리는 asio
//   배선 — steady_timer 만료 → Close, 상한 도달 시 새 연결 거부, 플러딩 → 강제 종료 —
//   이 실제 소켓 위에서 도는지 확인한다. 서버 생존(크래시 없음)도 함께 본다.
#include <gtest/gtest.h>

#include <asio.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "core/dispatch/dispatcher.h"
#include "core/net/server.h"
#include "core/net/session.h"  // SessionPolicy, kDefaultSendQueueCapBytes
#include "core/net/session_registry.h"
#include "core/packet/packet.h"
#include "game_logic/account/in_memory_account_repository.h"
#include "game_logic/login/login_handlers.h"
#include "game_logic/world/world.h"
#include "login.pb.h"

using namespace game::core;
using namespace game::proto;  // LoginRequest 등 .proto 메시지
using asio::ip::tcp;

namespace
{

tcp::socket Connect(asio::io_context& io, uint16_t port)
{
  tcp::socket sock(io);
  tcp::resolver resolver(io);
  asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(port)));
  return sock;
}

template <class Pred>
bool WaitUntil(Pred pred,
               std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (pred())
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

// 헤더만 있는(본문 0) 유효 프레임. rate-limit 은 '완성된 패킷 개수'로 판정하므로
//   내용은 무의미 — 토큰을 소비시키는 최소 패킷이다. size=kHeaderSize, id=임의 C->S.
std::vector<uint8_t> HeaderOnlyFrame(uint16_t id)
{
  std::vector<uint8_t> f(kHeaderSize);
  EncodeHeader(f.data(),
               PacketHeader{static_cast<uint16_t>(kHeaderSize), id});
  return f;
}

}  // namespace

// S-1(pre-auth): 접속 후 핸드셰이크 타임아웃 내 미인증이면 강제 종료된다.
//   핸들러 없는 bare 디스패처라 세션은 절대 인증되지 않는다 → 마감이 그대로 만료.
TEST(SessionPolicy, HandshakeTimeoutClosesUnauthenticatedSession)
{
  constexpr uint16_t kPort = 39230;
  asio::io_context server_io;
  SessionRegistry registry;
  Dispatcher dispatcher;
  SessionPolicy policy;
  policy.handshake_timeout = std::chrono::milliseconds(150);
  Server server(server_io, kPort, dispatcher, registry,
                kDefaultSendQueueCapBytes, policy);
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket sock = Connect(cio, kPort);
  ASSERT_TRUE(WaitUntil([&] { return registry.Count() == 1; }));

  // 아무것도 보내지 않는다 → 핸드셰이크 마감 만료 → 세션 강제 종료 → registry 제거.
  EXPECT_TRUE(WaitUntil([&] { return registry.Count() == 0; }))
      << "미인증 세션은 핸드셰이크 타임아웃으로 끊겨야 한다";

  sock.close();
  server_io.stop();
  server_thread.join();
}

// S-1(post-auth): 인증된 세션도 유휴(수신 활동 없음) 지속 시 강제 종료된다(좀비 회수).
//   로그인으로 인증 → 이후 클라가 침묵 → 유휴 마감 만료 → Close.
TEST(SessionPolicy, IdleTimeoutClosesAuthenticatedButQuietSession)
{
  using namespace game::logic;
  constexpr uint16_t kPort = 39231;
  asio::io_context server_io;
  SessionRegistry registry;
  InMemoryAccountRepository accounts;
  accounts.AddAccount("hero", "pw", /*player_id=*/7);
  World world(server_io);
  Dispatcher dispatcher;
  RegisterLoginHandlers(
      dispatcher, accounts,
      [&world](const SessionPtr& s, PlayerId pid) { world.PostEnter(s, pid); });

  SessionPolicy policy;
  policy.idle_timeout = std::chrono::milliseconds(200);  // handshake 는 비활성(0)
  Server server(server_io, kPort, dispatcher, registry,
                kDefaultSendQueueCapBytes, policy);
  server.set_on_disconnect(
      [&world](const SessionPtr& s) { world.PostLeave(s->id()); });
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket sock = Connect(cio, kPort);
  LoginRequest req;
  req.set_account("hero");
  req.set_password("pw");
  asio::write(sock, asio::buffer(MakePacket(PacketId::LoginRequest, req)));

  // 인증 성공 → 세션 등록 유지. 이후 클라는 아무 패킷도 보내지 않는다(유휴).
  ASSERT_TRUE(WaitUntil([&] { return registry.Count() == 1; }));
  // 유휴 마감(200ms) 만료 → 강제 종료. (서버 송신은 유휴 리셋 대상이 아니다.)
  EXPECT_TRUE(WaitUntil([&] { return registry.Count() == 0; }))
      << "인증됐어도 유휴가 지속되면 끊겨야 한다";

  sock.close();
  server_io.stop();
  server_thread.join();
}

// S-3: 동접 상한에 도달하면 새 연결은 서버가 조용히 닫는다(등록되지 않음). 상한
//   이하 연결은 정상 유지되고, 서버는 계속 accept 를 돈다(크래시 없음).
TEST(SessionPolicy, ConnectionCapRejectsBeyondLimit)
{
  constexpr uint16_t kPort = 39232;
  constexpr std::size_t kCap = 2;
  asio::io_context server_io;
  SessionRegistry registry;
  Dispatcher dispatcher;
  Server server(server_io, kPort, dispatcher, registry,
                kDefaultSendQueueCapBytes, /*policy=*/{}, kCap);
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket s1 = Connect(cio, kPort);
  tcp::socket s2 = Connect(cio, kPort);
  ASSERT_TRUE(WaitUntil([&] { return registry.Count() == kCap; }));

  // 상한 초과 연결: TCP 는 붙지만 서버가 즉시 닫는다 → 읽기에서 eof/에러.
  tcp::socket s3 = Connect(cio, kPort);
  std::error_code ec;
  std::vector<uint8_t> buf(1);
  asio::read(s3, asio::buffer(buf), ec);  // 서버가 닫아 즉시 반환(에러)
  EXPECT_TRUE(ec) << "상한 초과 연결은 서버가 닫아야 한다";
  EXPECT_EQ(registry.Count(), kCap) << "상한을 넘겨 등록되면 안 된다";

  s1.close();
  s2.close();
  s3.close();
  server_io.stop();
  server_thread.join();
}

// S-4: 버스트를 넘겨 패킷을 쏟아내는 세션은 강제 종료된다(플러딩 방어). 리필 0 이라
//   burst 개까지만 통과하고 그다음 패킷에서 rate-limit 발동 → Close → registry 제거.
TEST(SessionPolicy, RateLimitForceClosesFloodingSession)
{
  constexpr uint16_t kPort = 39233;
  asio::io_context server_io;
  SessionRegistry registry;
  Dispatcher dispatcher;  // bare: 패킷은 게이트에서 버려지나 rate 판정은 그 전에 일어난다
  SessionPolicy policy;
  policy.rate_burst = 3;       // 버스트 3개
  policy.rate_per_sec = 0.0;   // 리필 없음 → 4번째부터 초과
  Server server(server_io, kPort, dispatcher, registry,
                kDefaultSendQueueCapBytes, policy);
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket sock = Connect(cio, kPort);
  ASSERT_TRUE(WaitUntil([&] { return registry.Count() == 1; }));

  // 버스트(3)를 넘겨 10개를 몰아 보낸다 → 4번째에서 rate-limit → 강제 종료.
  const std::vector<uint8_t> frame = HeaderOnlyFrame(0x1001);
  std::error_code wec;
  for (int i = 0; i < 10 && !wec; ++i)
  {
    asio::write(sock, asio::buffer(frame), wec);
  }
  EXPECT_TRUE(WaitUntil([&] { return registry.Count() == 0; }))
      << "버스트 초과 플러딩 세션은 강제 종료돼야 한다";

  sock.close();
  server_io.stop();
  server_thread.join();
}

// ADR-V(무응답 종료): 인증 후 heartbeat ping 에 계속 무응답(any-recv 없음)이면
//   마지막 활동 이후 timeout 초과로 강제 종료된다(half-open/좀비 능동 탐지).
//   클라는 로그인만 하고 이후 완전히 침묵한다 → 서버 ping 은 가지만 pong 이 없다.
TEST(SessionPolicy, HeartbeatForceClosesUnresponsiveSession)
{
  using namespace game::logic;
  constexpr uint16_t kPort = 39234;
  asio::io_context server_io;
  SessionRegistry registry;
  InMemoryAccountRepository accounts;
  accounts.AddAccount("hero", "pw", /*player_id=*/7);
  World world(server_io);
  Dispatcher dispatcher;
  RegisterLoginHandlers(
      dispatcher, accounts,
      [&world](const SessionPtr& s, PlayerId pid) { world.PostEnter(s, pid); });

  SessionPolicy policy;  // idle 는 비활성(0) — heartbeat 경로만 격리해 검증
  policy.heartbeat_interval = std::chrono::milliseconds(80);  // ping 주기
  policy.heartbeat_timeout = std::chrono::milliseconds(120);  // 무응답 사망 임계
  Server server(server_io, kPort, dispatcher, registry,
                kDefaultSendQueueCapBytes, policy);
  server.set_on_disconnect(
      [&world](const SessionPtr& s) { world.PostLeave(s->id()); });
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket sock = Connect(cio, kPort);
  LoginRequest req;
  req.set_account("hero");
  req.set_password("pw");
  asio::write(sock, asio::buffer(MakePacket(PacketId::LoginRequest, req)));
  ASSERT_TRUE(WaitUntil([&] { return registry.Count() == 1; }));

  // 로그인 후 완전 침묵 → 서버 ping 은 오지만 pong(any-recv) 이 없어 timeout 초과 → Close.
  EXPECT_TRUE(WaitUntil([&] { return registry.Count() == 0; }))
      << "heartbeat 무응답 세션은 강제 종료돼야 한다";

  sock.close();
  server_io.stop();
  server_thread.join();
}

// ADR-V(생존): 인증 후 timeout 보다 짧은 간격으로 pong(any-recv)을 계속 보내면
//   데드라인이 매번 리셋돼 timeout 의 여러 배 시간을 넘겨도 세션이 유지된다
//   (heartbeat 가 정상 연결을 죽이지 않음). 이후 침묵하면 같은 배선으로 종료 수렴.
TEST(SessionPolicy, HeartbeatKeepsResponsiveSessionAlive)
{
  using namespace game::logic;
  constexpr uint16_t kPort = 39235;
  asio::io_context server_io;
  SessionRegistry registry;
  InMemoryAccountRepository accounts;
  accounts.AddAccount("hero", "pw", /*player_id=*/7);
  World world(server_io);
  Dispatcher dispatcher;
  RegisterLoginHandlers(
      dispatcher, accounts,
      [&world](const SessionPtr& s, PlayerId pid) { world.PostEnter(s, pid); });

  SessionPolicy policy;
  policy.heartbeat_interval = std::chrono::milliseconds(80);
  policy.heartbeat_timeout = std::chrono::milliseconds(120);
  Server server(server_io, kPort, dispatcher, registry,
                kDefaultSendQueueCapBytes, policy);
  server.set_on_disconnect(
      [&world](const SessionPtr& s) { world.PostLeave(s->id()); });
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  asio::io_context cio;
  tcp::socket sock = Connect(cio, kPort);
  LoginRequest req;
  req.set_account("hero");
  req.set_password("pw");
  asio::write(sock, asio::buffer(MakePacket(PacketId::LoginRequest, req)));
  ASSERT_TRUE(WaitUntil([&] { return registry.Count() == 1; }));

  // timeout(120ms)보다 짧은 40ms 간격으로 pong 을 계속 보낸다 → any-recv 로 리셋.
  //   timeout 의 여러 배(~500ms) 동안 세션이 유지되는지 확인한다.
  const std::vector<uint8_t> pong = MakeControlPacket(PacketId::HeartbeatPong);
  const auto until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  std::error_code wec;
  while (std::chrono::steady_clock::now() < until && !wec)
  {
    asio::write(sock, asio::buffer(pong), wec);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  EXPECT_FALSE(wec) << "pong 을 보내는 동안 세션이 끊기면 안 된다";
  EXPECT_EQ(registry.Count(), 1u) << "응답하는 세션은 유지돼야 한다";

  // 이제 침묵 → timeout 초과 → 강제 종료로 수렴(동일 배선의 종료 경로 확인).
  EXPECT_TRUE(WaitUntil([&] { return registry.Count() == 0; }))
      << "응답이 끊기면 heartbeat timeout 으로 종료돼야 한다";

  sock.close();
  server_io.stop();
  server_thread.join();
}
