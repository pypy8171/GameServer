#include "core/log/log.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace game::core::log {

namespace {

spdlog::level::level_enum ToSpd(Level l) {
  switch (l) {
    case Level::kDebug: return spdlog::level::debug;
    case Level::kInfo:  return spdlog::level::info;
    case Level::kWarn:  return spdlog::level::warn;
    case Level::kError: return spdlog::level::err;
    case Level::kFatal: return spdlog::level::critical;
  }
  return spdlog::level::info;
}

constexpr std::size_t kQueueSlots = 8192;       // 비동기 큐 깊이
constexpr std::size_t kMaxFileBytes = 5 * 1024 * 1024;  // 5MB 단위 로테이션
constexpr std::size_t kMaxFiles = 3;            // 최대 보관 파일 수

// Init/Shutdown 1회 짝 보장(리뷰 E). 중복 Init 은 thread_pool 을 교체해
//   기존 async_logger 의 weak_ptr 를 깨뜨릴 수 있어 무시한다. Shutdown 이 다시 내린다.
std::atomic<bool> g_inited{false};

}  // namespace

void Init(const std::string& file_path, Level min_level, bool console) {
  if (g_inited.exchange(true)) {
    LOG_WARN("log::Init called more than once — ignoring");
    return;
  }
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path p(file_path);
  if (p.has_parent_path()) {
    fs::create_directories(p.parent_path(), ec);  // 실패해도 sink 생성에서 재판정
  }

  // 백그라운드 스레드 1개 = MPSC(N개 io 스레드 producer → 1 소비자). CLAUDE.md 계약.
  spdlog::init_thread_pool(kQueueSlots, 1);

  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      file_path, kMaxFileBytes, kMaxFiles));
  if (console) {
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  }

  // overflow=block: 큐가 가득 차면 producer 가 잠깐 대기(로그 유실 없음).
  //   "먼저 옳게" — 유실 대신 대기. 저지연 우선으로 overrun_oldest 전환은 /perf-debate 안건.
  auto logger = std::make_shared<spdlog::async_logger>(
      "server", sinks.begin(), sinks.end(), spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);

  logger->set_level(ToSpd(min_level));
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [tid=%t] %v");
  // WARN+ 발생 시 flush 를 큐에 post(백그라운드가 처리) — async 라 '즉시'가 아니다.
  //   정상 종료 경로는 Shutdown 이 큐를 drain 하지만, 하드 크래시/Shutdown 미호출
  //   종료 시엔 큐 잔여분(FATAL 포함)이 유실될 수 있다(리뷰 B). FATAL 동기 flush 는 M5.
  logger->flush_on(spdlog::level::warn);

  spdlog::set_default_logger(logger);
}

void Shutdown() {
  if (!g_inited.exchange(false)) {
    return;  // Init 안 됐거나 이미 Shutdown — no-op (idempotent)
  }
  spdlog::shutdown();  // 큐 drain + 백그라운드 스레드 join (블로킹)
  // 이후 LOG_* 가 null 기본 로거를 만나지 않도록 동기 stdout 로거로 복원.
  //   drop 은 방어적: 배치가 바뀌어 "fallback" 이 잔존해도 재등록 예외를 막는다(리뷰 F).
  spdlog::drop("fallback");
  spdlog::set_default_logger(spdlog::stdout_color_mt("fallback"));
}

}  // namespace game::core::log
