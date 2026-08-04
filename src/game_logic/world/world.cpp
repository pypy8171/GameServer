#include "game_logic/world/world.h"

#include "core/log/log.h"
#include "core/net/session.h"
#include "core/packet/packet.h"

namespace game::logic {

using game::core::MakePacket;
using game::core::PacketId;
using game::core::SessionId;
using game::core::SessionPtr;

World::World(asio::io_context& io)
    : strand_(asio::make_strand(io.get_executor())) {}

std::optional<PlayerEntity> World::Enter(SessionId sid, PlayerId pid) {
  // 스폰 정책(MG 최소): 원점 (0,0). 위치는 이후 MoveRequest 로 변경(MG-⑦).
  const auto [it, inserted] = players_.try_emplace(sid, PlayerEntity{pid});
  if (!inserted) {
    return std::nullopt;  // 이미 입장한 세션 — 중복 가드(기존 신원 덮어쓰기 금지)
  }
  return it->second;
}

bool World::Leave(SessionId sid) { return players_.erase(sid) > 0; }

std::size_t World::Count() const { return players_.size(); }

const PlayerEntity* World::Find(SessionId sid) const {
  const auto it = players_.find(sid);
  return it == players_.end() ? nullptr : &it->second;
}

void World::PostEnter(const SessionPtr& s, PlayerId pid) {
  const SessionId sid = s->id();
  asio::post(strand_, [this, s, sid, pid] {
    const auto spawn = Enter(sid, pid);
    if (!spawn) {
      // 현재 도달 불가(재로그인은 login 단계에서 차단, SessionId 재사용 없음).
      //   방어적 관측: 여기 오면 스폰 미송신으로 클라가 무한 대기하므로 남긴다(ADR-G).
      LOG_WARN("[world] 중복 입장 무시 (id={}, player_id={}) — 스폰 미송신", sid, pid);
      return;
    }
    s->Send(MakePacket(PacketId::WorldEnteredNotify,
                       MakeWorldEnteredNotify(*spawn)));
  });
}

void World::PostLeave(SessionId sid) {
  asio::post(strand_, [this, sid] { Leave(sid); });
}

game::proto::WorldEnteredNotify MakeWorldEnteredNotify(const PlayerEntity& e) {
  game::proto::WorldEnteredNotify n;
  n.set_player_id(e.id);
  n.set_x(e.x);
  n.set_y(e.y);
  return n;
}

}  // namespace game::logic
