#include <algorithm>
#include <asio.hpp>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "core/config/server_config.h"
#include "core/dispatch/dispatcher.h"
#include "core/net/server.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"
#include "packet.pb.h"

using namespace game::core;
using namespace game::proto;

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  // 포트 우선순위: CLI 인자 > 설정파일(echo_server.cfg) > 기본값 7777.
  uint16_t port = 7777;
  bool cfg_ok = false;
  const ServerConfig cfg = ServerConfig::FromFile("echo_server.cfg", &cfg_ok);
  if (cfg_ok) port = cfg.GetUInt16("port", port);
  if (argc > 1) {
    uint16_t cli = 0;
    if (ServerConfig::ParseUInt16(argv[1], cli)) {
      port = cli;
    } else {
      std::cerr << "[server] invalid port arg '" << argv[1] << "', using "
                << port << "\n";
    }
  }
  const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
  // ---- 핸들러 등록 (Echo) ----

  Dispatcher dispatcher;
  dispatcher.Register(
      PacketId::EchoRequest,
      [](const SessionPtr& session, const uint8_t* body, uint16_t size) {
        EchoRequest req;
        if (!req.ParseFromArray(body, size)) {
          std::cerr << "[echo] parse failed\n";
          return;
        }
        EchoResponse res;
        res.set_message(req.message());
        session->Send(MakePacket(PacketId::EchoResponse, res));
      });
  // echo 는 인증 개념이 없는 무상태 데모 → 모든 트래픽이 정당한 preauth 다.
  // 미인증 게이트[ADR-J]에 명시적으로 통과시키지 않으면 EchoRequest 가 전부 drop 된다.
  dispatcher.AllowUnauthenticated(PacketId::EchoRequest);

  // ---- 서버 기동 ----
  asio::io_context io;
  SessionRegistry registry;
  Server server(io, port, dispatcher, registry);
  server.Start();

  // ---- graceful shutdown ----
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&io](std::error_code, int) {
    std::cout << "\n[server] shutting down\n";
    io.stop();
  });

  // ---- io_context 를 hardware_concurrency 만큼 스레드로 run ----
  // io.run() 워커 가드(N-1): 핸들러에서 예외가 새면 워커 스레드가 조용히 죽어
  //   (메인 스레드면 std::terminate) CCU 처리 용량이 무통보로 줄거나 서버가 멈춘다.
  //   로깅 후 run()에 재진입해 워커를 살린다. (chat/game 서버와 동일 관용구 —
  //   echo 는 async 로거를 띄우지 않는 최소 데모라 진단은 이 파일 규율대로 std::cerr.)
  auto worker = [&io] {
    for (;;) {
      try {
        io.run();
        return;  // 정상 종료(io.stop) — 루프 탈출
      } catch (const std::exception& e) {
        std::cerr << "[server] worker exception: " << e.what() << "\n";
      } catch (...) {
        std::cerr << "[server] worker exception: (unknown)\n";
      }
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(threads > 0 ? threads - 1 : 0);
  for (unsigned i = 1; i < threads; ++i) {
    pool.emplace_back(worker);
  }
  worker();
  for (auto& t : pool) {
    t.join();
  }

  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
