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

namespace {

InMemoryAccountRepository MakeRepo() {
  InMemoryAccountRepository repo;
  repo.AddAccount("alice", "s3cret", /*player_id=*/42);
  return repo;
}

LoginRequest MakeReq(const std::string& account, const std::string& password) {
  LoginRequest req;
  req.set_account(account);
  req.set_password(password);
  return req;
}

SessionPtr MakeDummySession(asio::io_context& io, Dispatcher& d,
                            SessionRegistry& r) {
  asio::ip::tcp::socket sock(io);
  return std::make_shared<Session>(std::move(sock), d, r);
}

}  // namespace

// ---- 순수 결정(EvaluateLogin) ----

// 유효 자격증명 → 인증하고 principal=계정, 응답 ok + player_id.
TEST(EvaluateLogin, ValidCredentialsAuthenticateAndReturnPlayerId) {
  const auto repo = MakeRepo();
  const LoginOutcome out = EvaluateLogin(repo, MakeReq("alice", "s3cret"));
  EXPECT_TRUE(out.authenticate);
  EXPECT_EQ(out.principal, "alice");
  EXPECT_TRUE(out.response.ok());
  EXPECT_EQ(out.response.player_id(), 42u);
}

// 틀린 비번 → 인증하지 않고 응답 ok=false + 사유.
TEST(EvaluateLogin, InvalidCredentialsDoNotAuthenticate) {
  const auto repo = MakeRepo();
  const LoginOutcome out = EvaluateLogin(repo, MakeReq("alice", "wrong"));
  EXPECT_FALSE(out.authenticate);
  EXPECT_FALSE(out.response.ok());
  EXPECT_FALSE(out.response.reason().empty());
}

// 빈 자격증명 → 저장소 조회 없이 즉시 거부.
TEST(EvaluateLogin, EmptyCredentialsRejectedWithoutAuth) {
  const auto repo = MakeRepo();
  EXPECT_FALSE(EvaluateLogin(repo, MakeReq("", "s3cret")).authenticate);
  EXPECT_FALSE(EvaluateLogin(repo, MakeReq("alice", "")).authenticate);
}

// ---- 핸들러 배선(RegisterLoginHandlers) ----

// 미인증 세션이 LoginRequest 를 보내면(preauth allowlist) 유효 시 세션이 인증된다.
TEST(RegisterLoginHandlers, ValidLoginAuthenticatesSessionThroughDispatcher) {
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

// 틀린 자격증명은 세션을 인증하지 않는다(게이트는 통과하되 인증 실패).
TEST(RegisterLoginHandlers, InvalidLoginLeavesSessionUnauthenticated) {
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
