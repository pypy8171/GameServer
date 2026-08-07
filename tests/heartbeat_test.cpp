// 세션 생존성(ADR-V)의 순수 heartbeat 판정 로직 테스트.
//   Heartbeat 는 asio·clock 을 모른다 — '시각'을 인자로 주입받아 단위테스트한다.
//   급소: (1) 미설정(timeout 0)은 비활성, (2) 활동 최근이면 Ping(살아있음),
//   (3) 활동 이후 timeout 초과면 Close(죽음), (4) 수신 활동(any-recv)이 데드라인 리셋,
//   (5) 경계(정확히 timeout)는 아직 살아있음(Ping).
#include <gtest/gtest.h>

#include "core/net/heartbeat.h"

using game::core::Heartbeat;
using game::core::HeartbeatAction;

// timeout 0 = 비활성: 기본 생성자는 heartbeat 를 걸지 않는다(기존 세션 무변경).
TEST(Heartbeat, DisabledByDefault)
{
  Heartbeat h;  // 기본 = timeout 0
  EXPECT_FALSE(h.enabled());
}

TEST(Heartbeat, EnabledWhenTimeoutPositive)
{
  Heartbeat h(/*timeout_sec=*/30.0);
  EXPECT_TRUE(h.enabled());
}

// 활동이 최근이면(경과 < timeout) 살아있는 것으로 보고 Ping(프로브 발신 + 재무장).
TEST(Heartbeat, PingsWhileActivityRecent)
{
  Heartbeat h(/*timeout_sec=*/30.0);
  h.OnActivity(100.0);
  EXPECT_EQ(h.Classify(120.0), HeartbeatAction::Ping);  // 경과 20 < 30
}

// 마지막 활동 이후 timeout 을 넘기면 죽은 세션으로 간주 → Close.
TEST(Heartbeat, ClosesWhenActivityStale)
{
  Heartbeat h(/*timeout_sec=*/30.0);
  h.OnActivity(100.0);
  EXPECT_EQ(h.Classify(131.0), HeartbeatAction::Close);  // 경과 31 > 30
}

// any-recv: 어떤 수신 활동이든 데드라인을 리셋한다(활성 클라 오살 방지).
TEST(Heartbeat, ActivityResetsDeadline)
{
  Heartbeat h(/*timeout_sec=*/30.0);
  h.OnActivity(100.0);
  EXPECT_EQ(h.Classify(125.0), HeartbeatAction::Ping);  // 경과 25 < 30 — 살아있음
  h.OnActivity(125.0);                                  // 수신 → 리셋
  EXPECT_EQ(h.Classify(150.0), HeartbeatAction::Ping);  // 리셋 기준 경과 25 < 30
  EXPECT_EQ(h.Classify(156.0), HeartbeatAction::Close); // 리셋 기준 경과 31 > 30
}

// 경계: 정확히 timeout 만큼 경과했을 때는 아직 살아있음(Ping). 초과(>)에서만 Close.
TEST(Heartbeat, ExactTimeoutIsStillAlive)
{
  Heartbeat h(/*timeout_sec=*/30.0);
  h.OnActivity(100.0);
  EXPECT_EQ(h.Classify(130.0), HeartbeatAction::Ping);  // 경과 정확히 30 — 아직 살아있음
}
