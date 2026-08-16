#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/net/session.h"  // SessionId, SessionPtr

namespace game::core
{

// 팬아웃 스코프 프리미티브(ADR-X 결정 A). 게임 무관 코어.
//
// Group = "이 프레임을 누구에게 뿌릴까"의 이름 붙은 수신자 집합. 계정·인증과 직교하며
//   (신원은 Session 에 산다, ADR-L), 한 세션은 동시에 여러 Group 에 속할 수 있다
//   (존채팅+길드+파티 겹침). SessionRegistry 의 전역 Broadcast 는 "전원이 멤버인 Group"
//   특수case 로, 이 프리미티브가 그 일반화다.
//
// 동시성 계약(ADR-A 재사용): 멤버맵은 mutex 로 보호. Broadcast 는 락 안에서 shared_ptr
//   '스냅샷'만 뜨고 락을 푼 뒤 순회한다(락 잡은 채 네트워크 경로 진입 금지 = 데드락 회피).
//   스냅샷이 shared_ptr 이라 순회 중 대상 종료돼도 객체는 살아있다(UAF 방지).
//
// 수명 계약: 멤버는 강참조(shared_ptr)로 잡는다 — SessionRegistry 와 동일 모델. 따라서
//   세션 종료 시 호출자가 Leave 를 불러줘야 한다(registry.Remove 와 대칭). 세션이 여러
//   Group 소속일 때의 일괄 Leave 배선은 결정 A 이후 통합 단계에서 다룬다.
class Group
{
 public:
  void Join(const SessionPtr& s);
  void Leave(SessionId id);  // 멱등: 없는 id 는 no-op

  // 순수 결정: 현재 멤버에서 except(발신자 등)를 뺀 수명안전 스냅샷.
  //   except==0 이면 전원. Broadcast 의 "누가 받는가" 판정을 I/O 와 분리해 단위 테스트.
  std::vector<SessionPtr> SnapshotRecipients(SessionId except = 0) const;

  // frame 을 멤버 중 except 를 제외한 전원에게 전송(copy-per-recipient, ADR-G).
  void Broadcast(const std::vector<uint8_t>& frame, SessionId except = 0);

  std::size_t Count() const;

 private:
  mutable std::mutex mu_;
  std::unordered_map<SessionId, SessionPtr> members_;
};

}  // namespace game::core
