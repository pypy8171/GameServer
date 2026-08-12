#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "core/net/session.h"
#include "core/net/slot_pool.h"

namespace game::core
{

// 고정 상한 세션 풀(ADR-B/W) — 순수 인덱스 할당기 SlotPool 위에 세션 **바이트
//   저장소 + placement-new + 풀반납 deleter** 를 얹은 통합 계층. new/delete churn 과
//   단편화를 제거하고(연결 폭주/이탈 반복에서도 힙을 안 흔든다), 동접 상한을 풀 용량으로
//   구조화한다. 코어 초기 확정(ADR-F), 구현은 이 마일스톤(M3).
//
//   급소 세 결정(ADR-B, 위험을 구조적으로 소멸):
//   (1) **슬롯 반납은 오직 shared_ptr deleter(refcount 0)에서만** — refcount 0 은 모든
//       async op(타이머 2·read·write·post) 완료(quiescence) 이후라, 이전 점유자의 타이머가
//       새 점유자 위에서 발화하는 일이 원천 불가. Close() 는 절대 슬롯을 반납하지 않는다.
//   (2) **획득 시 완전 재구성(placement-new)** — 매번 fresh 소켓/strand/id 로 Session 을
//       새로 짓는다. reset() 은 enable_shared_from_this 의 __weak_this 가 옛 컨트롤블록을
//       가리켜 UB 라 기각. fresh 구성이라 인증/신원 필드가 자동으로 초기 상태 → 재시딩 안전.
//   (3) **풀 수명 봉인** — Session 몸통은 풀의 storage_ 안에 산다. 그래서 풀이 살아있는
//       세션보다 먼저 죽으면 ~Session() 조차 해제된 메모리를 만진다(weak_ptr 로도 못 막음 —
//       소멸자가 이미 풀 저장소를 참조). 따라서 각 세션의 deleter 가 풀 **강참조**를 쥔다
//       → 풀은 마지막 세션이 죽는 순간까지 반드시 산다. 그 때문에 stack 이 아니라
//       shared_ptr 로만 만든다(Create 팩토리 + enable_shared_from_this).
//
//   상한 도달 시 Acquire 는 **예외가 아니라 nullptr**(ADR-B 기법 B): accept 게이트가
//   예외 없이 거부 경로로 흐른다. 라이브 게이트(registry.Count()<max_sessions)가 1차
//   방어고 이 nullptr 은 방어 백스톱이다(ADR-W). MPMC 접근 안전성은 SlotPool 의 mutex.
class SessionPool : public std::enable_shared_from_this<SessionPool>
{
 public:
  // 풀은 shared_ptr 로만 존재한다(수명 봉인 — 위 급소 (3)). 스택 생성 금지.
  static std::shared_ptr<SessionPool> Create(std::size_t capacity)
  {
    return std::shared_ptr<SessionPool>(new SessionPool(capacity));
  }

  // 세션 1개를 풀에서 획득한다. Session 생성자 인자를 그대로 forward(소켓·dispatcher·
  //   registry·정책 등) — 풀은 Session 의 생성자 형태를 몰라도 된다. 자유 슬롯이 없으면
  //   nullptr(상한 도달). 반환된 shared_ptr 의 deleter 가 refcount 0 에서 소멸자 실행 +
  //   슬롯 반납을 수행한다(ADR-B 불변식의 유일한 반납 지점)이며, 풀 강참조를 쥐어 풀
  //   수명을 세션 수명 뒤로 늘린다.
  template <class... Args>
  SessionPtr Acquire(Args&&... args)
  {
    const auto idx = slots_.TryAcquire();
    if (!idx)
    {
      return nullptr;  // 상한 도달 — 방어 백스톱 거부(예외 아님)
    }
    Session* p = nullptr;
    try
    {
      // placement-new 식 반환값을 그대로 객체 포인터로 쓴다(reinterpret_cast/launder
      //   불필요 — 객체 provenance 가 명확). fresh 재구성이라 신원 필드 자동 초기화.
      p = ::new (storage_[*idx].bytes) Session(std::forward<Args>(args)...);
    }
    catch (...)
    {
      slots_.Release(*idx);  // 구성 실패 시 슬롯 누수 방지(획득만 하고 반납 못 함 차단)
      throw;
    }
    // 기법 B: 3-인자 shared_ptr(포인터+deleter). 컨트롤블록만 힙, Session 몸통은 풀.
    //   deleter 는 refcount 0 에서 1회 발화 → 소멸자 + 슬롯 반납(유일 반납 지점).
    //   self(풀 강참조) 캡처로 풀이 이 세션보다 먼저 죽지 않음을 보장(수명 봉인).
    return SessionPtr(p, [self = shared_from_this(), i = *idx](Session* q) {
      q->~Session();
      self->slots_.Release(i);
    });
  }

  std::size_t capacity() const { return slots_.capacity(); }
  std::size_t occupied() const { return slots_.occupied(); }

 private:
  explicit SessionPool(std::size_t capacity)
      : slots_(capacity), storage_(capacity)
  {
  }

  // Session 1개를 담을 정렬된 원시 바이트 셀. placement-new 대상이라 생성자를 돌리지
  //   않는다(획득 시 비로소 구성). alignas 로 Session 정렬 요건을 만족시킨다.
  struct alignas(Session) Slot
  {
    std::byte bytes[sizeof(Session)];
  };

  SlotPool slots_;              // 자유목록(순수 인덱스 할당기, mutex 내장)
  std::vector<Slot> storage_;   // 슬롯 원시 저장소 [capacity]
};

}  // namespace game::core
