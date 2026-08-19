#include "core/net/room.h"

namespace game::core
{

Room::Room(asio::io_context& io, RoomId id)
    : strand_(asio::make_strand(io.get_executor())), id_(id)
{
}

void Room::Join(const SessionPtr& s)
{
  members_.Join(s);
  OnEnter(s->id());
}

void Room::Leave(SessionId member)
{
  // 실제 멤버였을 때만 OnLeave 발화(헛발화 차단) — Group::Leave 가 제거 여부를 반환.
  if (members_.Leave(member))
  {
    OnLeave(member);
  }
}

void Room::PostLeave(SessionId member)
{
  asio::post(strand_, [this, member] { Leave(member); });
}

void Room::Broadcast(const std::vector<uint8_t>& frame, SessionId except)
{
  members_.Broadcast(frame, except);
}

}  // namespace game::core
