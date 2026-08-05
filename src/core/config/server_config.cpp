#include "core/config/server_config.h"

#include <charconv>
#include <fstream>
#include <sstream>

namespace game::core
{

namespace
{

// 앞뒤 공백(스페이스/탭/CR)을 제거한 뷰를 돌려준다. 원본을 복사하지 않는다.
std::string_view Trim(std::string_view s)
{
  constexpr std::string_view kWs = " \t\r\n";
  const auto begin = s.find_first_not_of(kWs);
  if (begin == std::string_view::npos) return {};
  const auto end = s.find_last_not_of(kWs);
  return s.substr(begin, end - begin + 1);
}

// 문자열 전체가 하나의 정수로 소진돼야 성공. 뒤에 잔여 문자/overflow/빈 값이면
// false 를 돌려 "유효한 정수 아님"을 값과 분리해 알린다(센티넬 충돌 회피).
bool ParseInt(const std::string& v, int& out)
{
  const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
  return ec == std::errc{} && ptr == v.data() + v.size();
}

}  // namespace

ServerConfig ServerConfig::Parse(std::string_view text)
{
  ServerConfig cfg;
  size_t pos = 0;
  while (pos <= text.size())
  {
    const auto nl = text.find('\n', pos);
    const auto line_end = (nl == std::string_view::npos) ? text.size() : nl;
    std::string_view line = text.substr(pos, line_end - pos);
    pos = line_end + 1;

    line = Trim(line);
    if (line.empty() || line.front() == '#') continue;  // 빈 줄·주석 무시

    const auto sep = line.find(':');
    if (sep == std::string_view::npos) continue;  // 구분자 없는 줄 skip

    const std::string_view key = Trim(line.substr(0, sep));
    const std::string_view value = Trim(line.substr(sep + 1));
    if (key.empty()) continue;

    cfg.values_[std::string(key)] = std::string(value);  // 중복 키는 마지막 승리

    if (nl == std::string_view::npos) break;
  }
  return cfg;
}

ServerConfig ServerConfig::FromFile(const std::string& path, bool* ok)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    if (ok != nullptr) *ok = false;
    return ServerConfig{};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  // 열기 성공 후에도 읽기(rdbuf) 중 I/O 실패면 *ok=false 로 정직하게 보고.
  if (ok != nullptr) *ok = !in.bad();
  return Parse(ss.str());
}

bool ServerConfig::Has(std::string_view key) const
{
  return values_.find(std::string(key)) != values_.end();
}

std::string ServerConfig::GetString(std::string_view key,
                                    std::string_view fallback) const
{
  const auto it = values_.find(std::string(key));
  return it != values_.end() ? it->second : std::string(fallback);
}

int ServerConfig::GetInt(std::string_view key, int fallback) const
{
  const auto it = values_.find(std::string(key));
  int out = 0;
  if (it == values_.end() || !ParseInt(it->second, out)) return fallback;
  return out;
}

uint16_t ServerConfig::GetUInt16(std::string_view key, uint16_t fallback) const
{
  const auto it = values_.find(std::string(key));
  int out = 0;
  // 키 존재 + 정수 파싱 성공 + [0,65535] 범위 를 모두 만족해야 채택.
  if (it == values_.end() || !ParseInt(it->second, out) || out < 0 ||
      out > 0xFFFF)
  {
    return fallback;
  }
  return static_cast<uint16_t>(out);
}

bool ServerConfig::ParseUInt16(std::string_view text, uint16_t& out)
{
  int v = 0;
  if (!ParseInt(std::string(text), v) || v < 0 || v > 0xFFFF) return false;
  out = static_cast<uint16_t>(v);
  return true;
}

}  // namespace game::core
