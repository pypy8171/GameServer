#pragma once

#include <functional>
#include <memory>
#include <string>

#include "game_logic/account/account_repository.h"  // IAccountRepository, PlayerId
#include "login.pb.h"

namespace game::core
{
class Dispatcher;
class Session;
}

namespace game::logic
{

// 로그인 결정의 순수 결과(세션·IO 무관 — 테스트 대상).
//   authenticate: 세션에 신원을 귀속시킬지. true 면 principal 이 유효.
//   response: 클라이언트로 돌려줄 LoginResponse.
struct LoginOutcome
{
  bool authenticate = false;
  std::string principal;  // authenticate=true 일 때의 신원(계정 id)
  game::proto::LoginResponse response;
};

// 순수 결정 함수: 저장소 + 요청 → 결과. 세션도 소켓도 건드리지 않는다.
//   빈 자격증명은 저장소에 묻지 않고 즉시 거부(불필요한 조회·해시 회피).
LoginOutcome EvaluateLogin(const IAccountRepository& repo,
                           const game::proto::LoginRequest& req);

// 인증 성공 직후(신원 확립 + LoginResponse 송신 후) 1회 호출되는 옵셔널 훅.
//   MG-⑥ 에서 월드 입장(World::PostEnter)을 여기 배선한다. 기본값(빈 함수)이면
//   로그인만 하고 후속 없음 — 로그인이 월드를 직접 알지 않도록 결합을 끊는 seam.
using OnAuthenticated =
    std::function<void(const std::shared_ptr<game::core::Session>&, PlayerId)>;

// 디스패처에 로그인 핸들러를 등록하고 LoginRequest 를 preauth allowlist 에 넣는다.
//   (allowlist 하지 않으면 미인증 세션의 LoginRequest 가 게이트에 막혀 아무도 못 들어온다)
//   repo 는 dispatcher 보다 오래 살아야 한다(핸들러가 참조 캡처).
//   on_authenticated: 인증 성공 후속 훅(선택). 넘긴 함수도 핸들러 수명 내내 살아야 한다.
void RegisterLoginHandlers(game::core::Dispatcher& dispatcher,
                           const IAccountRepository& repo,
                           OnAuthenticated on_authenticated = {});

}  // namespace game::logic
