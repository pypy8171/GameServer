// game_server — 게임 서버 수직 슬라이스(MG)의 실행 셸.
//   로그인(인게임 핸드셰이크)→인증세션→월드입장→게임액션 1왕복을 최종 목표로 한다.
//   이 스켈레톤(MG-②)은 부팅·설정·로그·우아한 종료만 갖춘 껍데기다.
//   핸들러(로그인/이동)는 게임측 등록기(RegisterGameHandlers)로 MG-⑤+ 에서 꽂는다.
//   — Dispatcher/SessionRegistry/Server 배선은 echo/chat 과 동일한 코어를 재사용한다.
#include <algorithm>
#include <asio.hpp>
#include <chrono>
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
#include "core/net/send_budget.h"  // ClampSendQueueCap
#include "core/net/server.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"  // kMaxPacketSize
#include "game_logic/account/in_memory_account_repository.h"
#include "game_logic/game/game_handlers.h"
#include "game_logic/login/login_handlers.h"
#include "game_logic/world/world.h"

using namespace game::core;

int main(int argc, char** argv)
{
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
  int sendqueue_max_bytes =
      static_cast<int>(kDefaultSendQueueCapBytes);  // send 큐 상한(백프레셔, ADR-E)
  bool cfg_ok = false;
  const ServerConfig cfg = ServerConfig::FromFile("game_server.cfg", &cfg_ok);
  if (cfg_ok)
  {
    port = cfg.GetUInt16("port", port);
    entity_pool_count = cfg.GetInt("entity_pool_count", entity_pool_count);
    guild_pool_count = cfg.GetInt("guild_pool_count", guild_pool_count);
    sendqueue_max_bytes = cfg.GetInt("sendqueue_max_bytes", sendqueue_max_bytes);
  }
  if (argc > 1)
  {
    uint16_t cli = 0;
    if (ServerConfig::ParseUInt16(argv[1], cli))
    {
      port = cli;
    }
    else
    {
      std::cerr << "[game] invalid port arg '" << argv[1] << "', using " << port
                << "\n";
    }
  }
  // send 큐 상한을 유효 바이트값으로 확정한다. 음수/0(설정 실수·미설정)은 기본값으로,
  //   그다음 하한(단일 최대 프레임=kMaxPacketSize)으로 승격한다 — cap 이 한 프레임보다
  //   작으면 정상 최대 패킷조차 백프레셔에 걸려 세션이 끊긴다(ClampSendQueueCap 주석).
  const std::size_t send_cap = ClampSendQueueCap(
      sendqueue_max_bytes > 0 ? static_cast<std::size_t>(sendqueue_max_bytes)
                              : kDefaultSendQueueCapBytes,
      kMaxPacketSize);

  // 세션 보안 정책(S-1 타임아웃 / S-4 rate-limit)과 접속 상한(S-3)을 config 에서 주입.
  //   전부 0 = 비활성(기본). 음수/미설정은 0(비활성)으로 폴백한다.
  SessionPolicy policy;
  policy.handshake_timeout = std::chrono::milliseconds(
      std::max(0, cfg.GetInt("handshake_timeout_ms", 0)));
  policy.idle_timeout =
      std::chrono::milliseconds(std::max(0, cfg.GetInt("idle_timeout_ms", 0)));
  policy.rate_burst = std::max(0, cfg.GetInt("rate_limit_burst", 0));
  policy.rate_per_sec = std::max(0, cfg.GetInt("rate_limit_per_sec", 0));
  const std::size_t max_sessions =
      static_cast<std::size_t>(std::max(0, cfg.GetInt("max_sessions", 0)));
  LOG_INFO(
      "session policy: handshake_timeout_ms={} idle_timeout_ms={} "
      "rate_limit_burst={} rate_limit_per_sec={} max_sessions={}",
      static_cast<long long>(policy.handshake_timeout.count()),
      static_cast<long long>(policy.idle_timeout.count()), policy.rate_burst,
      policy.rate_per_sec, max_sessions);

  const unsigned threads = std::max(1u, std::thread::hardware_concurrency());

  // 비동기 파일 로거 초기화(콘솔 미러링 on). 스레드 시작 전에 1회.
  log::Init("logs/game_server.log", log::Level::Info, /*console=*/true);

  // 해석된 설정값 확인용 로그. cfg_ok=false 면 파일을 못 읽어 전부 기본값이다.
  LOG_INFO(
      "config loaded (cfg_ok={}): port={} entity_pool_count={} "
      "guild_pool_count={} sendqueue_max_bytes={} (effective send_cap={})",
      cfg_ok, port, entity_pool_count, guild_pool_count, sendqueue_max_bytes,
      send_cap);

  // 계정 저장소(인메모리 스텁). dispatcher 보다 먼저 선언해 더 오래 살게 한다
  //   — 로그인 핸들러가 이 repo 를 참조 캡처하기 때문(수명 역전 시 UAF).
  //   ⚠️ 자격증명은 소스/커밋에 절대 두지 않는다: 데모 계정은 로컬 전용(비추적)
  //   game_server.cfg 의 demo_account/demo_password 키에서만 프로비저닝한다.
  //   키가 없으면 계정 0개 → 모든 로그인 거부(핸드셰이크 경로 자체는 살아있음).
  // io_context 를 먼저 선언한다: World 의 strand 와 acceptor 가 이 실행기를 쓴다.
  //   선언 순서 = 파괴 역순 → io 가 가장 늦게 파괴돼 World·Server 의 strand/소켓
  //   실행기 수명을 덮는다.
  asio::io_context io;

  game::logic::InMemoryAccountRepository accounts;
  const std::string demo_account = cfg.GetString("demo_account", "");
  const std::string demo_password = cfg.GetString("demo_password", "");
  if (!demo_account.empty() && !demo_password.empty())
  {
    const auto demo_pid =
        static_cast<game::logic::PlayerId>(cfg.GetInt("demo_player_id", 1));
    accounts.AddAccount(demo_account, demo_password, demo_pid);
    LOG_INFO("[login] 데모 계정 프로비저닝됨(cfg): account={} player_id={}",
             demo_account, demo_pid);  // 비밀번호는 로깅하지 않는다
  }
  else
  {
    LOG_WARN(
        "[login] 프로비저닝된 계정 없음 — 모든 로그인 거부. "
        "game_server.cfg 에 demo_account/demo_password 설정 시 활성화.");
  }

  // 게임 월드(엔티티 소유자). accounts 와 함께 dispatcher/server 보다 먼저 선언해
  //   더 오래 살게 한다 — 아래 콜백들이 world 를 참조 캡처하기 때문(수명 역전 시 UAF).
  game::logic::World world(io);

  SessionRegistry registry;
  Dispatcher dispatcher;
  // 로그인 성공 → 월드 입장 배선(ADR-N/O). 인증 성공 훅에서 world strand 로 진입해
  //   엔티티를 만들고 WorldEnteredNotify(스폰)를 보낸다. player_id 는 로그인이 발급.
  game::logic::RegisterLoginHandlers(
      dispatcher, accounts,
      [&world](const SessionPtr& s, game::logic::PlayerId pid) {
        world.PostEnter(s, pid);
      });  // [ADR-P/J/N]
  // 게임(인게임 액션) 핸들러 배선: 이동(MoveRequest→월드 strand 변이→MoveNotify
  //   전원 브로드캐스트). registry 는 위에서 선언돼 dispatcher 보다 오래 산다.
  game::logic::RegisterGameHandlers(dispatcher, world, registry);
  // 바인드/리슨 실패(포트 점유 등)는 Server 생성자에서 예외로 던진다. worker
  //   try/catch 진입 이전이라 감싸지 않으면 로그 없이 terminate → FATAL 로 원인을
  //   남기고 큐를 flush 한 뒤 정상 종료 코드로 나간다(chat_server 와 동일 규율).
  std::optional<Server> server;
  try
  {
    server.emplace(io, port, dispatcher, registry, send_cap, policy,
                   max_sessions);
    // 세션 종료 시 월드에서 퇴장(엔티티 제거). world strand 로 진입해 처리하며,
    //   세션 객체가 이미 파괴됐을 수 있으므로 SessionId 값만 넘긴다.
    server->set_on_disconnect(
        [&world](const SessionPtr& s) { world.PostLeave(s->id()); });
    server->Start();
  }
  catch (const std::exception& e)
  {
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
    for (;;)
    {
      try
      {
        io.run();
        return;  // 정상 종료(io.stop) — 루프 탈출
      }
      catch (const std::exception& e)
      {
        LOG_ERROR("worker exception: {}", e.what());
      }
      catch (...)
      {
        LOG_ERROR("worker exception: (unknown)");
      }
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(threads > 0 ? threads - 1 : 0);
  for (unsigned i = 1; i < threads; ++i)
  {
    pool.emplace_back(worker);
  }
  worker();
  for (auto& t : pool)
  {
    t.join();
  }

  log::Shutdown();  // 비동기 로그 큐 flush + 백그라운드 스레드 정리
  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
