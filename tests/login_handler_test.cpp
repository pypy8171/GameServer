#include <gtest/gtest.h>

#include <asio.hpp>
#include <memory>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"
#include "game_logic/account/in_memory_account_repository.h"
#include "game_logic/login/login_handlers.h"
#include "login.pb.h"

using namespace game::core;
using namespace game::logic;
using game::proto::LoginRequest;

namespace
{

InMemoryAccountRepository MakeRepo()
{
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "s3cret", /*player_id=*/42);
  return repo;
}

LoginRequest MakeReq(const std::string& account, const std::string& password)
{
  LoginRequest req;
  req.set_account(account);
  req.set_password(password);
  return req;
}

SessionPtr MakeDummySession(asio::io_context& io, Dispatcher& d,
                            SessionRegistry& r)
{
  asio::ip::tcp::socket sock(io);
  return std::make_shared<Session>(std::move(sock), d, r);
}

}  // namespace

// ---- 순수 결정(EvaluateLogin) ----

// 유효 자격증명 → 인증하고 principal=계정, 응답 ok + player_id.
TEST(EvaluateLogin, ValidCredentialsAuthenticateAndReturnPlayerId)
{
  const auto repo = MakeRepo();
  const LoginOutcome out = EvaluateLogin(repo, MakeReq("alice", "s3cret"));
  EXPECT_TRUE(out.authenticate);
  EXPECT_EQ(out.principal, "alice");
  EXPECT_TRUE(out.response.ok());
  EXPECT_EQ(out.response.player_id(), 42u);
}

// 틀린 비번 → 인증하지 않고 응답 ok=false + 사유.
TEST(EvaluateLogin, InvalidCredentialsDoNotAuthenticate)
{
  const auto repo = MakeRepo();
  const LoginOutcome out = EvaluateLogin(repo, MakeReq("alice", "wrong"));
  EXPECT_FALSE(out.authenticate);
  EXPECT_FALSE(out.response.ok());
  EXPECT_FALSE(out.response.reason().empty());
}

// 빈 자격증명 → 저장소 조회 없이 즉시 거부.
TEST(EvaluateLogin, EmptyCredentialsRejectedWithoutAuth)
{
  const auto repo = MakeRepo();
  EXPECT_FALSE(EvaluateLogin(repo, MakeReq("", "s3cret")).authenticate);
  EXPECT_FALSE(EvaluateLogin(repo, MakeReq("alice", "")).authenticate);
}

// ---- 핸들러 배선(RegisterLoginHandlers) ----

// 미인증 세션이 LoginRequest 를 보내면(preauth allowlist) 유효 시 세션이 인증된다.
TEST(RegisterLoginHandlers, ValidLoginAuthenticatesSessionThroughDispatcher)
{
  asio::io_context io;
  SessionRegistry reg;
  Dispatcher d;
  const auto repo = MakeRepo();
  RegisterLoginHandlers(d, repo);

  auto s = MakeDummySession(io, d, reg);  // 미인증
  ASSERT_FALSE(s->authenticated());
  const auto pkt = MakePacket(PacketId::LoginRequest, MakeReq("alice", "s3cret"));
  d.Dispatch(s, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_TRUE(s->authenticated());     // 게이트 통과 + 인증 성공
  EXPECT_EQ(s->principal(), "alice");
}

// 이미 인증된 세션의 재로그인은 거부된다 — 유효 자격증명이어도 신원은 재설정되지 않고
//   인증 훅도 다시 뜨지 않는다(신원 1회성). login_handlers.cpp 의 authenticated() 가드. [U-7]
TEST(RegisterLoginHandlers, ReLoginOnAuthenticatedSessionIsRejected)
{
  asio::io_context io;
  SessionRegistry reg;
  Dispatcher d;
  const auto repo = MakeRepo();

  int calls = 0;
  RegisterLoginHandlers(d, repo,
                        [&](const SessionPtr&, PlayerId) { ++calls; });

  auto s = MakeDummySession(io, d, reg);
  s->Authenticate("alice");  // 이미 인증된 상태로 만든다(1차 로그인 성공 가정)
  ASSERT_TRUE(s->authenticated());

  // 유효 자격증명으로 재로그인 시도 → 거부(신원·훅 변화 없음).
  const auto pkt = MakePacket(PacketId::LoginRequest, MakeReq("alice", "s3cret"));
  d.Dispatch(s, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_TRUE(s->authenticated());   // 여전히 인증(신원 유지)
  EXPECT_EQ(s->principal(), "alice");
  EXPECT_EQ(calls, 0);               // 재인증 훅은 다시 뜨지 않는다
}

// 틀린 자격증명은 세션을 인증하지 않는다(게이트는 통과하되 인증 실패).
TEST(RegisterLoginHandlers, InvalidLoginLeavesSessionUnauthenticated)
{
  asio::io_context io;
  SessionRegistry reg;
  Dispatcher d;
  const auto repo = MakeRepo();
  RegisterLoginHandlers(d, repo);

  auto s = MakeDummySession(io, d, reg);
  const auto pkt = MakePacket(PacketId::LoginRequest, MakeReq("alice", "nope"));
  d.Dispatch(s, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_FALSE(s->authenticated());
}

// ---- 인증 성공 후속 훅(OnAuthenticated) — 월드 입장 배선 seam ----

// 유효 로그인 → 훅이 정확히 1회, 발급된 player_id 와 함께 호출된다.
TEST(RegisterLoginHandlers, OnAuthenticatedFiresOnSuccessWithPlayerId)
{
  asio::io_context io;
  SessionRegistry reg;
  Dispatcher d;
  const auto repo = MakeRepo();

  int calls = 0;
  PlayerId seen_pid = 0;
  RegisterLoginHandlers(d, repo, [&](const SessionPtr&, PlayerId pid) {
    ++calls;
    seen_pid = pid;
  });

  auto s = MakeDummySession(io, d, reg);
  const auto pkt = MakePacket(PacketId::LoginRequest, MakeReq("alice", "s3cret"));
  d.Dispatch(s, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_EQ(calls, 1);
  EXPECT_EQ(seen_pid, 42u);
}

// 실패 로그인 → 훅은 호출되지 않는다(인증되지 않은 세션은 월드에 입장 안 함).
TEST(RegisterLoginHandlers, OnAuthenticatedDoesNotFireOnFailure)
{
  asio::io_context io;
  SessionRegistry reg;
  Dispatcher d;
  const auto repo = MakeRepo();

  int calls = 0;
  RegisterLoginHandlers(d, repo,
                        [&](const SessionPtr&, PlayerId) { ++calls; });

  auto s = MakeDummySession(io, d, reg);
  const auto pkt = MakePacket(PacketId::LoginRequest, MakeReq("alice", "nope"));
  d.Dispatch(s, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_EQ(calls, 0);
}
