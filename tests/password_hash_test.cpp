#include <gtest/gtest.h>

#include <string>

#include "game_logic/account/password_hash.h"

using namespace game::logic;

// 해시는 결정적이다: 같은 (비번, 솔트) → 같은 다이제스트. 그리고 평문이 아니다.
TEST(PasswordHash, IsDeterministicAndNotPlaintext)
{
  const std::string h1 = HashPassword("s3cret", "saltA");
  const std::string h2 = HashPassword("s3cret", "saltA");
  EXPECT_EQ(h1, h2);
  EXPECT_FALSE(h1.empty());
  EXPECT_NE(h1, "s3cret");  // 평문 저장 금지 계약
}

// 솔트가 다르면 같은 비번이라도 다른 다이제스트 → 레인보우/재사용 방어.
TEST(PasswordHash, DifferentSaltYieldsDifferentDigest)
{
  EXPECT_NE(HashPassword("s3cret", "saltA"), HashPassword("s3cret", "saltB"));
}

// 검증: 저장된 해시와 후보 비번을 솔트로 재계산해 비교.
TEST(PasswordHash, VerifyAcceptsCorrectRejectsWrong)
{
  const std::string stored = HashPassword("s3cret", "saltA");
  EXPECT_TRUE(VerifyPassword("s3cret", "saltA", stored));    // 정답
  EXPECT_FALSE(VerifyPassword("wrong", "saltA", stored));    // 틀린 비번
  EXPECT_FALSE(VerifyPassword("s3cret", "saltB", stored));   // 틀린 솔트
}
