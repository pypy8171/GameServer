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

// ③ 재시딩 보안 회귀(ADR-B 필수 동반) — 축소판(위)과 달리 이전 점유자를 **실제로
//   오염**시킨다: 인증을 확립하고 불투명 신원(principal)을 심은 뒤 슬롯을 반납한다.
//   그런 다음 cap=1 로 같은 슬롯을 강제 재사용해, 새 점유자가 이전의 확립된 신원을
//   한 조각도 물려받지 않음을 못박는다. 이 잔류가 곧 **권한 오염**(인증 세션의 계정
//   신원이 다음 연결로 새는 것)이라 M3 풀의 보안 불변식이다.
//
//   왜 authenticated_/principal_ 만 오염하는가: 이 둘은 IO 없이(dispatch 경로 없이)
//   직접 확립 가능한 유일한 신원 필드다. send_queue_·recv_buf_·closed_ 는 살아있는
//   소켓/strand 실행(io.run())이 있어야 오염되며 순수 유닛에서 결정적으로 심을 수
//   없다. 하지만 풀은 **placement-new 로 매 획득마다 Session 을 통째로 재구성**한다
//   (reset() 후 재사용이 아니라 생성자 1회 전면 실행) — 한 생성자가 모든 필드를 초기
//   상태로 되돌리므로 "일부만 리셋"이 구조적으로 불가능하다. 따라서 신원 필드가
//   깨끗하다는 것 = 같은 생성자가 나머지 필드도 초기화했다는 증거다. 만약 누군가 풀을
//   "객체 재사용(reset)"으로 바꿔 이 불변식을 깨면, 오염된 principal 이 잔류해 이
//   테스트가 즉시 실패한다(RED 의 이빨).
//   전제: default SessionPolicy(모든 타이머 0=비활성). 이때 Authenticate 가 부르는
//   ArmIdleDeadline/ArmHeartbeat 는 조기 리턴이라 self 를 쥔 async_wait 가 안 걸린다 →
//   dirty.reset() 이 곧장 refcount 0 에 도달해 deleter 가 슬롯을 반납한다. 만약 default
//   정책이 바뀌어 타이머가 무장되면 pending 콜백이 self 를 붙들어 슬롯이 안 풀리고,
//   cap=1 이라 아래 fresh 획득이 nullptr → ASSERT_TRUE(fresh) 가 (원인과 동떨어진 곳에서)
//   깨진다. 그 경우 정책이 아니라 이 전제를 의심하라.
TEST(SessionPool, ReusedSlotDoesNotLeakAuthenticatedIdentity)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  auto pool = SessionPool::Create(1);

  // 1) 슬롯을 확립된 신원으로 오염시킨다.
  auto dirty = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(dirty);
  const SessionId dirty_id = dirty->id();
  const Session* dirty_addr = dirty.get();  // 재사용 슬롯 동일성 검증용 raw 저장소 주소
  ASSERT_TRUE(dirty->Authenticate("acct-secret-777"));
  ASSERT_TRUE(dirty->authenticated()) << "오염 전제: 인증이 확립돼 있어야 한다";
  ASSERT_EQ(dirty->principal(), "acct-secret-777");
  dirty.reset();  // refcount 0 → deleter 가 소멸 + 슬롯 반납

  // 2) 같은 슬롯을 강제 재사용(cap=1). 주소가 같아야 "같은 바이트에 재구성"이 확정된다
  //    — 우연히 다른 깨끗한 슬롯을 받아 통과하는 거짓 GREEN 을 배제(cap=1 이라 구조적
  //    으로 slot 0 강제지만, 이 등가성을 테스트에 못박아 재시딩 경로를 명시한다).
  auto fresh = AcquireDummy(*pool, io, d, r);
  ASSERT_TRUE(fresh);
  ASSERT_EQ(fresh.get(), dirty_addr) << "cap=1 은 같은 슬롯 저장소를 재사용해야 한다";

  // 3) 이전 점유자의 확립된 신원이 한 조각도 새면 안 된다.
  EXPECT_FALSE(fresh->authenticated())
      << "재사용 슬롯이 이전 세션의 인증을 물려받았다 — 권한 오염";
  EXPECT_TRUE(fresh->principal().empty())
      << "재사용 슬롯에 이전 principal 이 잔류했다: '" << fresh->principal() << "'";
  EXPECT_NE(fresh->id(), dirty_id) << "재사용 슬롯은 신규 id 를 받아야 한다";
}
