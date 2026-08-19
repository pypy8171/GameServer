// PresenceDisconnectSweep — ADR-X 결정 B(Room) W-1 배선(안 b) 불변식 못박기.
//
//   Room 은 멤버 Group 을 통해 세션을 **강참조**로 붙든다(room.h:26-29, group.h:26-28)
//   → Join 된 세션은 외부 shared_ptr 가 사라져도 방이 refcount 를 붙들어 풀 슬롯을 영구
//   점유한다. 코어 Session 은 자신이 어느 방에 속하는지 모르므로(Group ⊥ 신원), disconnect
//   시 소속 전 방을 일괄 Leave 시키는 배선은 게임층 Presence(SessionId→Room* 역인덱스)가
//   맡는다. Presence::SweepOnDisconnect 가 그 배선 — 이게 없거나 방을 실제로 안 떠나면
//   registry 보다 나쁜 영구 슬롯 누수가 된다(W-1). 이 테스트는 "sweep 이 방을 떠나
//   마지막 강참조를 놓아야 슬롯이 반납된다"를 결정적으로 못박는다.
//
//   sweep 은 각 방 strand 로 교차-strand post(ADR-O) → io.poll() 로 실행을 편치(flush)해야
//   반납이 관측된다.
#include <gtest/gtest.h>

#include <asio.hpp>
#include <memory>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/net/room.h"
#include "core/net/session.h"
#include "core/net/session_pool.h"
#include "core/net/session_registry.h"
#include "game_logic/presence/presence.h"

using namespace game::core;
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

// disconnect sweep 이 소속 방을 일괄 Leave 시켜 풀 슬롯을 반납한다(W-1 안 b).
TEST(PresenceDisconnectSweep, RoomSlotReleasedAfterSweep)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);
  Room room(io, /*id=*/1);
  Presence presence;

  auto s = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(s);
  const SessionId id = s->id();

  room.Join(s);            // 방이 강참조로 붙듦
  presence.Track(id, &room);  // 역인덱스에 소속 기록
  EXPECT_EQ(presence.RoomCountFor(id), 1u);
  EXPECT_EQ(room.Count(), 1u);

  // 외부 유일 참조를 놓아도 방이 붙들어 슬롯이 안 풀린다.
  s.reset();
  EXPECT_EQ(pool->occupied(), 1u)
      << "Room 강참조가 풀 슬롯을 점유한다 — Leave 전까지 반납 불가(W-1)";

  // disconnect 훅: 이 세션이 든 전 방을 sweep. 교차-strand post 라 poll 로 flush.
  presence.SweepOnDisconnect(id);
  io.poll();

  EXPECT_EQ(presence.RoomCountFor(id), 0u)
      << "sweep 후 역인덱스에서 세션이 사라져야 한다";
  EXPECT_EQ(room.Count(), 0u)
      << "sweep 이 방에서 세션을 Leave 시켜야 한다";
  EXPECT_EQ(pool->occupied(), 0u)
      << "sweep 이 마지막 강참조를 놓아 슬롯이 반납된다(W-1 배선)";

  // 반납분이 재획득되는지로 슬롯이 진짜 풀렸음을 확정.
  auto reuse = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(reuse);
  EXPECT_EQ(pool->occupied(), 1u);
}
