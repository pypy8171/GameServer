// Session::Start() 동시성 회귀 가드.
//
// 배경(레이스): Start() 는 registry_.Add() 로 세션을 '공개'(브로드캐스트 대상화)한 직후
//   ArmHandshakeDeadline()/ReadHeader() 로 timer_·socket_ 를 만졌다. 그런데 Start 는
//   accept 완료 핸들러(server.cpp — 어떤 strand 에도 bind 되지 않은 람다)가 임의 io
//   스레드에서 부른다. 즉 공개 직후, 다른 스레드의 Broadcast→Send→(예산초과/에러 시)
//   Close 가 같은 세션의 timer_.cancel()/socket_.close() 를 돌리는 동안, 최초 스레드가
//   여전히 off-strand 로 timer_.async_wait()/socket_.async_read() 를 걸어 asio 객체의
//   동시 멤버호출(UB)이 될 수 있었다. 수정: arm/read 를 asio::post(strand_,...) 로 넘겨
//   다른 strand 핸들러(Send/DoWrite/Close)와 직렬화한다(session.h 계약과 일치).
//
// 이 테스트는 멀티스레드 io 에서 connect 폭주 + 동시 Broadcast 스톰을 걸어 그 레이스
//   경로를 실제로 태우고, 전 세션이 정상 등록되며 서버가 살아남는지 본다.
//   ※ 데이터레이스 특성상 TSan 없이 '항상 실패하는' 결정론적 red 는 만들 수 없다 —
//     이는 회귀/스트레스 가드다(수정 제거 시 UB 재노출, sanitizer/반복에서 포착).
#include <gtest/gtest.h>

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "core/dispatch/dispatcher.h"
#include "core/net/server.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"

using namespace game::core;
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

// 헤더만 있는(본문 0) 최소 프레임. 클라는 이 broadcast 를 파싱하지 않으므로 id/내용은
//   무의미 — 스톰의 팬아웃 바이트를 최소화하려는 것뿐이다.
std::vector<uint8_t> HeaderOnlyFrame(uint16_t id)
{
  std::vector<uint8_t> f(kHeaderSize);
  EncodeHeader(f.data(), PacketHeader{static_cast<uint16_t>(kHeaderSize), id});
  return f;
}

}  // namespace

// Start 가 세션을 공개하는 순간 다른 스레드가 그 세션에 Broadcast 를 쏟아부어도,
//   asio 객체 동시접근 없이(=크래시/UB 없이) 전원이 정상 등록돼야 한다.
TEST(SessionStart, SurvivesBroadcastRacingConnectStorm)
{
  constexpr uint16_t kPort = 39240;
  constexpr std::size_t kClients = 16;
  asio::io_context server_io;
  SessionRegistry registry;
  Dispatcher dispatcher;  // bare + 정책 전부 비활성 → 타임아웃/rate 로 인한 종료 요인 배제
  Server server(server_io, kPort, dispatcher, registry);
  server.Start();

  // 멀티스레드 run: 레이스가 성립하려면 accept(Start)와 Broadcast(Send)가 서로 다른
  //   스레드에서 겹쳐야 한다(단일 스레드면 직렬화돼 레이스 자체가 발생하지 않는다).
  const unsigned n = std::max(2u, std::thread::hardware_concurrency());
  std::vector<std::thread> io_threads;
  io_threads.reserve(n);
  for (unsigned i = 0; i < n; ++i)
  {
    io_threads.emplace_back([&server_io] { server_io.run(); });
  }

  // Broadcast 스톰: 세션들이 Start 되는 동안 계속 전역 팬아웃 → 공개 직후 창을 때린다.
  std::atomic<bool> storm{true};
  std::thread broadcaster([&] {
    const std::vector<uint8_t> frame = HeaderOnlyFrame(0x1001);
    while (storm.load(std::memory_order_relaxed))
    {
      registry.Broadcast(frame);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // connect 폭주.
  asio::io_context cio;
  std::vector<tcp::socket> clients;
  clients.reserve(kClients);
  for (std::size_t i = 0; i < kClients; ++i)
  {
    clients.push_back(Connect(cio, kPort));
  }

  // 전원이 스톰을 뚫고 등록돼야 한다(레이스로 세션 유실/서버 크래시가 없어야 함).
  EXPECT_TRUE(WaitUntil([&] { return registry.Count() == kClients; }))
      << "Start 레이스로 세션이 유실되거나 서버가 죽으면 안 된다";

  storm.store(false);
  broadcaster.join();
  for (auto& c : clients)
  {
    std::error_code ignore;
    c.close(ignore);
  }
  server_io.stop();
  for (auto& t : io_threads)
  {
    t.join();
  }
}
