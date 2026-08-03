// 로거 검증: 파일에 기록되는가 + 레벨 필터가 동작하는가.
//   비동기 로거이므로 Shutdown()으로 큐를 drain 한 뒤 파일을 읽어 확정 검사한다.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/log/log.h"

using namespace game::core;

TEST(Log, WritesLevelsAndFiltersBelowMin) {
  namespace fs = std::filesystem;
  const std::string path = "logs/test_log.log";
  std::error_code ec;
  fs::remove(path, ec);  // 이전 잔여 제거

  // min_level=INFO → DEBUG 는 필터링(또는 Release 에선 컴파일 아웃)되어야 한다.
  log::Init(path, log::Level::kInfo, /*console=*/false);
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
}
