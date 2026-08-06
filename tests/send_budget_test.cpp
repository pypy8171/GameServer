// send 큐 백프레셔(ADR-E)의 순수 바이트 예산 로직 테스트.
//   SendBudget 은 asio 를 모른다 — strand 안에서 쓰이지만 그 자체는 순수 산술이라
//   단일스레드에서 직접 검증한다(결정을 I/O 에서 분리 — cpp-tdd 규율).
//   상한 판정의 급소: (1) 개수가 아니라 '바이트 합', (2) 경계에서 정확히 뒤집힘,
//   (3) 상한보다 큰 단일 프레임도 거부, (4) 전송 완료분 회수로 예산 재개방.
#include <gtest/gtest.h>

#include "core/net/send_budget.h"

using game::core::ClampSendQueueCap;
using game::core::SendBudget;

// cap 바이트까지는 수용(경계 포함), cap 을 넘기는 프레임은 거부하고 예산 불변.
TEST(SendBudget, AdmitsUpToCapThenRejectsAtBoundary)
{
  SendBudget b(/*cap_bytes=*/10);
  EXPECT_TRUE(b.TryAdmit(4));   // queued 0→4
  EXPECT_EQ(b.queued(), 4u);
  EXPECT_TRUE(b.TryAdmit(6));   // queued 4→10 (정확히 cap — 허용)
  EXPECT_EQ(b.queued(), 10u);
  EXPECT_FALSE(b.TryAdmit(1));  // 10+1 > 10 — 거부
  EXPECT_EQ(b.queued(), 10u);   // 거부는 예산을 건드리지 않는다(수용 안 함)
}

// 판정 단위는 프레임 '개수'가 아니라 '바이트 합'이다.
TEST(SendBudget, GatesByByteSumNotFrameCount)
{
  SendBudget b(/*cap_bytes=*/5);
  EXPECT_TRUE(b.TryAdmit(2));   // queued 2
  EXPECT_TRUE(b.TryAdmit(2));   // queued 4
  EXPECT_FALSE(b.TryAdmit(2));  // 4+2=6 > 5 — 개수는 3뿐이어도 바이트로 막힌다
  EXPECT_EQ(b.queued(), 4u);
}

// 상한보다 큰 단일 프레임은 빈 큐에서도 거부된다(oversize 가드).
TEST(SendBudget, RejectsSingleFrameLargerThanCap)
{
  SendBudget b(/*cap_bytes=*/8);
  EXPECT_FALSE(b.TryAdmit(16));  // 빈 큐(0)여도 16 > 8 — 거부
  EXPECT_EQ(b.queued(), 0u);
}

// 전송 완료분(큐에서 빠진 바이트)을 회수하면 예산이 다시 열린다.
TEST(SendBudget, OnSentReclaimsBudget)
{
  SendBudget b(/*cap_bytes=*/10);
  ASSERT_TRUE(b.TryAdmit(10));   // 가득
  EXPECT_FALSE(b.TryAdmit(6));   // 초과 거부
  b.OnSent(6);                   // 6바이트 전송 완료 → queued 10→4
  EXPECT_EQ(b.queued(), 4u);
  EXPECT_TRUE(b.TryAdmit(6));    // 이제 4+6=10 — 다시 허용
  EXPECT_EQ(b.queued(), 10u);
}

// cap 하한 가드: min_cap(단일 최대 프레임) 미만으로 설정된 상한은 min_cap 으로 승격.
//   미만 설정을 그대로 두면 정상 최대 패킷조차 수용 거부 → 세션 자해(위 규율 참조).
TEST(SendBudget, ClampRaisesUndersizedCapToMinimum)
{
  constexpr std::size_t kMin = 16u * 1024;    // = kMaxPacketSize 를 모사한 하한
  EXPECT_EQ(ClampSendQueueCap(0, kMin), kMin);        // 0(또는 미설정) → 하한
  EXPECT_EQ(ClampSendQueueCap(100, kMin), kMin);      // 한 프레임도 못 담음 → 하한
  EXPECT_EQ(ClampSendQueueCap(kMin, kMin), kMin);     // 경계: 그대로
  EXPECT_EQ(ClampSendQueueCap(kMin + 1, kMin), kMin + 1);  // 이상: 원값 보존
  EXPECT_EQ(ClampSendQueueCap(1u << 20, kMin), 1u << 20);  // 넉넉한 값 보존
}
