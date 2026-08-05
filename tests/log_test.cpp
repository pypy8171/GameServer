// 로거 검증: 파일에 기록되는가 + 레벨 필터가 동작하는가.
//   비동기 로거이므로 Shutdown()으로 큐를 drain 한 뒤 파일을 읽어 확정 검사한다.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/log/log.h"

using namespace game::core;

TEST(Log, WritesLevelsAndFiltersBelowMin)
{
  namespace fs = std::filesystem;
  const std::string path = "logs/test_log.log";
  std::error_code ec;
  fs::remove(path, ec);  // 이전 잔여 제거

  // min_level=INFO → DEBUG 는 필터링(또는 Release 에선 컴파일 아웃)되어야 한다.
  log::Init(path, log::Level::Info, /*console=*/false);
  LOG_DEBUG("debug-should-be-absent");
  LOG_INFO("info-line-{}", 42);
  LOG_WARN("warn-line");
  LOG_ERROR("err-line");
  log::Shutdown();  // 큐 drain(블로킹) → 파일 확정

  std::ifstream in(path);
  ASSERT_TRUE(in.good()) << "log file not created: " << path;
  const std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

  EXPECT_NE(content.find("info-line-42"), std::string::npos);
  EXPECT_NE(content.find("warn-line"), std::string::npos);
  EXPECT_NE(content.find("err-line"), std::string::npos);
  // DEBUG 는 min_level 아래라 남으면 안 된다.
  EXPECT_EQ(content.find("debug-should-be-absent"), std::string::npos);
  // 레벨 태그가 패턴에 실제로 찍히는지(관측성 회귀 방지).
  EXPECT_NE(content.find("[info]"), std::string::npos);
  EXPECT_NE(content.find("[error]"), std::string::npos);
  // 소스 위치(파일:줄)가 패턴에 찍히는지 — LOG_* 매크로가 캡처한 __FILE__.
  //   "어떤 cpp의 몇 번째 줄이 남겼는가"를 로그만으로 추적 가능해야 한다.
  EXPECT_NE(content.find("log_test.cpp:"), std::string::npos);
}

// FATAL 은 프로세스 임종 로그이므로 비동기 큐가 아니라 '동기'로 즉시 디스크에
//   기록돼야 한다(리뷰 B 해소). 핵심 회귀 가드: **Shutdown 을 부르지 않고** LOG_FATAL
//   리턴 직후 파일을 읽어 이미 FATAL 라인이 있는지 본다. async 로 되돌아가면(동기 flush
//   제거) Shutdown 전이라 이 라인이 없어 테스트가 깨진다.
TEST(Log, FatalIsFlushedSynchronouslyBeforeShutdown)
{
  namespace fs = std::filesystem;
  const std::string path = "logs/test_fatal.log";
  std::error_code ec;
  fs::remove(path, ec);

  log::Init(path, log::Level::Info, /*console=*/false);
  LOG_INFO("info-before-fatal");  // async — 큐에 남아 아직 디스크에 없을 수 있음(정상)
  LOG_FATAL("fatal-line-{}", 7);  // 동기 flush → 리턴 시점에 이미 디스크에 있어야 함

  // Shutdown 을 부르지 않고 '지금' 읽는다. 동기 flush 가 없으면 이 라인은 없다.
  {
    std::ifstream in(path);
    ASSERT_TRUE(in.good()) << "log file not created: " << path;
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("fatal-line-7"), std::string::npos)
        << "FATAL must be on disk before Shutdown (synchronous flush)";
    EXPECT_NE(content.find("[critical]"), std::string::npos);  // Fatal→critical
  }

  log::Shutdown();  // 나머지 정리(잔여 async drain + 스레드 join)
  fs::remove(path, ec);
}
