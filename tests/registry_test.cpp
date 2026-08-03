#include <gtest/gtest.h>

#include <asio.hpp>
#include <memory>
#include <utility>

#include "core/dispatch/dispatcher.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"

using namespace game::core;

namespace {
// 소켓을 열지 않은(unopened) 더미 세션. 등록/해제/카운트 로직만 검증한다
// (Start/Send 등 실제 IO 는 호출하지 않는다).
SessionPtr MakeDummySession(asio::io_context& io, Dispatcher& d,
                            SessionRegistry& r) {
  asio::ip::tcp::socket sock(io);
  return std::make_shared<Session>(std::move(sock), d, r);
}
}  // namespace

TEST(SessionRegistry, AddRemoveCount) {
  asio::io_context io;
  Dispatcher d;
  SessionRegistry reg;

  EXPECT_EQ(reg.Count(), 0u);
  auto a = MakeDummySession(io, d, reg);
  auto b = MakeDummySession(io, d, reg);
  reg.Add(a);
  reg.Add(b);
  EXPECT_EQ(reg.Count(), 2u);

  reg.Remove(a->id());
  EXPECT_EQ(reg.Count(), 1u);
}

TEST(SessionRegistry, RemoveIsIdempotent) {
  asio::io_context io;
  Dispatcher d;
  SessionRegistry reg;

  auto a = MakeDummySession(io, d, reg);
  reg.Add(a);
  reg.Remove(a->id());
  reg.Remove(a->id());  // 두 번째 = no-op (멱등)
  reg.Remove(999999);   // 없는 id = no-op
  EXPECT_EQ(reg.Count(), 0u);
}

TEST(SessionRegistry, ReAddSameSessionKeepsOne) {
  asio::io_context io;
  Dispatcher d;
  SessionRegistry reg;

  auto a = MakeDummySession(io, d, reg);
  reg.Add(a);
  reg.Add(a);  // 같은 id 재등록 → 여전히 1
  EXPECT_EQ(reg.Count(), 1u);
}

TEST(Session, IdsAreUniqueAndIdentitySetOnce) {
  asio::io_context io;
  Dispatcher d;
  SessionRegistry reg;

  auto a = MakeDummySession(io, d, reg);
  auto b = MakeDummySession(io, d, reg);
  EXPECT_NE(a->id(), b->id());

  EXPECT_FALSE(a->joined());
  EXPECT_TRUE(a->SetIdentity("alice"));
  EXPECT_TRUE(a->joined());
  EXPECT_EQ(a->nickname(), "alice");
  EXPECT_FALSE(a->SetIdentity("bob"));  // 두 번째 신원 설정은 거부
  EXPECT_EQ(a->nickname(), "alice");
}
