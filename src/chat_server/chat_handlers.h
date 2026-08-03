#pragma once

#include <functional>
#include <memory>

namespace game::core {
class Dispatcher;
class SessionRegistry;
class Session;
}  // namespace game::core

namespace game::chat {

// 채팅 릴레이 핸들러(ChatJoin/ChatSay)를 디스패처에 등록한다.
// 데모/하네스 로직 — 게임 무관 코어(src/core) 밖에 격리한다.
void RegisterChatHandlers(game::core::Dispatcher& dispatcher,
                          game::core::SessionRegistry& registry);

// 세션 종료 시 "퇴장" 알림을 남은 세션에 브로드캐스트하는 훅.
std::function<void(const std::shared_ptr<game::core::Session>&)>
MakeDisconnectHook(game::core::SessionRegistry& registry);

}  // namespace game::chat
