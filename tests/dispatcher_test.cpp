#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "chat.pb.h"
#include "core/dispatch/dispatcher.h"
#include "core/packet/packet.h"

using namespace game::core;
using namespace game::proto;

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
