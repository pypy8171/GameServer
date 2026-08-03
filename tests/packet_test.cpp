#include <gtest/gtest.h>

#include <cstring>
#include <stdexcept>
#include <string>

#include "chat.pb.h"
#include "core/packet/packet.h"
#include "packet.pb.h"

using namespace game::core;

TEST(Packet, MakePacketFramesHeaderAndRoundTrips) {
  game::proto::EchoRequest req;
  req.set_message("ping");
  auto buf = MakePacket(PacketId::kEchoRequest, req);

  ASSERT_GE(buf.size(), kHeaderSize);
  PacketHeader h;
  std::memcpy(&h, buf.data(), kHeaderSize);
  EXPECT_EQ(h.size, buf.size());
  EXPECT_EQ(h.id, static_cast<uint16_t>(PacketId::kEchoRequest));

  game::proto::EchoRequest parsed;
  ASSERT_TRUE(parsed.ParseFromArray(
      buf.data() + kHeaderSize, static_cast<int>(buf.size() - kHeaderSize)));
  EXPECT_EQ(parsed.message(), "ping");
}

TEST(Packet, DirectionBandsClassifyChatIds) {
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::kChatJoin)));
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::kChatSay)));
  EXPECT_FALSE(IsClientToServer(static_cast<uint16_t>(PacketId::kChatBroadcast)));

  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::kChatBroadcast)));
  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::kChatSystem)));
  EXPECT_FALSE(IsServerToClient(static_cast<uint16_t>(PacketId::kChatJoin)));
}

TEST(Packet, RejectsOversizePayload) {
  game::proto::ChatSay say;
  say.set_text(std::string(kMaxPacketSize + 100, 'x'));
  EXPECT_THROW(MakePacket(PacketId::kChatSay, say), std::length_error);
}
