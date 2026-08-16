#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/net/group.h"    // Group (전역 팬아웃 위임)
#include "core/net/session.h"  // SessionId, SessionPtr

namespace game::core
{

// 살아있는 세션 집합 + 전역 팬아웃. 게임 무관 코어 프리미티브.
//
// ADR-X 결정 A: **전역 Broadcast 는 "전원이 멤버인 Group" 특수case 다.** 멤버십·팬아웃·
//   동시성 계약(ADR-A: 락 안 shared_ptr 스냅샷 → 락 밖 순회 Send / ADR-G: copy-per-
//   recipient)은 전부 내부 all-members `Group` 에 위임한다. SessionRegistry 는 그 Group 을
//   전역 스코프로 감싸는 얇은 어댑터로 남는다(Count = 라이브 게이트 겸 관측 메트릭, ADR-W/T).
class SessionRegistry
{
 public:
  void Add(const SessionPtr& s);
  void Remove(SessionId id);  // 멱등: 없는 id 는 no-op

  // frame([헤더+페이로드])을 현재 모든 세션에 전송한다.
  // except 와 같은 id 인 세션은 건너뛴다(0 이면 전원).
  void Broadcast(const std::vector<uint8_t>& frame, SessionId except = 0);

  std::size_t Count() const;

 private:
  Group all_;  // 전원 멤버 그룹 = 전역 브로드캐스트 스코프
};

}  // namespace game::core
