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

World::World(asio::io_context& io)
    : strand_(asio::make_strand(io.get_executor()))
{
}

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
  asio::post(strand_, [this, s, sid, pid] {
    const auto spawn = Enter(sid, pid);
    if (!spawn)
    {
      // 현재 도달 불가(재로그인은 login 단계에서 차단, SessionId 재사용 없음).
      //   방어적 관측: 여기 오면 스폰 미송신으로 클라가 무한 대기하므로 남긴다(ADR-G).
      LOG_WARN("[world] 중복 입장 무시 (id={}, player_id={}) — 스폰 미송신", sid, pid);
      return;
    }
    s->Send(MakePacket(PacketId::WorldEnteredNotify,
                       MakeWorldEnteredNotify(*spawn)));
  });
}

void World::PostLeave(SessionId sid)
{
  asio::post(strand_, [this, sid] { Leave(sid); });
}

void World::PostMove(SessionId sid, float x, float y, BroadcastSink broadcast)
{
  asio::post(strand_, [this, sid, x, y, broadcast = std::move(broadcast)] {
    const auto moved = Move(sid, x, y);
    if (!moved)
    {
      // 미입장(또는 이미 퇴장)한 세션의 이동 — 팬아웃 없이 드롭.
      //   도달 경로상 인증세션만 오지만(게이트 밖 아님), 입장 전/퇴장 직후 레이스로
      //   여기 올 수 있다. 관측만 남기고 조용히 무시(ADR-G).
      LOG_WARN("[world] 미입장 세션 이동 무시 (id={})", sid);
      return;
    }
    // MoveNotify 는 이동한 본인 포함 전원에 팬아웃한다(except=0). player_id 는
    //   서버가 세션 신원에서 채운 값 — 클라 주장 아님(스푸핑 차단).
    broadcast(MakePacket(PacketId::MoveNotify, MakeMoveNotify(*moved)));
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
