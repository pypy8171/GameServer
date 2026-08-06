#include "game_logic/account/password_hash.h"

#include <cstdint>
#include <random>

namespace game::logic
{

namespace
{

// FNV-1a 64-bit. ⚠️ 비암호학적 — 데모 전용(실서비스는 argon2id/bcrypt).
//   솔트를 비밀번호 앞에 흡수시켜 계정별로 다이제스트를 분리한다.
std::uint64_t Fnv1a64(std::string_view data, std::uint64_t seed)
{
  std::uint64_t h = seed;
  for (const unsigned char c : data)
  {
    h ^= static_cast<std::uint64_t>(c);
    h *= 0x100000001b3ULL;  // FNV prime
  }
  return h;
}

std::string ToHex(std::uint64_t v)
{
  static const char* kDigits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i)
  {
    out[i] = kDigits[v & 0xF];
    v >>= 4;
  }
  return out;
}

}  // namespace

std::string GenerateSalt(std::size_t n_bytes)
{
  // std::random_device 는 플랫폼 CSPRNG(Windows RtlGenRandom/rdrand, Linux
  //   /dev/urandom)에 연결된다 — 솔트용 예측불가 엔트로피로 충분(솔트는 비밀 아님).
  //   호출마다 새 인스턴스: 솔트 생성은 부트스트랩 저빈도 경로라 비용 무시 가능.
  std::random_device rd;
  std::uniform_int_distribution<unsigned> byte_dist(0, 255);
  static const char* kDigits = "0123456789abcdef";
  std::string out;
  out.reserve(n_bytes * 2);
  for (std::size_t i = 0; i < n_bytes; ++i)
  {
    const unsigned b = byte_dist(rd);
    out.push_back(kDigits[(b >> 4) & 0xF]);
    out.push_back(kDigits[b & 0xF]);
  }
  return out;
}

std::string HashPassword(std::string_view password, std::string_view salt)
{
  // salt 를 오프셋 베이시스로 흡수한 뒤 비밀번호를 해싱 → 솔트별 다이제스트 분리.
  const std::uint64_t seeded = Fnv1a64(salt, 0xcbf29ce484222325ULL);  // FNV offset
  return ToHex(Fnv1a64(password, seeded));
}

bool ConstantTimeEquals(std::string_view a, std::string_view b)
{
  // 길이가 다르면 불일치. 해시 길이는 고정·공개값이라 조기 반환이 비밀을 흘리지 않는다.
  if (a.size() != b.size())
  {
    return false;
  }
  // 같은 길이면 전 바이트를 XOR 누적 — 첫 불일치에서 멈추지 않아 시간이 내용에 무관.
  unsigned char diff = 0;
  for (std::size_t i = 0; i < a.size(); ++i)
  {
    diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  }
  return diff == 0;
}

bool VerifyPassword(std::string_view password, std::string_view salt,
                    std::string_view expected_hash)
{
  // 재계산한 다이제스트와 저장본을 상수시간 비교(타이밍 사이드채널 차단).
  return ConstantTimeEquals(HashPassword(password, salt), expected_hash);
}

}  // namespace game::logic
