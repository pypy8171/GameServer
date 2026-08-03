#include "core/packet/packet.h"

#include <cstring>
#include <stdexcept>

namespace game::core {

const char* PacketIdName(uint16_t id) {
  // 표시용 이름은 enum 상수의 k 접두어(구글 스타일)를 뗀 형태로 노출한다.
  switch (static_cast<PacketId>(id)) {
    case PacketId::kInvalid:        return "Invalid";
    case PacketId::kEchoRequest:    return "EchoRequest";
    case PacketId::kEchoResponse:   return "EchoResponse";
    case PacketId::kChatJoin:       return "ChatJoin";
    case PacketId::kChatSay:        return "ChatSay";
    case PacketId::kChatJoinResult: return "ChatJoinResult";
    case PacketId::kChatBroadcast:  return "ChatBroadcast";
    case PacketId::kChatSystem:     return "ChatSystem";
  }
  return "Unknown";
}

std::vector<uint8_t> MakePacket(PacketId id,
                                const google::protobuf::MessageLite& body) {
  const size_t body_size = body.ByteSizeLong();
  const size_t total = kHeaderSize + body_size;
  if (total > kMaxPacketSize) {
    throw std::length_error("MakePacket: packet exceeds kMaxPacketSize");
  }

  std::vector<uint8_t> buf(total);
  PacketHeader header{static_cast<uint16_t>(total), static_cast<uint16_t>(id)};
  std::memcpy(buf.data(), &header, kHeaderSize);
  body.SerializeToArray(buf.data() + kHeaderSize, static_cast<int>(body_size));
  return buf;
}

}  // namespace game::core
