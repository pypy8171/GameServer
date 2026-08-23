// WorldRoomMembership — ADR-X 결정 B: World has-a Room 배선 불변식.
//
//   결정 B 로 World 는 코어 `Room` 을 포함하고, 입장 시 세션을 방 멤버로 Join 한다
//   (world.cpp PostEnter). Room 은 멤버를 강참조로 붙들어(room.h/group.h) 풀 슬롯을
//   점유하므로(W-1), 퇴장(`PostLeave`)이 엔티티 제거뿐 아니라 **방 멤버 해제**까지
//   해야 마지막 강참조가 풀려 슬롯이 반납된다. 이 테스트는 "PostLeave = 엔티티 Leave
//   + 방 멤버 Leave" 를 못박는다 — room_.Leave 를 빠뜨리면(구 동작) occupied 가 1 에
//   머물러 되살아나 문다.
//
//   Send(소켓) 를 피하려고 PostEnter 대신 방 Join + 순수 Enter 로 "입장 상태"를 직접
//   구성한다(socket 통합은 game_entry_test 몫). PostLeave 는 world strand 로 post 하므로
//   io.poll() 로 flush 한다.
#include <gtest/gtest.h>

#include <asio.hpp>
#include <memory>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/net/session.h"
#include "core/net/session_pool.h"
#include "core/net/session_registry.h"
#include "game_logic/world/world.h"

using namespace game::core;
using game::logic::World;

namespace
{
SessionPtr AcquireDummy(SessionPool& pool, asio::io_context& io, Dispatcher& d,
                        SessionRegistry& r)
{
  asio::ip::tcp::socket sock(io);
  return pool.Acquire(std::move(sock), d, r);
}
}  // namespace

// World::PostLeave 가 엔티티 제거 + 방 멤버 해제로 풀 슬롯을 반납한다(W-1 배선).
TEST(WorldRoomMembership, PostLeaveReleasesRoomSlot)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);
  World world(io);

  auto s = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(s);
  const SessionId id = s->id();

  // 입장 상태 구성(Send 회피): 방 멤버로 Join + 엔티티 삽입(순수 Enter).
  world.room().Join(s);
  ASSERT_TRUE(world.Enter(id, /*pid=*/7).has_value());
  EXPECT_EQ(world.room().Count(), 1u);
  EXPECT_EQ(world.Count(), 1u);

  // 외부 유일 참조를 놓아도 방이 강참조로 붙들어 슬롯이 안 풀린다.
  s.reset();
  EXPECT_EQ(pool->occupied(), 1u)
      << "방 멤버 Group 강참조가 풀 슬롯을 점유한다(W-1)";

  // 퇴장: world strand 로 post → poll 로 flush.
  world.PostLeave(id);
  io.poll();

  EXPECT_EQ(world.Count(), 0u) << "엔티티가 제거돼야 한다";
  EXPECT_EQ(world.room().Count(), 0u)
      << "PostLeave 가 방 멤버까지 해제해야 한다(엔티티만 지우면 방이 붙든 채 누수)";
  EXPECT_EQ(pool->occupied(), 0u)
      << "방 멤버 해제로 마지막 강참조가 풀려 슬롯이 반납된다(W-1)";

  // 반납분이 재획득되는지로 슬롯이 진짜 풀렸음을 확정.
  auto reuse = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(reuse);
  EXPECT_EQ(pool->occupied(), 1u);
}
