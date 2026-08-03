#include "core/packet/packet.h"

#include <cstring>
#include <stdexcept>

namespace game::core {

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
