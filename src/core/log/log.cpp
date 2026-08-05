#include "core/log/log.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace game::core::log
{

namespace
{

spdlog::level::level_enum ToSpd(Level l)
{
  switch (l)
  {
    case Level::Debug: return spdlog::level::debug;
    case Level::Info:  return spdlog::level::info;
    case Level::Warn:  return spdlog::level::warn;
    case Level::Error: return spdlog::level::err;
    case Level::Fatal: return spdlog::level::critical;
  }
  return spdlog::level::info;
}

constexpr std::size_t kQueueSlots = 8192;       // 비동기 큐 깊이
constexpr std::size_t kMaxFileBytes = 5 * 1024 * 1024;  // 5MB 단위 로테이션
constexpr std::size_t kMaxFiles = 3;            // 최대 보관 파일 수
// [%s:%# %!] = 호출 지점 소스파일:줄번호 함수명 (SPDLOG_* 매크로가
//   __FILE__/__LINE__/함수명을 source_loc 로 캡처 → "어떤 cpp의 몇 번째 줄,
//   어느 함수가 남겼는가"를 로그만으로 추적).
constexpr const char* kPattern =
    "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [tid=%t] [%s:%# %!] %v";

// Init/Shutdown 1회 짝 보장(리뷰 E). 중복 Init 은 thread_pool 을 교체해
//   기존 async_logger 의 weak_ptr 를 깨뜨릴 수 있어 무시한다. Shutdown 이 다시 내린다.
std::atomic<bool> g_inited{false};

// FATAL 전용 '동기' 로거. 일반 로거(async)와 같은 sink 를 공유하되, 별도의
//   *동기* 로거라 flush 가 호출 스레드에서 즉시 일어난다(큐 경유 X).
//   → LOG_FATAL 은 이 로거로 흘려 프로세스가 직후 죽어도 디스크에 남는다(리뷰 B 해소).
//   sink 는 _mt(뮤텍스 내장)라 async 백그라운드 스레드와 동시 사용해도 안전.
//   원자 교체: Init/Shutdown 이 세팅/해제, LOG_FATAL 이 읽기 → 레이스 없이 스냅샷.
std::shared_ptr<spdlog::logger> g_fatal;
std::mutex g_fatal_mtx;

}  // namespace

namespace detail
{

std::shared_ptr<spdlog::logger> FatalLogger()
{
  std::lock_guard<std::mutex> lk(g_fatal_mtx);
  return g_fatal;  // shared_ptr 복사로 수명 고정 → 호출 중 Shutdown 이 내려도 안전
}

}  // namespace detail

void Init(const std::string& file_path, Level min_level, bool console)
{
  if (g_inited.exchange(true))
  {
    LOG_WARN("log::Init called more than once — ignoring");
    return;
  }
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path p(file_path);
  if (p.has_parent_path())
  {
    fs::create_directories(p.parent_path(), ec);  // 실패해도 sink 생성에서 재판정
  }

  // 백그라운드 스레드 1개 = MPSC(N개 io 스레드 producer → 1 소비자). CLAUDE.md 계약.
  spdlog::init_thread_pool(kQueueSlots, 1);

  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      file_path, kMaxFileBytes, kMaxFiles));
  if (console)
  {
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  }

  // overflow=block: 큐가 가득 차면 producer 가 잠깐 대기(로그 유실 없음).
  //   "먼저 옳게" — 유실 대신 대기. 저지연 우선으로 overrun_oldest 전환은 /perf-debate 안건.
  auto logger = std::make_shared<spdlog::async_logger>(
      "server", sinks.begin(), sinks.end(), spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);

  logger->set_level(ToSpd(min_level));
  logger->set_pattern(kPattern);
  // WARN/ERROR 는 flush 를 큐에 post(백그라운드가 처리) — async 라 '즉시'가 아니다.
  //   정상 종료 경로는 Shutdown 이 큐를 drain 한다. FATAL 만은 프로세스가 직후
  //   죽을 수 있어 아래 동기 로거로 별도 처리한다(리뷰 B 해소).
  logger->flush_on(spdlog::level::warn);

  spdlog::set_default_logger(logger);

  // FATAL 동기 로거: 같은 sink 를 공유하는 *비동기 아님* 로거. critical 에 flush_on
  //   → LOG_FATAL 이 호출 스레드에서 즉시 디스크에 기록+flush 하고 리턴한다.
  //   프로세스가 그 직후 죽어도 로그가 남는다. FATAL 은 프로세스 임종이라 동기 I/O
  //   비용은 무의미(빈도 0에 수렴).
  auto fatal =
      std::make_shared<spdlog::logger>("fatal", sinks.begin(), sinks.end());
  fatal->set_level(spdlog::level::critical);
  fatal->set_pattern(kPattern);
  fatal->flush_on(spdlog::level::critical);
  {
    std::lock_guard<std::mutex> lk(g_fatal_mtx);
    g_fatal = std::move(fatal);
  }
}

void Shutdown()
{
  if (!g_inited.exchange(false))
  {
    return;  // Init 안 됐거나 이미 Shutdown — no-op (idempotent)
  }
  {
    // FATAL 동기 로거를 먼저 내린다. 이후 LOG_FATAL 은 fallback(동기 stdout)로 감.
    std::lock_guard<std::mutex> lk(g_fatal_mtx);
    g_fatal.reset();
  }
  spdlog::shutdown();  // 큐 drain + 백그라운드 스레드 join (블로킹)
  // 이후 LOG_* 가 null 기본 로거를 만나지 않도록 동기 stdout 로거로 복원.
  //   drop 은 방어적: 배치가 바뀌어 "fallback" 이 잔존해도 재등록 예외를 막는다(리뷰 F).
  spdlog::drop("fallback");
  spdlog::set_default_logger(spdlog::stdout_color_mt("fallback"));
}

}  // namespace game::core::log
