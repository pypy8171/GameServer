#include "game_logic/account/in_memory_account_repository.h"

#include "game_logic/account/password_hash.h"

namespace game::logic
{

namespace
{
// 사용자 열거(account enumeration) 방지: "계정 없음"과 "비번 틀림"을 구분하지 않는다.
constexpr const char* kInvalidCredentials = "invalid credentials";
// 계정 미존재 경로에서 소비할 더미 자격 — 검증 경로와 해시 작업량을 맞춘다(아래 설명).
const std::string kDummySalt = "enumeration-guard-salt";
const std::string kDummyHash = "0000000000000000";
}  // namespace

void InMemoryAccountRepository::AddAccount(const std::string& account,
                                          const std::string& password,
                                          PlayerId player_id)
{
  // 데모 솔트 = 계정명(계정별로 다이제스트 분리). ⚠️ 실서비스는 계정별 랜덤 솔트를
  //   별도 저장한다 — 계정명 파생 솔트는 예측 가능하므로 데모 한정.
  const std::string salt = account;
  accounts_[account] = Record{salt, HashPassword(password, salt), player_id};
}

AuthResult InMemoryAccountRepository::Authenticate(
    const std::string& account, const std::string& password) const
{
  const auto it = accounts_.find(account);
  if (it == accounts_.end())
  {
    // 타이밍 열거 방지: 계정이 없어도 검증과 동등한 해시 작업을 수행한 뒤 실패한다.
    //   find-miss 가 해시를 건너뛰면, seam 을 느린 KDF(argon2id/bcrypt)로 교체하는
    //   순간 응답시간 차로 계정 존재여부가 새는 사이드채널이 된다 — 지금 상수 작업량으로 봉인.
    //   (volatile sink 로 컴파일러의 무의미-호출 제거를 방지.)
    [[maybe_unused]] volatile bool sink =
        VerifyPassword(password, kDummySalt, kDummyHash);
    return AuthResult{false, 0, kInvalidCredentials};  // 계정 존재여부 미노출
  }
  const Record& rec = it->second;
  if (!VerifyPassword(password, rec.salt, rec.hash))
  {
    return AuthResult{false, 0, kInvalidCredentials};
  }
  return AuthResult{true, rec.player_id, {}};
}

}  // namespace game::logic
