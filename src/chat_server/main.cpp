// chat_server — M0.5 채팅 릴레이 수직 슬라이스.
//   여러 클라이언트 접속 → 신원(nickname) 등록 → 발화를 전원에게 릴레이.
//   코어(SessionRegistry/Session)의 수명주기·동시성 규율을 처음 굴려보는 하네스.
//   릴레이 로직은 chat_handlers.{h,cpp} 에 있고(테스트와 공유), 여기선 배선만 한다.
#include <algorithm>
#include <asio.hpp>
#include <csignal>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <google/protobuf/stubs/common.h>

#include "chat_server/chat_handlers.h"
#include "core/dispatch/dispatcher.h"
#include "core/log/log.h"
#include "core/net/server.h"
#include "core/net/session_registry.h"

using namespace game::core;

int main(int argc, char** argv) {
#if defined(_WIN32)
  SetConsoleOutputCP(CP_UTF8);  // 콘솔 로그 한글 깨짐 방지 (소스는 /utf-8)
  SetConsoleCP(CP_UTF8);
#endif
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  const uint16_t port =
      (argc > 1) ? static_cast<uint16_t>(std::stoi(argv[1])) : 7777;
  const unsigned threads = std::max(1u, std::thread::hardware_concurrency());

  // 비동기 파일 로거 초기화(콘솔 미러링 on). 스레드 시작 전에 1회.
  log::Init("logs/chat_server.log", log::Level::Info, /*console=*/true);

  SessionRegistry registry;
  Dispatcher dispatcher;
  game::chat::RegisterChatHandlers(dispatcher, registry);

  asio::io_context io;
  // 바인드/리슨 실패(포트 점유 등)는 Server 생성자에서 예외로 던진다. 이는
  //   worker try/catch 진입 이전이라 감싸지 않으면 로그 없이 terminate 한다(리뷰 C).
  //   → FATAL 로 원인을 남기고 큐를 flush 한 뒤 정상 종료 코드로 나간다.
  std::optional<Server> server;
  try {
    server.emplace(io, port, dispatcher, registry);
    server->set_on_disconnect(game::chat::MakeDisconnectHook(registry));
    server->Start();
  } catch (const std::exception& e) {
    LOG_FATAL("server bind/start failed on port {}: {}", port, e.what());
    log::Shutdown();
    google::protobuf::ShutdownProtobufLibrary();
    return 1;
  }

  // ---- graceful shutdown ----
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&io](std::error_code, int) {
    LOG_INFO("shutting down");
    io.stop();
  });

  // io.run() 워커 가드(F3): 핸들러에서 예외가 새면 워커 스레드가 조용히 죽어
  //   CCU 처리 용량이 크래시 없이 줄어든다. 로깅 후 run()에 재진입해 워커를 살린다.
  auto worker = [&io] {
    for (;;) {
      try {
        io.run();
        return;  // 정상 종료(io.stop) — 루프 탈출
      } catch (const std::exception& e) {
        LOG_ERROR("worker exception: {}", e.what());
      } catch (...) {
        LOG_ERROR("worker exception: (unknown)");
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

  log::Shutdown();  // 비동기 로그 큐 flush + 백그라운드 스레드 정리
  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
