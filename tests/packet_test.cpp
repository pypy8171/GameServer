#include <gtest/gtest.h>

#include <cstring>
#include <stdexcept>
#include <string>

#include "chat.pb.h"
#include "core/packet/packet.h"
#include "packet.pb.h"

using namespace game::core;
using namespace game::proto;

TEST(Packet, MakePacketFramesHeaderAndRoundTrips) {
  EchoRequest req;
  req.set_message("ping");
  auto buf = MakePacket(PacketId::EchoRequest, req);

  ASSERT_GE(buf.size(), HeaderSize);
  PacketHeader h;
  std::memcpy(&h, buf.data(), HeaderSize);
  EXPECT_EQ(h.size, buf.size());
  EXPECT_EQ(h.id, static_cast<uint16_t>(PacketId::EchoRequest));

  EchoRequest parsed;
  ASSERT_TRUE(parsed.ParseFromArray(
      buf.data() + HeaderSize, static_cast<int>(buf.size() - HeaderSize)));
  EXPECT_EQ(parsed.message(), "ping");
}

TEST(PacketHeader, WriteThenReadRoundTrips) {
  const PacketHeader h{/*size=*/1234, /*id=*/0x1002};
  uint8_t buf[HeaderSize];
  EncodeHeader(buf, h);
  const PacketHeader got = DecodeHeader(buf);
  EXPECT_EQ(got.size, 1234);
  EXPECT_EQ(got.id, 0x1002);
}

// 와이어 포맷은 little-endian 으로 못박는다. 호스트 바이트오더에 의존하지
// 않음을 바이트 단위로 검증 → D6/F4(엔디안 정규화) 회귀 방지.
TEST(PacketHeader, SerializesLittleEndianOnWire) {
  const PacketHeader h{/*size=*/0x3412, /*id=*/0x7856};
  uint8_t buf[HeaderSize];
  EncodeHeader(buf, h);
  EXPECT_EQ(buf[0], 0x12);  // size 하위바이트
  EXPECT_EQ(buf[1], 0x34);  // size 상위바이트
  EXPECT_EQ(buf[2], 0x56);  // id   하위바이트
  EXPECT_EQ(buf[3], 0x78);  // id   상위바이트
}

TEST(Packet, DirectionBandsClassifyChatIds) {
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatJoin)));
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatSay)));
  EXPECT_FALSE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatBroadcast)));

  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::ChatBroadcast)));
  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::ChatNotice)));
  EXPECT_FALSE(IsServerToClient(static_cast<uint16_t>(PacketId::ChatJoin)));
}

TEST(Packet, RejectsOversizePayload) {
  ChatSay say;
  say.set_text(std::string(MaxPacketSize + 100, 'x'));
  EXPECT_THROW(MakePacket(PacketId::ChatSay, say), std::length_error);
}
