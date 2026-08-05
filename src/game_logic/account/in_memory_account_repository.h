#pragma once

#include <string>
#include <unordered_map>

#include "game_logic/account/account_repository.h"

namespace game::logic
{

// 인메모리 계정 저장소(데모/테스트용 스텁).
//
//  동시성 계약: 계정은 기동 시 AddAccount 로 채우고, 서빙 중에는 Authenticate(const)
//  로만 조회한다. 여러 io 스레드의 동시 읽기만 발생 → 수정 없음 → 락 불필요.
//  (기동 등록 → server.Start() 의 happens-before 를 반드시 유지. 디스패처 preauth 와
//   동일 규율.) 서빙 중 동적 등록이 필요해지면 그 시점에 동기화를 도입한다.
class InMemoryAccountRepository : public IAccountRepository
{
 public:
  // 계정을 등록한다(기동/부트스트랩 전용). 비밀번호는 솔트+해시로만 저장 — 평문 미보관.
  //   같은 account 재등록은 마지막 값으로 덮어쓴다.
  void AddAccount(const std::string& account, const std::string& password,
                  PlayerId player_id);

  AuthResult Authenticate(const std::string& account,
                          const std::string& password) const override;

 private:
  struct Record
  {
    std::string salt;
    std::string hash;
    PlayerId player_id = 0;
  };
  std::unordered_map<std::string, Record> accounts_;
};

}  // namespace game::logic
