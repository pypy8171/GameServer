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
  auto buf = MakePacket(PacketId::EchoRequest, req);

  ASSERT_GE(buf.size(), HeaderSize);
  PacketHeader h;
  std::memcpy(&h, buf.data(), HeaderSize);
  EXPECT_EQ(h.size, buf.size());
  EXPECT_EQ(h.id, static_cast<uint16_t>(PacketId::EchoRequest));

  game::proto::EchoRequest parsed;
  ASSERT_TRUE(parsed.ParseFromArray(
      buf.data() + HeaderSize, static_cast<int>(buf.size() - HeaderSize)));
  EXPECT_EQ(parsed.message(), "ping");
}

TEST(Packet, DirectionBandsClassifyChatIds) {
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatJoin)));
  EXPECT_TRUE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatSay)));
  EXPECT_FALSE(IsClientToServer(static_cast<uint16_t>(PacketId::ChatBroadcast)));

  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::ChatBroadcast)));
  EXPECT_TRUE(IsServerToClient(static_cast<uint16_t>(PacketId::ChatSystem)));
  EXPECT_FALSE(IsServerToClient(static_cast<uint16_t>(PacketId::ChatJoin)));
}

TEST(Packet, RejectsOversizePayload) {
  game::proto::ChatSay say;
  say.set_text(std::string(MaxPacketSize + 100, 'x'));
  EXPECT_THROW(MakePacket(PacketId::ChatSay, say), std::length_error);
}
