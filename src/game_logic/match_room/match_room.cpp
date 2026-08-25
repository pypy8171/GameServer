#include "game_logic/match_room/match_room.h"

namespace game::logic
{

MatchRoom::MatchRoom(asio::io_context& io, game::core::RoomId id,
                     Presence& presence)
    : game::core::Room(io, id), presence_(presence)
{
}

// 훅은 Room 파사드(Join/Leave)가 방 strand 위에서 발화 — Track/Untrack 을 그 strand
//   위에서 구동한다(Presence 자체 mutex 가 다른 방 strand 와의 동시 접근도 보호).
//   this = 이 방 포인터 → 역인덱스가 (세션 → 이 방) 소속을 기록/삭제한다.
void MatchRoom::OnEnter(game::core::SessionId member)
{
  presence_.Track(member, this);
}

void MatchRoom::OnLeave(game::core::SessionId member)
{
  presence_.Untrack(member, this);
}

}  // namespace game::logic
