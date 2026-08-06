// send 큐 백프레셔(ADR-E) 종단간(loopback) 통합 테스트.
//   순수 SendBudget 유닛(send_budget_test.cpp)이 못 건드리는 비동기 경로를 검증한다:
//   느린(안 읽는) 수신자 → 서버 send_queue_ 무한 성장 → 바이트 상한 초과 → 그 세션만
//   강제 종료(H5 킬 경로) → registry 에서 제거. 그리고 서버는 계속 살아 새 연결을 받는다.
//   이 경로가 in-flight async_write 와의 UAF 없이 도는지도 함께 확인한다(크래시=실패).
#include <gtest/gtest.h>

#include <asio.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "core/dispatch/dispatcher.h"
#include "core/net/server.h"
#include "core/net/session_registry.h"

using namespace game::core;
using asio::ip::tcp;

namespace
{

constexpr uint16_t BackpressurePort = 39240;  // 39218-39233(game_entry·policy) 대역 밖 — 포트 충돌 회피

tcp::socket Connect(asio::io_context& io, uint16_t port)
{
  tcp::socket sock(io);
  tcp::resolver resolver(io);
  asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(port)));
  return sock;
}

// pred 가 참이 될 때까지(또는 타임아웃) 폴링한다. 비동기 상태(registry.Count())가
//   서버 io 스레드에서 바뀌므로 값 폴링으로 관측한다. 무한 hang 방지 타임아웃 포함.
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

}  // namespace

// 안 읽는 수신자에게 상한(8KB)을 훌쩍 넘는 양을 팬아웃하면, 그 세션만 강제 종료돼
//   registry 에서 빠지고, 서버는 살아남아 새 연결을 계속 받는다.
TEST(SessionBackpressure, SlowReceiverIsForceClosedAndServerSurvives)
{
  asio::io_context server_io;
  SessionRegistry registry;
  Dispatcher dispatcher;  // 핸들러 불필요 — 팬아웃은 registry.Broadcast 로 직접 구동
  const std::size_t kTinyCap = 8 * 1024;  // 작은 send 큐 상한으로 빠르게 초과 유도
  Server server(server_io, BackpressurePort, dispatcher, registry, kTinyCap);
  server.Start();
  std::thread server_thread([&server_io] { server_io.run(); });

  // 1) 느린 수신자: 접속만 하고 절대 읽지 않는다(Start 에서 registry 등록됨).
  asio::io_context cio;
  tcp::socket slow = Connect(cio, BackpressurePort);
  ASSERT_TRUE(WaitUntil([&] { return registry.Count() == 1; }));

  // 2) 이 세션으로 대량 팬아웃. 프레임 내용은 무의미(수신자는 파싱하지 않는다) —
  //    순수 send 경로에 바이트를 흘린다. 프레임(4KB)은 cap(8KB)보다 작으므로 개별
  //    프레임은 통과하고 '여러 개가 쌓여' 상한을 넘긴다(oversize 단일이 아니라 누적을
  //    검증). 총량(≈3.2MB) ≫ OS 송수신 버퍼 + cap 이라 userspace 큐가 반드시 넘친다.
  const std::vector<uint8_t> frame(4000, 0xAB);
  for (int i = 0; i < 800; ++i)
  {
    registry.Broadcast(frame);  // copy-per-recipient → slow 의 send_queue_ 로 축적
  }

  // 3) 백프레셔가 느린 세션만 강제 종료 → registry 에서 제거된다.
  EXPECT_TRUE(WaitUntil([&] { return registry.Count() == 0; }))
      << "느린 수신자는 상한 초과로 강제 종료됐어야 한다";

  // 4) 서버 생존: 종료 후에도 새 연결을 받아 등록한다(io 살아있음·크래시 없음).
  tcp::socket fresh = Connect(cio, BackpressurePort);
  EXPECT_TRUE(WaitUntil([&] { return registry.Count() == 1; }))
      << "서버는 강제 종료 후에도 새 연결을 계속 받아야 한다";

  slow.close();
  fresh.close();
  server_io.stop();
  server_thread.join();
}
