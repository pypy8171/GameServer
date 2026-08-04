// game_server — 게임 서버 수직 슬라이스(MG)의 실행 셸.
//   로그인(인게임 핸드셰이크)→인증세션→월드입장→게임액션 1왕복을 최종 목표로 한다.
//   이 스켈레톤(MG-②)은 부팅·설정·로그·우아한 종료만 갖춘 껍데기다.
//   핸들러(로그인/이동)는 게임측 등록기(RegisterGameHandlers)로 MG-⑤+ 에서 꽂는다.
//   — Dispatcher/SessionRegistry/Server 배선은 echo/chat 과 동일한 코어를 재사용한다.
#include <algorithm>
#include <asio.hpp>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <google/protobuf/stubs/common.h>

#include "core/config/server_config.h"
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

  // 설정값 해석. port 는 CLI 인자 > 설정파일 > 기본값, 풀 용량은 설정파일 > 기본값.
  //   키가 있으면 그 값, 없거나(파일 부재 포함) 잘못된 값이면 기본값으로 폴백한다.
  //   entity/guild 풀은 MG-⑤+ 서브시스템에서 이 용량을 소비할 예정(현재는 로그로 확인).
  uint16_t port = 7777;              // 리슨 포트 기본값
  int entity_pool_count = 10000;     // 엔티티 풀 기본 용량
  int guild_pool_count = 1000;       // 길드 풀 기본 용량
  bool cfg_ok = false;
  const ServerConfig cfg = ServerConfig::FromFile("game_server.cfg", &cfg_ok);
  if (cfg_ok) {
    port = cfg.GetUInt16("port", port);
    entity_pool_count = cfg.GetInt("entity_pool_count", entity_pool_count);
    guild_pool_count = cfg.GetInt("guild_pool_count", guild_pool_count);
  }
  if (argc > 1) {
    uint16_t cli = 0;
    if (ServerConfig::ParseUInt16(argv[1], cli)) {
      port = cli;
    } else {
      std::cerr << "[game] invalid port arg '" << argv[1] << "', using " << port
                << "\n";
    }
  }
  const unsigned threads = std::max(1u, std::thread::hardware_concurrency());

  // 비동기 파일 로거 초기화(콘솔 미러링 on). 스레드 시작 전에 1회.
  log::Init("logs/game_server.log", log::Level::Info, /*console=*/true);

  // 해석된 설정값 확인용 로그. cfg_ok=false 면 파일을 못 읽어 전부 기본값이다.
  LOG_INFO(
      "config loaded (cfg_ok={}): port={} entity_pool_count={} "
      "guild_pool_count={}",
      cfg_ok, port, entity_pool_count, guild_pool_count);

  SessionRegistry registry;
  Dispatcher dispatcher;
  // TODO(MG-⑤+): RegisterLoginHandlers/RegisterGameHandlers(dispatcher, ...) 로
  //   로그인·이동 핸들러를 꽂는다. 지금은 핸들러가 없어 모든 수신 패킷을 drop 한다.

  asio::io_context io;
  // 바인드/리슨 실패(포트 점유 등)는 Server 생성자에서 예외로 던진다. worker
  //   try/catch 진입 이전이라 감싸지 않으면 로그 없이 terminate → FATAL 로 원인을
  //   남기고 큐를 flush 한 뒤 정상 종료 코드로 나간다(chat_server 와 동일 규율).
  std::optional<Server> server;
  try {
    server.emplace(io, port, dispatcher, registry);
    server->Start();
  } catch (const std::exception& e) {
    LOG_FATAL("game_server bind/start failed on port {}: {}", port, e.what());
    log::Shutdown();
    google::protobuf::ShutdownProtobufLibrary();
    return 1;
  }
  LOG_INFO("game_server listening on port {}", port);

  // ---- graceful shutdown ----
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&io](std::error_code, int) {
    LOG_INFO("game_server shutting down");
    io.stop();
  });

  // io.run() 워커 가드: 핸들러에서 예외가 새면 워커 스레드가 조용히 죽어 처리
  //   용량이 크래시 없이 줄어든다. 로깅 후 run()에 재진입해 워커를 살린다.
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
