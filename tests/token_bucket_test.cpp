// 인바운드 rate-limit(ADR-R)의 순수 토큰버킷 로직 테스트.
//   TokenBucket 은 asio·clock 을 모른다 — '시각'을 인자로 주입받아 단위테스트한다.
//   급소: (1) 미설정(capacity 0)은 무제한 허용, (2) 버스트 소진 후 거부,
//   (3) 시간 경과분만큼 리필돼 재허용, (4) 리필은 capacity 로 클램프,
//   (5) 시계 역행/동일 시각은 리필하지 않음.
#include <gtest/gtest.h>

#include "core/net/token_bucket.h"

using game::core::TokenBucket;

// capacity 0 = 비활성: 시각과 무관하게 항상 허용(rate-limit 미설정 경로).
TEST(TokenBucket, DisabledBucketAlwaysAdmits)
{
  TokenBucket b;  // 기본 = capacity 0
  EXPECT_FALSE(b.enabled());
  for (int i = 0; i < 1000; ++i)
  {
    EXPECT_TRUE(b.TryConsume(/*now_sec=*/0.0));
  }
}

// 버스트: 같은 시각에 capacity 개까지 허용, 그다음은 거부(리필 없음).
TEST(TokenBucket, AdmitsBurstUpToCapacityThenRejects)
{
  TokenBucket b(/*capacity=*/3, /*refill_per_sec=*/0.0);
  EXPECT_TRUE(b.TryConsume(0.0));   // 3→2
  EXPECT_TRUE(b.TryConsume(0.0));   // 2→1
  EXPECT_TRUE(b.TryConsume(0.0));   // 1→0
  EXPECT_FALSE(b.TryConsume(0.0));  // 0 — 거부
}

// 리필: 시간이 지나면 초당 refill 만큼 토큰이 돌아와 다시 허용된다.
TEST(TokenBucket, RefillsOverTime)
{
  TokenBucket b(/*capacity=*/2, /*refill_per_sec=*/1.0);
  EXPECT_TRUE(b.TryConsume(0.0));   // 2→1
  EXPECT_TRUE(b.TryConsume(0.0));   // 1→0
  EXPECT_FALSE(b.TryConsume(0.0));  // 0 — 거부
  EXPECT_TRUE(b.TryConsume(1.0));   // +1초 → +1토큰 → 1, 소비 후 0
  EXPECT_FALSE(b.TryConsume(1.0));  // 다시 0 — 거부
}

// 리필은 capacity 를 넘지 않는다: 오래 쉬어도 버스트 상한 이상 축적 불가.
TEST(TokenBucket, RefillClampsToCapacity)
{
  TokenBucket b(/*capacity=*/2, /*refill_per_sec=*/1.0);
  EXPECT_TRUE(b.TryConsume(0.0));   // 2→1
  EXPECT_TRUE(b.TryConsume(0.0));   // 1→0
  // 100초 경과 → 이론상 +100 이지만 capacity(2)로 클램프. 정확히 2개만 허용.
  EXPECT_TRUE(b.TryConsume(100.0));  // 2→1
  EXPECT_TRUE(b.TryConsume(100.0));  // 1→0
  EXPECT_FALSE(b.TryConsume(100.0)); // 0 — 클램프됐으므로 3개째는 거부
}

// 동일 시각/시계 역행에는 리필하지 않는다(음수 리필로 토큰이 늘거나 줄지 않음).
TEST(TokenBucket, NoRefillOnSameOrBackwardTime)
{
  TokenBucket b(/*capacity=*/1, /*refill_per_sec=*/1000.0);
  EXPECT_TRUE(b.TryConsume(5.0));   // 1→0 (기준시각=5)
  EXPECT_FALSE(b.TryConsume(5.0));  // 동일 시각 — 리필 없음 → 거부
  EXPECT_FALSE(b.TryConsume(4.0));  // 역행 — 리필 없음 → 거부
}
