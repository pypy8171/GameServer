#include "game_logic/game/game_handlers.h"

#include <vector>

#include "core/dispatch/dispatcher.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"
#include "game_logic/world/world.h"

namespace game::logic
{

using game::core::Dispatcher;
using game::core::PacketId;
using game::core::SessionPtr;
using game::core::SessionRegistry;
using game::proto::MoveRequest;

void RegisterGameHandlers(Dispatcher& dispatcher, World& world,
                          SessionRegistry& registry)
{
  // MoveRequest 는 allowlist 하지 않는다 → 미인증 게이트(ADR-J)가 막아 입장한
  //   인증세션만 이 핸들러에 도달한다. 별도 authenticated() 재확인 불필요.
  dispatcher.RegisterTyped<MoveRequest>(
      PacketId::MoveRequest,
      [&world, &registry](const SessionPtr& s, const MoveRequest& req)
  {
        // 이동은 월드 strand 로 위임(엔티티 가변상태는 strand 안에서만 — ADR-O).
        //   sid 는 세션 신원값이라 클라가 남의 위치를 못 움직인다(스푸핑 차단).
        //   브로드캐스트 sink 는 registry 를 감싸 World 의 net 의존을 끊는다.
        world.PostMove(s->id(), req.x(), req.y(),
                       [&registry](const std::vector<uint8_t>& frame)
                       { registry.Broadcast(frame); });
      });
}

}  // namespace game::logic
