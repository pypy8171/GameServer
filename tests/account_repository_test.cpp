#include <gtest/gtest.h>

#include "game_logic/account/in_memory_account_repository.h"

using namespace game::logic;

// 등록된 계정 + 올바른 비번 → 인증 성공 + 부여된 player_id 반환.
TEST(InMemoryAccountRepository, AcceptsCorrectCredentials) {
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "s3cret", /*player_id=*/42);

  const AuthResult r = repo.Authenticate("alice", "s3cret");
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.player_id, 42u);
}

// 틀린 비번 → 실패. 사유는 비어있지 않다.
TEST(InMemoryAccountRepository, RejectsWrongPassword) {
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "s3cret", 42);

  const AuthResult r = repo.Authenticate("alice", "wrong");
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.player_id, 0u);
  EXPECT_FALSE(r.reason.empty());
}

// 없는 계정 → 실패. 사용자 열거 방지: 사유가 "틀린 비번"과 동일해야 한다.
TEST(InMemoryAccountRepository, UnknownAccountIsIndistinguishableFromWrongPw) {
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "s3cret", 42);

  const AuthResult unknown = repo.Authenticate("nobody", "whatever");
  const AuthResult wrongpw = repo.Authenticate("alice", "wrong");
  EXPECT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.reason, wrongpw.reason);  // 구분 불가(계정 존재여부 미노출)
}
