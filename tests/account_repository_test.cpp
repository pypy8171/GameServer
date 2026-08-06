#include <gtest/gtest.h>

#include "game_logic/account/in_memory_account_repository.h"

using namespace game::logic;

// 등록된 계정 + 올바른 비번 → 인증 성공 + 부여된 player_id 반환.
TEST(InMemoryAccountRepository, AcceptsCorrectCredentials)
{
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "s3cret", /*player_id=*/42);

  const AuthResult r = repo.Authenticate("alice", "s3cret");
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.player_id, 42u);
}

// 틀린 비번 → 실패. 사유는 비어있지 않다.
TEST(InMemoryAccountRepository, RejectsWrongPassword)
{
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "s3cret", 42);

  const AuthResult r = repo.Authenticate("alice", "wrong");
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.player_id, 0u);
  EXPECT_FALSE(r.reason.empty());
}

// 없는 계정 → 실패. 사용자 열거 방지: 사유가 "틀린 비번"과 동일해야 한다.
TEST(InMemoryAccountRepository, UnknownAccountIsIndistinguishableFromWrongPw)
{
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "s3cret", 42);

  const AuthResult unknown = repo.Authenticate("nobody", "whatever");
  const AuthResult wrongpw = repo.Authenticate("alice", "wrong");
  EXPECT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.reason, wrongpw.reason);  // 구분 불가(계정 존재여부 미노출)
}

// 같은 account 재등록 → 마지막 값이 이긴다(비번·player_id 모두 덮어쓰기). 부트스트랩
//   중복 프로비저닝 시 예전 자격증명이 남아 인증되면 안 된다. [문서화된 계약, U-7]
TEST(InMemoryAccountRepository, ReRegisteringAccountOverwritesWithLatest)
{
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "old", /*player_id=*/1);
  repo.AddAccount("alice", "new", /*player_id=*/2);  // 덮어쓰기

  EXPECT_FALSE(repo.Authenticate("alice", "old").ok);  // 옛 비번은 더 이상 안 통함
  const AuthResult r = repo.Authenticate("alice", "new");
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.player_id, 2u);  // player_id 도 갱신된다
}

// 랜덤 per-account 솔트 하에서도 검증은 정상 동작한다: 같은 비밀번호를 쓰는 두 계정이
//   각자 다른 솔트를 갖더라도 둘 다 올바르게 인증된다(솔트 랜덤화가 검증을 안 깨뜨림). [S-5]
TEST(InMemoryAccountRepository, SamePasswordAcrossAccountsBothAuthenticate)
{
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "same-pw", /*player_id=*/1);
  repo.AddAccount("bob", "same-pw", /*player_id=*/2);

  EXPECT_TRUE(repo.Authenticate("alice", "same-pw").ok);
  EXPECT_TRUE(repo.Authenticate("bob", "same-pw").ok);
  EXPECT_FALSE(repo.Authenticate("alice", "wrong").ok);  // 틀린 비번은 여전히 거부
}
