#include "game_logic/world/world.h"

#include <cmath>

#include "core/log/log.h"
#include "core/net/session.h"
#include "core/packet/packet.h"

namespace game::logic
{

using game::core::MakePacket;
using game::core::PacketId;
using game::core::SessionId;
using game::core::SessionPtr;

World::World(asio::io_context& io) : room_(io, kWorldRoomId) {}

std::optional<PlayerEntity> World::Enter(SessionId sid, PlayerId pid)
{
  // 스폰 정책(MG 최소): 원점 (0,0). 위치는 이후 MoveRequest 로 변경(MG-⑦).
  const auto [it, inserted] = players_.try_emplace(sid, PlayerEntity{pid});
  if (!inserted)
  {
    return std::nullopt;  // 이미 입장한 세션 — 중복 가드(기존 신원 덮어쓰기 금지)
  }
  return it->second;
}

bool World::Leave(SessionId sid)
{
  return players_.erase(sid) > 0;
}

std::size_t World::Count() const
{
  return players_.size();
}

const PlayerEntity* World::Find(SessionId sid) const
{
  const auto it = players_.find(sid);
  return it == players_.end() ? nullptr : &it->second;
}

std::optional<PlayerEntity> World::Move(SessionId sid, float x, float y)
{
  if (!std::isfinite(x) || !std::isfinite(y))
  {
    // NaN/Inf 좌표 거부(상태 불변). 클라가 실은 비유한 비트패턴을 저장하면
    //   MoveNotify 로 전원에 팬아웃돼 정상 클라 상태를 오염시킨다 — 변이 전 봉인.
    //   (이동거리/속도 상한 anti-cheat 는 게임설계 결정, 별도 마일스톤 seam.)
    return std::nullopt;
  }
  const auto it = players_.find(sid);
  if (it == players_.end())
  {
    return std::nullopt;  // 미입장/이미 퇴장한 세션 — 유령 이동 가드(상태 불변)
  }
  it->second.x = x;
  it->second.y = y;
  return it->second;
}

void World::PostEnter(const SessionPtr& s, PlayerId pid)
{
  const SessionId sid = s->id();
  asio::post(room_.strand(), [this, s, sid, pid] {
    const auto spawn = Enter(sid, pid);
    if (!spawn)
    {
      // 현재 도달 불가(재로그인은 login 단계에서 차단, SessionId 재사용 없음).
      //   방어적 관측: 여기 오면 스폰 미송신으로 클라가 무한 대기하므로 남긴다(ADR-G).
      LOG_WARN("[world] 중복 입장 무시 (id={}, player_id={}) — 스폰 미송신", sid, pid);
      return;
    }
    // 엔티티 삽입 성공 시에만 방 멤버로 Join — 이후 MoveNotify 등 월드 팬아웃 대상이
    //   되고, Room 이 세션을 강참조로 붙든다(W-1: PostLeave 가 해제 책임).
    room_.Join(s);
    s->Send(MakePacket(PacketId::WorldEnteredNotify,
                       MakeWorldEnteredNotify(*spawn)));
  });
}

void World::PostLeave(SessionId sid)
{
  // 엔티티 제거 + 방 멤버 해제를 같은 strand 에서 함께 — Leave(엔티티)는 상태 정리,
  //   room_.Leave 는 마지막 강참조를 놓아 풀 슬롯을 반납한다(W-1). 입장 때 Join 이
  //   먼저 post 됐으므로(FIFO) 여기 Leave 가 항상 그 뒤에 돈다(누수 없음).
  asio::post(room_.strand(), [this, sid] {
    Leave(sid);
    room_.Leave(sid);
  });
}

void World::PostMove(SessionId sid, float x, float y)
{
  asio::post(room_.strand(), [this, sid, x, y] {
    const auto moved = Move(sid, x, y);
    if (!moved)
    {
      // 미입장(또는 이미 퇴장)한 세션의 이동 — 팬아웃 없이 드롭.
      //   도달 경로상 인증세션만 오지만(게이트 밖 아님), 입장 전/퇴장 직후 레이스로
      //   여기 올 수 있다. 관측만 남기고 조용히 무시(ADR-G).
      LOG_WARN("[world] 미입장 세션 이동 무시 (id={})", sid);
      return;
    }
    // MoveNotify 는 이동한 본인 포함 방 멤버 전원에 팬아웃(except=0). player_id 는
    //   서버가 세션 신원에서 채운 값 — 클라 주장 아님(스푸핑 차단). 팬아웃이 코어
    //   Room 의 멤버 Group 을 통과하므로 입장한 플레이어에게만 간다(미인증 연결 제외).
    room_.Broadcast(MakePacket(PacketId::MoveNotify, MakeMoveNotify(*moved)));
  });
}

game::proto::WorldEnteredNotify MakeWorldEnteredNotify(const PlayerEntity& e)
{
  game::proto::WorldEnteredNotify n;
  n.set_player_id(e.id);
  n.set_x(e.x);
  n.set_y(e.y);
  return n;
}

game::proto::MoveNotify MakeMoveNotify(const PlayerEntity& e)
{
  game::proto::MoveNotify n;
  n.set_player_id(e.id);
  n.set_x(e.x);
  n.set_y(e.y);
  return n;
}

}  // namespace game::logic
