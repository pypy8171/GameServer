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

// 랜덤 솔트: 요청 바이트 수의 hex 길이를 갖고, 호출마다 값이 다르다(예측 불가). [S-5]
TEST(PasswordHash, GenerateSaltIsRandomAndSized)
{
  const std::string a = GenerateSalt(16);
  const std::string b = GenerateSalt(16);
  EXPECT_EQ(a.size(), 32u);  // 16바이트 → hex 32자
  EXPECT_EQ(GenerateSalt(8).size(), 16u);
  EXPECT_NE(a, b);  // 두 호출이 같으면 사실상 랜덤이 아니다(엔트로피 부재)
}

// 검증: 저장된 해시와 후보 비번을 솔트로 재계산해 비교.
TEST(PasswordHash, VerifyAcceptsCorrectRejectsWrong)
{
  const std::string stored = HashPassword("s3cret", "saltA");
  EXPECT_TRUE(VerifyPassword("s3cret", "saltA", stored));    // 정답
  EXPECT_FALSE(VerifyPassword("wrong", "saltA", stored));    // 틀린 비번
  EXPECT_FALSE(VerifyPassword("s3cret", "saltB", stored));   // 틀린 솔트
}

// 상수시간 비교의 정확성(타이밍이 아니라 값 판정): 같은 내용만 true.
//   길이 불일치·내용 불일치·빈 문자열 경계를 확인한다. 타이밍 자체는 단위테스트
//   대상이 아니므로(측정 불가) 여기선 '조기 반환 제거가 결과를 바꾸지 않음'을 고정한다.
TEST(PasswordHash, ConstantTimeEqualsMatchesOnlyIdenticalContent)
{
  EXPECT_TRUE(ConstantTimeEquals("abcdef", "abcdef"));   // 완전 일치
  EXPECT_FALSE(ConstantTimeEquals("abcdef", "abcdeg"));  // 마지막 바이트만 다름
  EXPECT_FALSE(ConstantTimeEquals("abcXef", "abcdef"));  // 중간 바이트만 다름
  EXPECT_FALSE(ConstantTimeEquals("abc", "abcdef"));     // 길이 불일치(짧음)
  EXPECT_FALSE(ConstantTimeEquals("abcdefg", "abcdef")); // 길이 불일치(긺)
  EXPECT_TRUE(ConstantTimeEquals("", ""));               // 빈-빈 경계 = 일치
}
