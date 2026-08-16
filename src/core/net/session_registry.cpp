#include "core/net/session_registry.h"

namespace game::core
{

// 전 메서드를 내부 all-members Group 에 위임한다(ADR-X 결정 A). 멤버십·스냅샷·락 규율은
//   Group 이 소유하므로 여기엔 추가 상태/락이 없다(이중 락·이중 진실원 회피).

void SessionRegistry::Add(const SessionPtr& s)
{
  all_.Join(s);
}

void SessionRegistry::Remove(SessionId id)
{
  all_.Leave(id);
}

void SessionRegistry::Broadcast(const std::vector<uint8_t>& frame, SessionId except)
{
  all_.Broadcast(frame, except);
}

std::size_t SessionRegistry::Count() const
{
  return all_.Count();
}

}  // namespace game::core
