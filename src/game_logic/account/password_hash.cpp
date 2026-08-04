#include "game_logic/account/password_hash.h"

#include <cstdint>

namespace game::logic {

namespace {

// FNV-1a 64-bit. ⚠️ 비암호학적 — 데모 전용(실서비스는 argon2id/bcrypt).
//   솔트를 비밀번호 앞에 흡수시켜 계정별로 다이제스트를 분리한다.
std::uint64_t Fnv1a64(std::string_view data, std::uint64_t seed) {
  std::uint64_t h = seed;
  for (const unsigned char c : data) {
    h ^= static_cast<std::uint64_t>(c);
    h *= 0x100000001b3ULL;  // FNV prime
  }
  return h;
}

std::string ToHex(std::uint64_t v) {
  static const char* kDigits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[i] = kDigits[v & 0xF];
    v >>= 4;
  }
  return out;
}

}  // namespace

std::string HashPassword(std::string_view password, std::string_view salt) {
  // salt 를 오프셋 베이시스로 흡수한 뒤 비밀번호를 해싱 → 솔트별 다이제스트 분리.
  const std::uint64_t seeded = Fnv1a64(salt, 0xcbf29ce484222325ULL);  // FNV offset
  return ToHex(Fnv1a64(password, seeded));
}

bool VerifyPassword(std::string_view password, std::string_view salt,
                    std::string_view expected_hash) {
  return HashPassword(password, salt) == expected_hash;
}

}  // namespace game::logic
