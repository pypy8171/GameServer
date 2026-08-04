#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace game::core {

// 기동 설정 — 최소형 txt "key:value" 파서.
//   - 한 줄에 `key : value`(콜론 구분). 앞뒤 공백 트림.
//   - 줄 맨 앞이 '#' 인 줄만 통째로 주석(인라인 주석 미지원). 빈 줄 무시.
//     구분자(':') 없는 줄은 건너뜀(크래시 금지).
//   - 중복 키는 마지막 값이 이긴다. 미지정 키는 조회 시 준 기본값(fallback).
// 게임 무관 인프라라 코어에 둔다. 조회는 실패해도 예외를 던지지 않고 fallback.
class ServerConfig {
 public:
  // 텍스트에서 직접 파싱(파일 없이 테스트 가능).
  static ServerConfig Parse(std::string_view text);
  // 파일에서 로드. 못 읽으면 빈 설정(모든 조회가 fallback). ok!=nullptr 이면 성패 기록.
  static ServerConfig FromFile(const std::string& path, bool* ok = nullptr);

  bool Has(std::string_view key) const;
  std::string GetString(std::string_view key, std::string_view fallback) const;
  int GetInt(std::string_view key, int fallback) const;
  // 정수로 읽되 [0, 65535] 밖이면 fallback. 포트/카운트 주입용.
  uint16_t GetUInt16(std::string_view key, uint16_t fallback) const;

  // 문자열 하나를 uint16 로 안전 파싱. 전량 정수 + [0,65535] 면 out 채우고 true,
  // 아니면 out 불변 + false. CLI 인자 등 맵 밖의 값에도 동일 규율을 쓰기 위한 유틸.
  // (std::stoi 는 비숫자/overflow 에 예외를 던져 기동 즉시 terminate → 사용 금지.)
  static bool ParseUInt16(std::string_view text, uint16_t& out);

 private:
  std::unordered_map<std::string, std::string> values_;
};

}  // namespace game::core
