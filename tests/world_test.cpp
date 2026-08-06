#include <gtest/gtest.h>

#include <asio.hpp>

#include <limits>

#include "game.pb.h"
#include "game_logic/world/player_entity.h"
#include "game_logic/world/world.h"

using namespace game::logic;
using game::core::SessionId;

namespace
{

// World 는 io_context 로 strand 를 만들지만, 순수 메서드(Enter/Leave/Count/Find)
//   테스트는 단일스레드에서 직접 호출한다(월드 strand 위 호출과 동치). io 는
//   run() 하지 않으므로 Post* 는 여기서 다루지 않는다(그건 스모크/통합 몫).
asio::io_context g_io;

}  // namespace

// ---- 입장(Enter) ----

// 새 세션 입장 → 원점 스폰으로 엔티티 삽입, player_id 보존, 스냅샷 반환.
TEST(World, EnterSpawnsPlayerAtOrigin)
{
  World world(g_io);
  const auto spawn = world.Enter(/*sid=*/1, /*pid=*/42);
  ASSERT_TRUE(spawn.has_value());
  EXPECT_EQ(spawn->id, 42u);
  EXPECT_FLOAT_EQ(spawn->x, 0.0f);
  EXPECT_FLOAT_EQ(spawn->y, 0.0f);
  EXPECT_EQ(world.Count(), 1u);
}

// 같은 SessionId 재입장은 중복 가드로 무시 → nullopt, 카운트 불변.
TEST(World, DuplicateEnterIsRejected)
{
  World world(g_io);
  ASSERT_TRUE(world.Enter(1, 42).has_value());
  const auto again = world.Enter(1, 99);  // 같은 sid, 다른 pid
  EXPECT_FALSE(again.has_value());
  EXPECT_EQ(world.Count(), 1u);
  // 최초 입장의 신원이 유지되어야 한다(덮어쓰기 금지).
  ASSERT_NE(world.Find(1), nullptr);
  EXPECT_EQ(world.Find(1)->id, 42u);
}

// 서로 다른 세션은 독립적으로 입장한다.
TEST(World, DistinctSessionsCoexist)
{
  World world(g_io);
  world.Enter(1, 42);
  world.Enter(2, 43);
  EXPECT_EQ(world.Count(), 2u);
  ASSERT_NE(world.Find(2), nullptr);
  EXPECT_EQ(world.Find(2)->id, 43u);
}

// ---- 퇴장(Leave) ----

// 입장한 세션 퇴장 → 제거, 카운트 감소, true 반환.
TEST(World, LeaveRemovesPlayer)
{
  World world(g_io);
  world.Enter(1, 42);
  EXPECT_TRUE(world.Leave(1));
  EXPECT_EQ(world.Count(), 0u);
  EXPECT_EQ(world.Find(1), nullptr);
}

// 입장한 적 없는 세션 퇴장은 멱등 no-op → false.
TEST(World, LeaveUnknownSessionIsNoop)
{
  World world(g_io);
  EXPECT_FALSE(world.Leave(777));
  EXPECT_EQ(world.Count(), 0u);
}

// ---- 이동(Move) — 순수 코어, 월드 strand 위 호출 전제 ----

// 입장한 세션의 이동 → 위치 갱신, player_id 보존, 갱신된 스냅샷 반환.
TEST(World, MoveUpdatesPositionAndReturnsSnapshot)
{
  World world(g_io);
  world.Enter(/*sid=*/1, /*pid=*/42);
  const auto moved = world.Move(1, 3.0f, -4.0f);
  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(moved->id, 42u);  // 신원(player_id) 보존
  EXPECT_FLOAT_EQ(moved->x, 3.0f);
  EXPECT_FLOAT_EQ(moved->y, -4.0f);
  // 월드 상태에도 반영된다(스냅샷과 저장상태 일치).
  ASSERT_NE(world.Find(1), nullptr);
  EXPECT_FLOAT_EQ(world.Find(1)->x, 3.0f);
  EXPECT_FLOAT_EQ(world.Find(1)->y, -4.0f);
}

// 입장하지 않은(또는 이미 퇴장한) 세션의 이동은 nullopt — 유령 이동 방지(미입장 가드).
TEST(World, MoveOnAbsentSessionReturnsNullopt)
{
  World world(g_io);
  const auto moved = world.Move(/*sid=*/777, 1.0f, 1.0f);
  EXPECT_FALSE(moved.has_value());
  EXPECT_EQ(world.Count(), 0u);
}

// 비유한(NaN/Inf) 좌표 이동은 거부 — nullopt 반환 + 저장 위치 불변. `float x,y` 는
//   악성/버그 클라가 NaN/Inf 비트패턴을 그대로 실을 수 있고, 검증 없이 저장하면
//   MoveNotify 로 전원에 팬아웃돼 정상 클라 렌더/물리 상태를 오염시킨다(리뷰 🟡).
//   ⚠️ 이동거리/속도 상한(anti-cheat)은 게임설계 결정이라 별도 마일스톤(seam 유지).
TEST(World, MoveWithNonFiniteCoordinatesIsRejected)
{
  World world(g_io);
  world.Enter(/*sid=*/1, /*pid=*/42);
  world.Move(1, 3.0f, -4.0f);  // 정상 위치 선점

  const float kNaN = std::numeric_limits<float>::quiet_NaN();
  const float kInf = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(world.Move(1, kNaN, 1.0f).has_value());   // x=NaN
  EXPECT_FALSE(world.Move(1, 1.0f, kInf).has_value());   // y=+Inf
  EXPECT_FALSE(world.Move(1, -kInf, kNaN).has_value());  // 둘 다 비유한

  // 저장 위치는 마지막 유효 이동(3,-4)에서 변하지 않았다(오염 차단).
  ASSERT_NE(world.Find(1), nullptr);
  EXPECT_FLOAT_EQ(world.Find(1)->x, 3.0f);
  EXPECT_FLOAT_EQ(world.Find(1)->y, -4.0f);
}

// ---- 스냅샷 → 알림 매핑(순수 빌더) ----

// PlayerEntity 필드가 WorldEnteredNotify 로 그대로 매핑된다.
TEST(World, MakeWorldEnteredNotifyMapsEntityFields)
{
  const PlayerEntity e{/*id=*/7, /*x=*/1.5f, /*y=*/-2.5f};
  const game::proto::WorldEnteredNotify n = MakeWorldEnteredNotify(e);
  EXPECT_EQ(n.player_id(), 7u);
  EXPECT_FLOAT_EQ(n.x(), 1.5f);
  EXPECT_FLOAT_EQ(n.y(), -2.5f);
}

// PlayerEntity 필드가 MoveNotify 로 그대로 매핑된다(서버가 세션 신원에서 채운 player_id).
TEST(World, MakeMoveNotifyMapsEntityFields)
{
  const PlayerEntity e{/*id=*/7, /*x=*/1.5f, /*y=*/-2.5f};
  const game::proto::MoveNotify n = MakeMoveNotify(e);
  EXPECT_EQ(n.player_id(), 7u);
  EXPECT_FLOAT_EQ(n.x(), 1.5f);
  EXPECT_FLOAT_EQ(n.y(), -2.5f);
}
