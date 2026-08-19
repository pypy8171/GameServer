#include "game_logic/presence/presence.h"

#include <algorithm>
#include <utility>

namespace game::logic
{

void Presence::Track(game::core::SessionId sid, game::core::Room* room)
{
  std::lock_guard<std::mutex> lock(mu_);
  auto& rooms = memberships_[sid];
  if (std::find(rooms.begin(), rooms.end(), room) == rooms.end())
  {
    rooms.push_back(room);  // 같은 (sid,room) 중복 방지 = 멱등
  }
}

void Presence::Untrack(game::core::SessionId sid, game::core::Room* room)
{
  std::lock_guard<std::mutex> lock(mu_);
  auto it = memberships_.find(sid);
  if (it == memberships_.end())
  {
    return;  // 없으면 no-op(멱등)
  }
  auto& rooms = it->second;
  rooms.erase(std::remove(rooms.begin(), rooms.end(), room), rooms.end());
  if (rooms.empty())
  {
    memberships_.erase(it);
  }
}

void Presence::SweepOnDisconnect(game::core::SessionId sid)
{
  // 락 안에서 소속 방 스냅샷만 뜨고 즉시 항목 삭제 — 락 밖에서 각 방 strand 로 post.
  std::vector<game::core::Room*> rooms;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = memberships_.find(sid);
    if (it == memberships_.end())
    {
      return;
    }
    rooms = std::move(it->second);
    memberships_.erase(it);
  }

  // 락 밖에서 각 방 strand 로 Leave 를 post(ADR-O: 교차-strand = 락 아닌 핸드오프).
  //   방이 세션을 놓아야(Group::Leave) 마지막 강참조가 풀려 풀 슬롯이 반납된다(W-1).
  for (game::core::Room* room : rooms)
  {
    room->PostLeave(sid);
  }
}

std::size_t Presence::RoomCountFor(game::core::SessionId sid) const
{
  std::lock_guard<std::mutex> lock(mu_);
  auto it = memberships_.find(sid);
  return it == memberships_.end() ? 0 : it->second.size();
}

}  // namespace game::logic
