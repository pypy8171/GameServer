#include "core/packet/packet.h"

#include <cstring>
#include <stdexcept>

namespace game::core {

const char* PacketIdName(uint16_t id) {
  // 표시용 이름은 enum 상수의 k 접두어(구글 스타일)를 뗀 형태로 노출한다.
  switch (static_cast<PacketId>(id)) {
    case PacketId::Invalid:        return "Invalid";
    case PacketId::EchoRequest:    return "EchoRequest";
    case PacketId::EchoResponse:   return "EchoResponse";
    case PacketId::ChatJoin:       return "ChatJoin";
    case PacketId::ChatSay:        return "ChatSay";
    case PacketId::ChatJoinResult: return "ChatJoinResult";
    case PacketId::ChatBroadcast:  return "ChatBroadcast";
    case PacketId::ChatSystem:     return "ChatSystem";
  }
  return "Unknown";
}

std::vector<uint8_t> MakePacket(PacketId id,
                                const google::protobuf::MessageLite& body) {
  const size_t body_size = body.ByteSizeLong();
  const size_t total = HeaderSize + body_size;
  if (total > MaxPacketSize) {
    throw std::length_error("MakePacket: packet exceeds MaxPacketSize");
  }

  std::vector<uint8_t> buf(total);
  PacketHeader header{static_cast<uint16_t>(total), static_cast<uint16_t>(id)};
  std::memcpy(buf.data(), &header, HeaderSize);
  body.SerializeToArray(buf.data() + HeaderSize, static_cast<int>(body_size));
  return buf;
}

}  // namespace game::core
