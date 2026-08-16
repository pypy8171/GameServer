#include <gtest/gtest.h>

#include <algorithm>
#include <asio.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "core/dispatch/dispatcher.h"
#include "core/net/group.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"

using namespace game::core;

namespace
{
// 소켓 미개방 더미 세션(registry_test 와 동일 방식). id/멤버십 판정만 쓰고 IO 는 안 탄다.
SessionPtr MakeDummySession(asio::io_context& io, Dispatcher& d, SessionRegistry& r)
{
  asio::ip::tcp::socket sock(io);
  return std::make_shared<Session>(std::move(sock), d, r);
}

bool ContainsId(const std::vector<SessionPtr>& v, SessionId id)
{
  return std::any_of(v.begin(), v.end(),
                     [id](const SessionPtr& s) { return s->id() == id; });
}
}  // namespace

TEST(Group, JoinLeaveCount)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  Group g;

  EXPECT_EQ(g.Count(), 0u);
  auto a = MakeDummySession(io, d, r);
  auto b = MakeDummySession(io, d, r);
  g.Join(a);
  g.Join(b);
  EXPECT_EQ(g.Count(), 2u);

  g.Leave(a->id());
  EXPECT_EQ(g.Count(), 1u);
}

TEST(Group, LeaveIsIdempotent)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  Group g;

  auto a = MakeDummySession(io, d, r);
  g.Join(a);
  g.Leave(a->id());
  g.Leave(a->id());  // 두 번째 = no-op (멱등)
  g.Leave(999999);   // 없는 id = no-op
  EXPECT_EQ(g.Count(), 0u);
}

TEST(Group, ReJoinSameSessionKeepsOne)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  Group g;

  auto a = MakeDummySession(io, d, r);
  g.Join(a);
  g.Join(a);  // 같은 id 재가입 → 여전히 1
  EXPECT_EQ(g.Count(), 1u);
}

TEST(Group, SnapshotRecipientsScopedToMembers)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  Group g;

  auto a = MakeDummySession(io, d, r);
  auto b = MakeDummySession(io, d, r);
  auto outsider = MakeDummySession(io, d, r);  // 가입 안 함
  g.Join(a);
  g.Join(b);

  auto all = g.SnapshotRecipients();
  EXPECT_EQ(all.size(), 2u);
  EXPECT_TRUE(ContainsId(all, a->id()));
  EXPECT_TRUE(ContainsId(all, b->id()));
  EXPECT_FALSE(ContainsId(all, outsider->id()));  // 비멤버는 스코프 밖
}

TEST(Group, SnapshotRecipientsExcludesExcept)
{
  asio::io_context io;
  Dispatcher d;
  SessionRegistry r;
  Group g;

  auto a = MakeDummySession(io, d, r);
  auto b = MakeDummySession(io, d, r);
  g.Join(a);
  g.Join(b);

  auto others = g.SnapshotRecipients(a->id());  // a(발신자) 제외
  EXPECT_EQ(others.size(), 1u);
  EXPECT_FALSE(ContainsId(others, a->id()));
  EXPECT_TRUE(ContainsId(others, b->id()));
}
