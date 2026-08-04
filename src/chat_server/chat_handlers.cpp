#include "chat_server/chat_handlers.h"

#include <cstdint>
#include <string>
#include <vector>

#include "chat.pb.h"
#include "core/dispatch/dispatcher.h"
#include "core/log/log.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"

namespace game::chat {

using namespace game::core;
using namespace game::proto;

namespace {

std::vector<uint8_t> MakeSystem(const std::string& text) {
  SystemNotify sys;
  sys.set_text(text);
  return MakePacket(PacketId::SystemNotify, sys);
}

}  // namespace

void RegisterChatHandlers(Dispatcher& dispatcher, SessionRegistry& registry) {
  // ChatJoinRequest 는 신원을 확립하는 입장 패킷이다 → 미인증 게이트를 통과해야 한다.
  // (allowlist 에 없으면 미인증 세션의 ChatJoinRequest 가 drop 돼 아무도 입장할 수 없다) [ADR-J]
  dispatcher.AllowUnauthenticated(PacketId::ChatJoinRequest);

  // ---- ChatJoinRequest (C->S): 신원 1회 등록 ----
  // 파싱/실패 drop 은 RegisterTyped 가 처리 → 핸들러는 이미-파싱된 msg 만 다룬다.
  dispatcher.RegisterTyped<ChatJoinRequest>(
      PacketId::ChatJoinRequest,
      [&registry](const SessionPtr& s, const ChatJoinRequest& msg) {
        ChatJoinResponse res;
        const std::string& nick = msg.nickname();
        if (nick.empty() || nick.size() > MaxChatTextBytes) {
          res.set_ok(false);
          res.set_reason("invalid nickname");
          s->Send(MakePacket(PacketId::ChatJoinResponse, res));
          return;
        }
        if (!s->Authenticate(nick)) {  // 이미 신원 확립됨(중복 join)
          res.set_ok(false);
          res.set_reason("already joined");
          s->Send(MakePacket(PacketId::ChatJoinResponse, res));
          return;
        }
        res.set_ok(true);
        s->Send(MakePacket(PacketId::ChatJoinResponse, res));
        LOG_INFO("[chat] {} 님 입장 (id={})", nick, s->id());
        // 남은 세션에게 입장 알림(자기 자신 제외).
        registry.Broadcast(MakeSystem(nick + " 님이 입장했습니다."), s->id());
      });

  // ---- ChatSayRequest (C->S): 발화 릴레이 ----
  dispatcher.RegisterTyped<ChatSayRequest>(
      PacketId::ChatSayRequest,
      [&registry](const SessionPtr& s, const ChatSayRequest& msg) {
        if (!s->authenticated()) {
          return;  // 신원 없는 발화는 무시
        }
        if (msg.text().empty() || msg.text().size() > MaxChatTextBytes) {
          return;
        }
        ChatNotify out;
        out.set_sender(s->principal());  // 발신자는 세션 신원에서(스푸핑 차단)
        out.set_text(msg.text());
        // 어떤 유저가 어떤 내용을 보냈는지 기록(내용은 인자로 — 포맷 오해석 방지).
        LOG_INFO("[chat] {}: {}", s->principal(), msg.text());
        // 전원에게(발신자 포함) — 발신자도 자기 발화 왕복을 확인.
        registry.Broadcast(MakePacket(PacketId::ChatNotify, out));
      });
}

std::function<void(const SessionPtr&)> MakeDisconnectHook(
    SessionRegistry& registry) {
  return [&registry](const SessionPtr& s) {
    if (s->authenticated()) {
      LOG_INFO("[chat] {} 님 퇴장 (id={})", s->principal(), s->id());
      registry.Broadcast(MakeSystem(s->principal() + " 님이 퇴장했습니다."));
    }
  };
}

}  // namespace game::chat
