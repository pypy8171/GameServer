#include "core/net/group.h"

#include <utility>

namespace game::core
{

void Group::Join(const SessionPtr& s)
{
  std::lock_guard<std::mutex> lock(mu_);
  members_[s->id()] = s;  // 같은 id 재가입 = 덮어쓰기(여전히 1)
}

void Group::Leave(SessionId id)
{
  std::lock_guard<std::mutex> lock(mu_);
  members_.erase(id);  // 없으면 no-op = 멱등
}

std::vector<SessionPtr> Group::SnapshotRecipients(SessionId except) const
{
  // 락 안에서 shared_ptr 스냅샷만 뜬다(수명 확보) — 네트워크 경로엔 락 밖에서 진입.
  std::vector<SessionPtr> snapshot;
  {
    std::lock_guard<std::mutex> lock(mu_);
    snapshot.reserve(members_.size());
    for (const auto& [id, s] : members_)
    {
      if (id != except)
      {
        snapshot.push_back(s);
      }
    }
  }
  return snapshot;
}

void Group::Broadcast(const std::vector<uint8_t>& frame, SessionId except)
{
  // 결정(누가 받는가)은 SnapshotRecipients 로 분리 — 여기선 얇게 순회 Send 만.
  for (const auto& s : SnapshotRecipients(except))
  {
    s->Send(frame);  // copy-per-recipient (ADR-G)
  }
}

std::size_t Group::Count() const
{
  std::lock_guard<std::mutex> lock(mu_);
  return members_.size();
}

}  // namespace game::core
