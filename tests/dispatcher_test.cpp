#include <gtest/gtest.h>

#include <asio.hpp>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "chat.pb.h"
#include "core/dispatch/dispatcher.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"
#include "core/packet/packet.h"

using namespace game::core;
using namespace game::proto;

namespace {
// 소켓을 열지 않은 더미 세션. 게이트가 참조하는 authenticated()/principal() 만
// 검증하며 실제 IO(Start/Send)는 부르지 않는다(registry_test 의 헬퍼와 동일 규율).
SessionPtr MakeDummySession(asio::io_context& io, Dispatcher& d,
                            SessionRegistry& r) {
  asio::ip::tcp::socket sock(io);
  return std::make_shared<Session>(std::move(sock), d, r);
}
}  // namespace

// 타입드 등록: 디스패처가 바디를 파싱해 이미-파싱된 메시지를 핸들러에 넘긴다.
// (핸들러마다 반복되던 ParseFromArray 를 등록 지점으로 일괄 회수한 결과)
TEST(DispatcherTyped, DispatchesParsedMessageToTypedHandler) {
  Dispatcher d;
  std::string got;
  d.RegisterTyped<ChatSay>(
      PacketId::ChatSay,
      [&got](const SessionPtr&, const ChatSay& m) {
        got = m.text();
      });

  ChatSay say;
  say.set_text("hi");
  const auto pkt = MakePacket(PacketId::ChatSay, say);
  d.Dispatch(SessionPtr{}, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_EQ(got, "hi");
}

// 손상된 바디는 파싱 실패 → 핸들러를 호출하지 않고 drop 한다(ADR-G). 빌드 무관.
TEST(DispatcherTyped, DropsMalformedBodyWithoutCallingHandler) {
  Dispatcher d;
  bool called = false;
  d.RegisterTyped<ChatJoin>(
      PacketId::ChatJoin,
      [&called](const SessionPtr&, const ChatJoin&) {
        called = true;
      });

  // 헤더는 ChatJoin, 바디는 잘린 length-delimited 필드(태그0x0A len5 인데 1바이트만).
  std::vector<uint8_t> buf(HeaderSize + 3);
  EncodeHeader(buf.data(),
               PacketHeader{static_cast<uint16_t>(buf.size()),
                            static_cast<uint16_t>(PacketId::ChatJoin)});
  buf[HeaderSize + 0] = 0x0A;
  buf[HeaderSize + 1] = 0x05;
  buf[HeaderSize + 2] = 0x41;
  d.Dispatch(SessionPtr{}, buf.data(), static_cast<uint16_t>(buf.size()));

  EXPECT_FALSE(called);
}

// [ADR-J] 미인증 게이트: allowlist 에 없는 패킷은 미인증 세션에서 파싱 이전에 drop.
// (역직렬화조차 도달 못 하게 막아 M1 의 "파싱 후 인증검사" 사각지대를 구조적으로 해소)
TEST(DispatcherGate, DropsNonPreauthPacketFromUnauthenticatedSession) {
  asio::io_context io;
  SessionRegistry reg;
  Dispatcher d;
  bool called = false;
  d.RegisterTyped<ChatSay>(
      PacketId::ChatSay,
      [&called](const SessionPtr&, const ChatSay&) { called = true; });

  auto s = MakeDummySession(io, d, reg);  // 미인증
  ChatSay say;
  say.set_text("hi");
  const auto pkt = MakePacket(PacketId::ChatSay, say);
  d.Dispatch(s, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_FALSE(called);  // 게이트에서 drop — 핸들러 미도달
}

// allowlist 에 등록된 로그인/입장 패킷은 미인증이어도 핸들러까지 도달해야 한다
// (그렇지 않으면 아무도 인증을 시작할 수 없다).
TEST(DispatcherGate, AllowsPreauthPacketFromUnauthenticatedSession) {
  asio::io_context io;
  SessionRegistry reg;
  Dispatcher d;
  bool called = false;
  d.RegisterTyped<ChatJoin>(
      PacketId::ChatJoin,
      [&called](const SessionPtr&, const ChatJoin&) { called = true; });
  d.AllowUnauthenticated(PacketId::ChatJoin);

  auto s = MakeDummySession(io, d, reg);  // 미인증
  ChatJoin join;
  join.set_nickname("alice");
  const auto pkt = MakePacket(PacketId::ChatJoin, join);
  d.Dispatch(s, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_TRUE(called);  // preauth allowlist 통과
}

// 인증된 세션은 allowlist 여부와 무관하게 등록된 핸들러에 도달한다.
TEST(DispatcherGate, AllowsAnyRegisteredPacketFromAuthenticatedSession) {
  asio::io_context io;
  SessionRegistry reg;
  Dispatcher d;
  bool called = false;
  d.RegisterTyped<ChatSay>(
      PacketId::ChatSay,
      [&called](const SessionPtr&, const ChatSay&) { called = true; });

  auto s = MakeDummySession(io, d, reg);
  ASSERT_TRUE(s->Authenticate("alice"));  // 인증 완료
  ChatSay say;
  say.set_text("hi");
  const auto pkt = MakePacket(PacketId::ChatSay, say);
  d.Dispatch(s, pkt.data(), static_cast<uint16_t>(pkt.size()));

  EXPECT_TRUE(called);
}
