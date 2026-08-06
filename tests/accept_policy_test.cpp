// accept 수용 제어(U-4 백오프 / S-3 접속상한, ADR-T)의 순수 판정 로직 테스트.
//   asio glue(steady_timer 재무장)와 분리해 '무엇을 할지'만 검증한다:
//   성공→다음수락, 취소→정지, 그 외 에러→백오프. 그리고 cap 판정.
#include <gtest/gtest.h>

#include <asio.hpp>

#include "core/net/accept_policy.h"

using game::core::AcceptAction;
using game::core::AcceptWithinCap;
using game::core::ClassifyAcceptResult;

// 성공(ec 없음) → 곧바로 다음 연결 수락.
TEST(AcceptPolicy, SuccessAcceptsNext)
{
  EXPECT_EQ(ClassifyAcceptResult(std::error_code{}), AcceptAction::kAcceptNext);
}

// operation_aborted(acceptor 취소/종료) → 정지. 재무장하면 종료가 안 끝난다.
TEST(AcceptPolicy, AbortedStops)
{
  const std::error_code aborted = asio::error::make_error_code(
      asio::error::operation_aborted);
  EXPECT_EQ(ClassifyAcceptResult(aborted), AcceptAction::kStop);
}

// 그 외 에러(자원 고갈 등) → 즉시 재무장 대신 백오프 후 재시도(busy-spin 방지).
TEST(AcceptPolicy, OtherErrorBacksOff)
{
  const std::error_code refused = asio::error::make_error_code(
      asio::error::connection_refused);
  EXPECT_EQ(ClassifyAcceptResult(refused), AcceptAction::kBackoffThenAccept);
}

// 접속 상한: cap==0 은 무제한, 그 외엔 current<cap 일 때만 수용(경계 포함 확인).
TEST(AcceptPolicy, CapGatesByCurrentCount)
{
  EXPECT_TRUE(AcceptWithinCap(/*current=*/1000000, /*cap=*/0));  // 0=무제한
  EXPECT_TRUE(AcceptWithinCap(0, 2));   // 0<2
  EXPECT_TRUE(AcceptWithinCap(1, 2));   // 1<2
  EXPECT_FALSE(AcceptWithinCap(2, 2));  // 2>=2 — 한계 도달, 거부
  EXPECT_FALSE(AcceptWithinCap(3, 2));  // 초과
}
