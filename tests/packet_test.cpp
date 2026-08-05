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

  ASSERT_GE(buf.size(), kHeaderSize);
  PacketHeader h;
  std::memcpy(&h, buf.data(), kHeaderSize);
  EXPECT_EQ(h.size, buf.size());
  EXPECT_EQ(h.id, static_cast<uint16_t>(PacketId::EchoRequest));

  EchoRequest parsed;
  ASSERT_TRUE(parsed.ParseFromArray(
      buf.data() + kHeaderSize, static_cast<int>(buf.size() - kHeaderSize)));
  EXPECT_EQ(parsed.message(), "ping");
}

TEST(PacketHeader, WriteThenReadRoundTrips) {
  const PacketHeader h{/*size=*/1234, /*id=*/0x1002};
  uint8_t buf[kHeaderSize];
  EncodeHeader(buf, h);
  const PacketHeader got = DecodeHeader(buf);
  EXPECT_EQ(got.size, 1234);
  EXPECT_EQ(got.id, 0x1002);
}

// 와이어 포맷은 little-endian 으로 못박는다. 호스트 바이트오더에 의존하지
// 않음을 바이트 단위로 검증 → D6/F4(엔디안 정규화) 회귀 방지.
TEST(PacketHeader, SerializesLittleEndianOnWire) {
  const PacketHeader h{/*size=*/0x3412, /*id=*/0x7856};
  uint8_t buf[kHeaderSize];
  EncodeHeader(buf, h);
  EXPECT_EQ(buf[0], 0x12);  // size 하위바이트
  EXPECT_EQ(buf[1], 0x34);  // size 상위바이트
  EXPECT_EQ(buf[2], 0x56);  // id   하위바이트
  EXPECT_EQ(buf[3], 0x78);  // id   상위바이트
}

TEST(Packet, DirectionBandsClassifyChatIds) {
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatJoinRequest)));
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatSayRequest)));
  EXPECT_FALSE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatNotify)));

  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::ChatNotify)));
  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::SystemNotify)));
  EXPECT_FALSE(IsServerToClient(static_cast<uint16_t>(PacketId::ChatJoinRequest)));
}

// 로그인은 표준 방향대역(0x1xxx/0x2xxx), 게임 콘텐츠는 확장대역(0x8xxx C->S /
// 0x9xxx S->C). 첫 게임패킷 전 확정한 하위관례의 회귀 가드.
TEST(Packet, DirectionBandsClassifyLoginAndGameIds) {
  // 로그인(인프라 성격) — 표준 대역
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::LoginRequest)));
  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::LoginResponse)));

  // 게임 콘텐츠 — 0x8000+ 확장 대역
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::MoveRequest)));
  EXPECT_FALSE(IsServerToClient(static_cast<uint16_t>(PacketId::MoveRequest)));

  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::WorldEnteredNotify)));
  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::MoveNotify)));
  EXPECT_FALSE(IsClientToServer(static_cast<uint16_t>(PacketId::MoveNotify)));
}

TEST(Packet, RejectsOversizePayload) {
  ChatSayRequest say;
  say.set_text(std::string(kMaxPacketSize + 100, 'x'));
  EXPECT_THROW(MakePacket(PacketId::ChatSayRequest, say), std::length_error);
}
