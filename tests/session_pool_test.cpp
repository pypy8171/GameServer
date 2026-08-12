// SessionPool: 세션 고정상한 풀(ADR-B/W). SlotPool(순수 인덱스 할당기) 위에 세션
//   바이트 저장소 + placement-new + 풀반납 deleter 를 얹은 통합 계층. 급소:
//   (1) cap 개까지 획득·초과는 nullptr(예외 아님 — accept 거부 경로),
//   (2) shared_ptr refcount 0(=deleter)에서만 슬롯 반납 → 반납분 재획득,
//   (3) 획득 세션은 매번 fresh 구성(placement-new) — 인증=false·신규 id(재시딩 안전 씨앗),
//   (4) occupied 카운트 정확.
#include <gtest/gtest.h>

#include <asio.hpp>
#include <memory>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/net/session.h"
#include "core/net/session_pool.h"
#include "core/net/session_registry.h"

using namespace game::core;

namespace
{
// 소켓을 열지 않은(unopened) 세션을 풀에서 획득한다(IO 는 호출하지 않는다 —
//   풀 할당/반납/재구성 로직만 검증). Session 3-인자 생성자로 forward.
SessionPtr AcquireDummy(SessionPool& pool, asio::io_context& io, Dispatcher& d,
                        SessionRegistry& r)
{
  asio::ip::tcp::socket sock(io);
  return pool.Acquire(std::move(sock), d, r);
}
}  // namespace

// cap 개까지 획득하면 유효 세션이 나오고, 초과 획득은 nullptr(예외 아님).
TEST(SessionPool, AcquiresUpToCapacityThenNull)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(2);
  EXPECT_EQ(pool->capacity(), 2u);

  auto a = AcquireDummy(*pool, io, d, r);
  auto b = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(a && b);
  EXPECT_EQ(pool->occupied(), 2u);

  auto c = AcquireDummy(*pool, io, d, r);  // 상한 초과 — nullptr(백스톱 거부)
  EXPECT_FALSE(c);
  EXPECT_EQ(pool->occupied(), 2u);
}

// shared_ptr 를 놓으면(refcount 0 → deleter) 슬롯이 풀로 돌아와 재획득된다.
TEST(SessionPool, ReleasedSessionReturnsSlotForReuse)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);

  auto a = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(a);
  EXPECT_EQ(pool->occupied(), 1u);
  EXPECT_FALSE(AcquireDummy(*pool, io, d, r));  // cap=1 소진

  a.reset();  // refcount 0 → deleter 가 소멸+슬롯 반납
  EXPECT_EQ(pool->occupied(), 0u);

  auto b = AcquireDummy(*pool, io, d, r);  // 반납분 재획득
  ASSERT_TRUE(b);
  EXPECT_EQ(pool->occupied(), 1u);
}

// 획득한 세션들은 서로 다른 id 를 갖고 전부 미인증 상태다(placement-new 로 매번
//   Session 생성자가 실제로 돌았다는 증거 — 잔류 신원 없음).
TEST(SessionPool, AcquiredSessionsHaveDistinctFreshIdentities)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(2);

  auto a = AcquireDummy(*pool, io, d, r);
  auto b = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(a && b);
  EXPECT_NE(a->id(), b->id());
  EXPECT_FALSE(a->authenticated());
  EXPECT_FALSE(b->authenticated());
}

// 재사용된 슬롯은 이전 점유자의 신원을 물려받지 않는다(재시딩 씨앗 — ③의 축소판).
//   같은 슬롯을 강제 재사용(cap=1)해도 새 세션은 미인증·신규 id.
TEST(SessionPool, ReusedSlotYieldsFreshIdentity)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);

  auto a = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(a);
  const SessionId first_id = a->id();
  a.reset();  // 슬롯 반납

  auto b = AcquireDummy(*pool, io, d, r);  // 같은 슬롯 재사용 강제
  ASSERT_TRUE(b);
  EXPECT_NE(b->id(), first_id) << "재사용 슬롯은 신규 id 를 받아야 한다";
  EXPECT_FALSE(b->authenticated()) << "이전 점유자 인증이 잔류하면 안 된다";
}
