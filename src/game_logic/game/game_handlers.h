#pragma once

namespace game::core
{
class Dispatcher;
}

namespace game::logic
{

class World;

// 디스패처에 게임(인게임 액션) 핸들러를 등록한다. 현재는 이동(MoveRequest) 하나.
//   MoveRequest 는 인증 후 게임 액션 → preauth allowlist 에 넣지 않는다(미인증 게이트가
//   막아 입장한 인증세션만 도달 — ADR-J). 서버는 sender 를 세션 신원에서 채우므로
//   MoveRequest 에는 sender 필드가 없다(스푸핑 차단).
//   MoveNotify 팬아웃은 World 가 포함한 코어 Room 의 멤버 Group 을 통과하므로(ADR-X
//   결정 B) 여기서 SessionRegistry 를 알 필요가 없다 — world 만 참조 캡처한다.
//   world 는 dispatcher 보다 오래 살아야 한다(핸들러가 참조 캡처).
void RegisterGameHandlers(game::core::Dispatcher& dispatcher, World& world);

}  // namespace game::logic
