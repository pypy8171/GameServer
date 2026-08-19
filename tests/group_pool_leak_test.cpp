// GroupSlotLeak — ADR-X 결정 A 마감 회귀가드 + W-1(@reviewer) 불변식 못박기.
//
//   ADR-W 풀 회계의 급소: 슬롯 반납은 오직 shared_ptr refcount 0(=deleter)에서만
//   일어난다(session_pool.h 급소 (1)). Group 은 멤버를 **강참조**(shared_ptr)로 잡으므로
//   (group.h:26-28), Group 에 Join 된 세션은 외부 shared_ptr 가 사라져도 Group 이
//   refcount 를 붙들어 **슬롯을 계속 점유**한다. draining tail(in-flight async_write 가
//   잠시 붙드는 일시적 점유)과 달리 이 점유는 **Leave 전까지 영구**다.
//
//   전역 all-members Group(SessionRegistry 내부)은 Close()→registry_.Remove()→all_.Leave()
//   로 이 반납이 배선돼 있어 누수하지 않는다(session.cpp). 하지만 결정 B(Room)에서 태어날
//   2번째 이후 Group(파티·길드·존 멤버그룹)은 disconnect 시 일괄 Leave 배선이 없으면
//   registry 보다 나쁜 영구 누수가 된다(W-1). 이 테스트는 "Group 강참조 = 슬롯 점유,
//   Leave 만이 반납"을 결정적으로 못박아, B 에서 leave-on-disconnect 배선을 빠뜨리면
//   그 불변식이 되살아나 물게 한다.
#include <gtest/gtest.h>

#include <asio.hpp>
#include <memory>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/net/group.h"
#include "core/net/session.h"
#include "core/net/session_pool.h"
#include "core/net/session_registry.h"

using namespace game::core;

namespace
{
// 소켓 미개방 세션을 풀에서 획득(IO 미탑 — 풀 회계와 Group 강참조 수명만 검증).
SessionPtr AcquireDummy(SessionPool& pool, asio::io_context& io, Dispatcher& d,
                        SessionRegistry& r)
{
  asio::ip::tcp::socket sock(io);
  return pool.Acquire(std::move(sock), d, r);
}
}  // namespace

// Group 강참조가 풀 슬롯을 붙든다 — 외부 shared_ptr 를 놓아도 Leave 전까지 반납 불가(W-1).
TEST(GroupSlotLeak, StrongRefPinsPoolSlotUntilLeave)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);
  Group g;

  auto s = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(s);
  EXPECT_EQ(pool->occupied(), 1u);

  g.Join(s);  // Group 이 강참조로 붙듦
  const SessionId id = s->id();

  // 외부에서 유일 참조를 놓아도 Group 이 refcount 를 붙들어 슬롯이 안 풀린다.
  s.reset();
  EXPECT_EQ(pool->occupied(), 1u)
      << "Group 강참조가 풀 슬롯을 점유한다 — Leave 전까지 반납 불가(W-1)";
  // 슬롯이 실제로 안 풀렸다는 증거: cap=1 인데 새 획득이 백스톱 거부(nullptr).
  EXPECT_FALSE(AcquireDummy(*pool, io, d, r))
      << "슬롯이 여전히 점유 중이면 cap=1 재획득은 거부돼야 한다";

  // Leave 가 마지막 강참조를 놓는다 → refcount 0 → deleter → 소멸 + 슬롯 반납.
  g.Leave(id);
  EXPECT_EQ(pool->occupied(), 0u)
      << "Leave 가 마지막 강참조를 놓아 슬롯이 반납된다";

  // 반납분이 재획득되는지로 슬롯이 진짜 풀렸음을 확정.
  auto reuse = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(reuse);
  EXPECT_EQ(pool->occupied(), 1u);
}
