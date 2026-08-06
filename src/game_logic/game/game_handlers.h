#pragma once

namespace game::core
{
class Dispatcher;
class SessionRegistry;
}

namespace game::logic
{

class World;

// 디스패처에 게임(인게임 액션) 핸들러를 등록한다. 현재는 이동(MoveRequest) 하나.
//   MoveRequest 는 인증 후 게임 액션 → preauth allowlist 에 넣지 않는다(미인증 게이트가
//   막아 입장한 인증세션만 도달 — ADR-J). 서버는 sender 를 세션 신원에서 채우므로
//   MoveRequest 에는 sender 필드가 없다(스푸핑 차단).
//   world/registry 는 dispatcher 보다 오래 살아야 한다(핸들러가 참조 캡처).
void RegisterGameHandlers(game::core::Dispatcher& dispatcher, World& world,
                          game::core::SessionRegistry& registry);

}  // namespace game::logic
