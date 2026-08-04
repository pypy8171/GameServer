#include <gtest/gtest.h>

#include <string>

#include "core/config/server_config.h"

using namespace game::core;

// 기본 파싱: "key:value" 한 줄씩 → 정수 조회.
TEST(ServerConfig, ParsesKeyColonValue) {
  const auto cfg = ServerConfig::Parse("port:7777\nsessioncnt:5000\n");
  EXPECT_EQ(cfg.GetInt("port", 0), 7777);
  EXPECT_EQ(cfg.GetInt("sessioncnt", 0), 5000);
  EXPECT_TRUE(cfg.Has("port"));
}

// 실패 경로: 없는 키는 호출자가 준 기본값으로 폴백.
TEST(ServerConfig, ReturnsFallbackForMissingKey) {
  const auto cfg = ServerConfig::Parse("port:7777\n");
  EXPECT_EQ(cfg.GetInt("absent", 42), 42);
  EXPECT_EQ(cfg.GetString("absent", "def"), "def");
  EXPECT_FALSE(cfg.Has("absent"));
}

// 주석('#' 이후)과 빈 줄은 무시. 들여쓴 주석도.
TEST(ServerConfig, IgnoresCommentsAndBlankLines) {
  const auto cfg = ServerConfig::Parse("# 주석\n\n  # 들여쓴 주석\nport:9000\n");
  EXPECT_EQ(cfg.GetInt("port", 0), 9000);
  EXPECT_FALSE(cfg.Has("주석"));
}

// 키·값 앞뒤 공백 트림.
TEST(ServerConfig, TrimsWhitespaceAroundKeyAndValue) {
  const auto cfg = ServerConfig::Parse("  port  :  8080  \n");
  EXPECT_EQ(cfg.GetInt("port", 0), 8080);
}

// 중복 키는 마지막 값이 이긴다.
TEST(ServerConfig, LastValueWinsOnDuplicateKey) {
  const auto cfg = ServerConfig::Parse("port:1\nport:2\n");
  EXPECT_EQ(cfg.GetInt("port", 0), 2);
}

// 실패 경로: uint16 범위 밖(초과/음수)은 fallback. 포트 주입의 핵심 방어.
TEST(ServerConfig, GetUInt16ClampsOutOfRangeToFallback) {
  const auto cfg = ServerConfig::Parse("big:70000\nneg:-1\nok:8080\n");
  EXPECT_EQ(cfg.GetUInt16("big", 7777), 7777);  // 65535 초과 → fallback
  EXPECT_EQ(cfg.GetUInt16("neg", 7777), 7777);  // 음수 → fallback
  EXPECT_EQ(cfg.GetUInt16("ok", 7777), 8080);
}

// 실패 경로: 구분자(':') 없는 줄은 조용히 건너뛴다(크래시 금지).
TEST(ServerConfig, MalformedLineWithoutSeparatorIsSkipped) {
  const auto cfg = ServerConfig::Parse("garbage_no_colon\nport:7777\n");
  EXPECT_EQ(cfg.GetInt("port", 0), 7777);
  EXPECT_FALSE(cfg.Has("garbage_no_colon"));
}

// 실패 경로: 정수로 못 읽는 값은 fallback(부분 파싱/쓰레기 방어).
TEST(ServerConfig, NonNumericIntValueFallsBack) {
  const auto cfg = ServerConfig::Parse("port:abc\n");
  EXPECT_EQ(cfg.GetInt("port", 7777), 7777);
}

// 경계: 빈 입력은 크래시 없이 빈 설정(모든 조회 fallback).
TEST(ServerConfig, EmptyInputYieldsEmptyConfig) {
  const auto cfg = ServerConfig::Parse("");
  EXPECT_FALSE(cfg.Has("port"));
  EXPECT_EQ(cfg.GetInt("port", 7777), 7777);
}

// 경계: 마지막 줄에 개행이 없어도 정확히 파싱(오프바이원 회귀 가드).
TEST(ServerConfig, LastLineWithoutTrailingNewlineParses) {
  const auto cfg = ServerConfig::Parse("port:8080");
  EXPECT_EQ(cfg.GetInt("port", 0), 8080);
}

// 경계: 빈 값("port:")은 키는 존재하나 정수 파싱 실패 → fallback.
TEST(ServerConfig, EmptyValueParsesAsPresentButIntFallsBack) {
  const auto cfg = ServerConfig::Parse("port:\n");
  EXPECT_TRUE(cfg.Has("port"));
  EXPECT_EQ(cfg.GetString("port", "x"), "");
  EXPECT_EQ(cfg.GetInt("port", 7777), 7777);
}

// CLI 등 맵 밖 값의 안전 파싱 유틸: 정상/비숫자/범위밖/부분파싱.
TEST(ServerConfig, ParseUInt16RejectsBadInputWithoutThrowing) {
  uint16_t out = 0;
  EXPECT_TRUE(ServerConfig::ParseUInt16("8080", out));
  EXPECT_EQ(out, 8080);

  out = 0;  // 실패 시 out 불변 계약 확인용 초기화
  EXPECT_FALSE(ServerConfig::ParseUInt16("abc", out));
  EXPECT_EQ(out, 0);
  EXPECT_FALSE(ServerConfig::ParseUInt16("70000", out));  // 범위 초과
  EXPECT_FALSE(ServerConfig::ParseUInt16("-1", out));     // 음수
  EXPECT_FALSE(ServerConfig::ParseUInt16("80abc", out));  // 부분 파싱
  EXPECT_FALSE(ServerConfig::ParseUInt16("", out));       // 빈 문자열
  EXPECT_EQ(out, 0);  // 어떤 실패에도 out 은 그대로
}
