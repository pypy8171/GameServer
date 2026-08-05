#include "game_logic/login/login_handlers.h"

#include "core/dispatch/dispatcher.h"
#include "core/log/log.h"
#include "core/net/session.h"
#include "core/packet/packet.h"
#include "game_logic/account/account_repository.h"

namespace game::logic
{

using game::core::Dispatcher;
using game::core::MakePacket;
using game::core::PacketId;
using game::core::SessionPtr;
using game::proto::LoginRequest;
using game::proto::LoginResponse;

LoginOutcome EvaluateLogin(const IAccountRepository& repo,
                           const LoginRequest& req)
{
  LoginOutcome out;
  // 빈 자격증명은 저장소 조회·해시 없이 즉시 거부(불필요한 작업·타이밍 노출 회피).
  if (req.account().empty() || req.password().empty())
  {
    out.response.set_ok(false);
    out.response.set_reason("empty credentials");
    return out;
  }
  const AuthResult ar = repo.Authenticate(req.account(), req.password());
  if (!ar.ok)
  {
    out.response.set_ok(false);
    out.response.set_reason(ar.reason);
    return out;
  }
  out.authenticate = true;
  out.principal = req.account();
  out.response.set_ok(true);
  out.response.set_player_id(ar.player_id);
  return out;
}

void RegisterLoginHandlers(Dispatcher& dispatcher,
                           const IAccountRepository& repo,
                           OnAuthenticated on_authenticated)
{
  // LoginRequest 는 신원을 확립하는 입장 패킷 → 미인증 게이트를 통과해야 한다. [ADR-J]
  dispatcher.AllowUnauthenticated(PacketId::LoginRequest);

  dispatcher.RegisterTyped<LoginRequest>(
      PacketId::LoginRequest,
      [&repo, on_authenticated = std::move(on_authenticated)](
          const SessionPtr& s, const LoginRequest& req)
  {
        // 이미 인증된 세션의 재로그인은 신원 재설정 없이 거부(1회성 유지).
        if (s->authenticated())
        {
          LoginResponse res;
          res.set_ok(false);
          res.set_reason("already authenticated");
          s->Send(MakePacket(PacketId::LoginResponse, res));
          return;
        }
        const LoginOutcome out = EvaluateLogin(repo, req);
        if (out.authenticate)
        {
          // strand 안(디스패치 경로) — 신원 확립(첫 로그인). first-call-wins.
          s->Authenticate(out.principal);
          // 계정/발급 player_id 는 기록하되 비밀번호는 절대 로깅하지 않는다.
          LOG_INFO("[login] {} 인증 성공 (id={}, player_id={})", out.principal,
                   s->id(), out.response.player_id());
        }
        else
        {
          LOG_INFO("[login] 인증 실패 (id={}): {}", s->id(),
                   out.response.reason());
        }
        s->Send(MakePacket(PacketId::LoginResponse, out.response));
        // 인증 성공 후속 훅(월드 입장 등)은 LoginResponse 송신 뒤 호출한다 →
        //   클라는 LoginResponse(ok, player_id) 다음에 WorldEnteredNotify 를 받는다.
        if (out.authenticate && on_authenticated)
        {
          on_authenticated(s, out.response.player_id());
        }
      });
}

}  // namespace game::logic
