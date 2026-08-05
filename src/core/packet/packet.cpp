#include "core/packet/packet.h"

#include <cstring>
#include <stdexcept>

namespace game::core
{

const char* PacketIdName(uint16_t id)
{
  // 표시용 이름은 enum 상수의 k 접두어(구글 스타일)를 뗀 형태로 노출한다.
  switch (static_cast<PacketId>(id))
  {
    case PacketId::Invalid:        return "Invalid";
    case PacketId::EchoRequest:    return "EchoRequest";
    case PacketId::EchoResponse:   return "EchoResponse";
    case PacketId::ChatJoinRequest:    return "ChatJoinRequest";
    case PacketId::ChatSayRequest:     return "ChatSayRequest";
    case PacketId::ChatJoinResponse:   return "ChatJoinResponse";
    case PacketId::ChatNotify:         return "ChatNotify";
    case PacketId::SystemNotify:       return "SystemNotify";
    case PacketId::LoginRequest:       return "LoginRequest";
    case PacketId::LoginResponse:      return "LoginResponse";
    case PacketId::MoveRequest:        return "MoveRequest";
    case PacketId::WorldEnteredNotify: return "WorldEnteredNotify";
    case PacketId::MoveNotify:         return "MoveNotify";
  }
  return "Unknown";
}

namespace
{
// 와이어 포맷(little-endian) ↔ 호스트 정규화. 바이트 단위라 호스트 엔디안 무관.
uint16_t LoadLE16(const uint8_t* p)
{
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
void StoreLE16(uint8_t* p, uint16_t v)
{
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}
}  // namespace

PacketHeader DecodeHeader(const uint8_t* src)
{
  return PacketHeader{LoadLE16(src), LoadLE16(src + sizeof(uint16_t))};
}
void EncodeHeader(uint8_t* dst, const PacketHeader& header)
{
  StoreLE16(dst, header.size);
  StoreLE16(dst + sizeof(uint16_t), header.id);
}

std::vector<uint8_t> MakePacket(PacketId id,
                                const google::protobuf::MessageLite& body)
{
  const size_t body_size = body.ByteSizeLong();
  const size_t total = kHeaderSize + body_size;
  if (total > kMaxPacketSize)
  {
    throw std::length_error("MakePacket: packet exceeds kMaxPacketSize");
  }

  std::vector<uint8_t> buf(total);
  EncodeHeader(buf.data(), PacketHeader{static_cast<uint16_t>(total),
                                        static_cast<uint16_t>(id)});
  body.SerializeToArray(buf.data() + kHeaderSize, static_cast<int>(body_size));
  return buf;
}

}  // namespace game::core
