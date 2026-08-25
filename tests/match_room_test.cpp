// MatchRoomLeaf — 잎(leaf) Room 의 OnEnter/OnLeave 훅이 Presence 를 자동 Track/Untrack.
//
//   ADR-X 두 층위: 집합체(World)는 core::Room 을 has-a 로 포함, 잎(MatchRoom 등)은
//   상속해 훅을 채운다. 이 사이클(①)이 그 첫 잎이다. 지금까지 presence_sweep_test 는
//   room.Join(s) 와 presence.Track(id,&room) 을 **수동으로 따로** 호출했다 — 잎의 존재
//   이유는 그 둘을 하나로 묶는 것: MatchRoom.Join → OnEnter → presence.Track(자동).
//   이 회귀가드는 "멤버십 변이 한 번으로 방 멤버십과 역인덱스가 함께 움직인다"를 못박아,
//   World+MatchRoom 다중 멤버십(사이클②)과 disconnect sweep(사이클③, ADR-Y)의 기반을 깐다.
#include <gtest/gtest.h>

#include <asio.hpp>
#include <memory>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/net/session.h"
#include "core/net/session_pool.h"
#include "core/net/session_registry.h"
#include "game_logic/match_room/match_room.h"
#include "game_logic/presence/presence.h"

using namespace game::core;
using game::logic::MatchRoom;
using game::logic::Presence;

namespace
{
SessionPtr AcquireDummy(SessionPool& pool, asio::io_context& io, Dispatcher& d,
                        SessionRegistry& r)
{
  asio::ip::tcp::socket sock(io);
  return pool.Acquire(std::move(sock), d, r);
}
}  // namespace

// Join → OnEnter → Presence.Track 자동 구동(수동 Track 없이 역인덱스에 잡힌다).
TEST(MatchRoomLeaf, JoinTracksPresenceViaOnEnter)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);
  Presence presence;
  MatchRoom room(io, /*id=*/2, presence);

  auto s = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(s);
  const SessionId id = s->id();

  room.Join(s);  // OnEnter 훅이 presence.Track(id, &room) 을 자동 호출해야 한다
  EXPECT_EQ(presence.RoomCountFor(id), 1u)
      << "MatchRoom.Join 의 OnEnter 훅이 Presence 를 자동 Track 해야 한다";
  EXPECT_EQ(room.Count(), 1u);
}

// Leave → OnLeave → Presence.Untrack 자동 구동(역인덱스에서 빠진다).
TEST(MatchRoomLeaf, LeaveUntracksPresenceViaOnLeave)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);
  Presence presence;
  MatchRoom room(io, /*id=*/2, presence);

  auto s = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(s);
  const SessionId id = s->id();

  room.Join(s);
  ASSERT_EQ(presence.RoomCountFor(id), 1u);

  room.Leave(id);  // OnLeave 훅이 presence.Untrack(id, &room) 을 자동 호출해야 한다
  EXPECT_EQ(presence.RoomCountFor(id), 0u)
      << "MatchRoom.Leave 의 OnLeave 훅이 Presence 를 자동 Untrack 해야 한다";
  EXPECT_EQ(room.Count(), 0u);
}

// ── 사이클②: 다중 멤버십 기계장치 ──────────────────────────────────────────
//   한 세션이 동시에 두 잎 방(예: 매치 인스턴스 + 파티)에 소속. Presence 역인덱스가
//   세션당 여러 방을 독립적으로 든다는 불변식을 못박는다 — 이게 서면 disconnect sweep
//   이 "이 연결이 든 전 방을 일괄 Leave"할 대상이 실재한다(사이클③ 배선의 기반).

// 두 방 소속이 독립 — 한 방만 떠나도 다른 방 소속은 유지된다.
TEST(MatchRoomLeaf, SessionInTwoLeafRoomsTrackedIndependently)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);
  Presence presence;
  MatchRoom match(io, /*id=*/2, presence);  // 매치 인스턴스
  MatchRoom party(io, /*id=*/3, presence);  // 파티

  auto s = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(s);
  const SessionId id = s->id();

  match.Join(s);
  party.Join(s);
  EXPECT_EQ(presence.RoomCountFor(id), 2u)
      << "한 세션이 동시에 두 잎 방에 소속 — 역인덱스가 둘을 함께 든다";
  EXPECT_EQ(match.Count(), 1u);
  EXPECT_EQ(party.Count(), 1u);

  match.Leave(id);  // 한 방만 떠난다
  EXPECT_EQ(presence.RoomCountFor(id), 1u)
      << "한 방 Leave 후에도 다른 방 소속은 유지된다";
  EXPECT_EQ(match.Count(), 0u);
  EXPECT_EQ(party.Count(), 1u);
}

// disconnect sweep 이 다중 소속 세션의 전 방을 일괄 Leave → 마지막 강참조가 풀려 슬롯 반납.
TEST(MatchRoomLeaf, SweepLeavesAllRoomsAndReleasesSlot)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);
  Presence presence;
  MatchRoom match(io, /*id=*/2, presence);
  MatchRoom party(io, /*id=*/3, presence);

  auto s = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(s);
  const SessionId id = s->id();

  match.Join(s);
  party.Join(s);
  ASSERT_EQ(presence.RoomCountFor(id), 2u);

  s.reset();  // 외부 유일 참조를 놓아도 두 방 강참조가 슬롯을 붙든다
  EXPECT_EQ(pool->occupied(), 1u)
      << "두 방의 강참조가 풀 슬롯을 점유한다 — Leave 전까지 반납 불가(W-1)";

  presence.SweepOnDisconnect(id);
  io.poll();  // 각 방 strand 로 post 된 Leave 를 flush

  EXPECT_EQ(presence.RoomCountFor(id), 0u)
      << "sweep 후 역인덱스에서 세션이 사라진다";
  EXPECT_EQ(match.Count(), 0u) << "sweep 이 매치 방에서 Leave 시킨다";
  EXPECT_EQ(party.Count(), 0u) << "sweep 이 파티 방에서 Leave 시킨다";
  EXPECT_EQ(pool->occupied(), 0u)
      << "두 방 모두 놓아야 마지막 강참조가 풀려 슬롯이 반납된다(W-1 다중 확장)";
}
