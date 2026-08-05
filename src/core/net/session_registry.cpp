#include "core/net/session_registry.h"

#include <utility>

namespace game::core
{

void SessionRegistry::Add(const SessionPtr& s)
{
  std::lock_guard<std::mutex> lock(mu_);
  sessions_[s->id()] = s;
}

void SessionRegistry::Remove(SessionId id)
{
  std::lock_guard<std::mutex> lock(mu_);
  sessions_.erase(id);  // 없으면 no-op = 멱등
}

void SessionRegistry::Broadcast(const std::vector<uint8_t>& frame,
                                SessionId except)
{
  // 1) 락 안에서 스냅샷만 뜬다(shared_ptr copy = 수명 확보).
  std::vector<SessionPtr> snapshot;
  {
    std::lock_guard<std::mutex> lock(mu_);
    snapshot.reserve(sessions_.size());
    for (const auto& [id, s] : sessions_)
    {
      if (id != except)
      {
        snapshot.push_back(s);
      }
    }
  }
  // 2) 락을 푼 뒤 순회. Send 는 대상 strand 로 post + 프레임을 세션별로 복사한다.
  for (const auto& s : snapshot)
  {
    s->Send(frame);  // copy-per-recipient (ADR-G)
  }
}

std::size_t SessionRegistry::Count() const
{
  std::lock_guard<std::mutex> lock(mu_);
  return sessions_.size();
}

}  // namespace game::core
