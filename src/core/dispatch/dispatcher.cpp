#include "core/dispatch/dispatcher.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include "core/log/log.h"
#include "core/packet/packet.h"

namespace game::core {

namespace {
// "kChatSay(id=0x1002)" 형태로 포맷 — 로그에서 패킷을 한눈에 식별.
std::string DescribePacket(uint16_t id) {
  std::ostringstream os;
  os << PacketIdName(id) << "(id=0x" << std::hex << std::uppercase
     << std::setw(4) << std::setfill('0') << id << ')';
  return os.str();
}
}  // namespace

void Dispatcher::Register(PacketId id, PacketHandler handler) {
  handlers_[static_cast<uint16_t>(id)] = std::move(handler);
}

void Dispatcher::Dispatch(const SessionPtr& session, const uint8_t* packet,
                          uint16_t packet_size) const {
  PacketHeader header;
  std::memcpy(&header, packet, kHeaderSize);

  // 방향 검증(ADR-D): S->C 대역 패킷을 서버가 수신 = 프로토콜 위반 → 드롭.
  if (IsServerToClient(header.id)) {
    LOG_WARN("wrong-direction (S->C) {} dropped", DescribePacket(header.id));
    return;
  }

  auto it = handlers_.find(header.id);
  if (it == handlers_.end()) {
    LOG_WARN("unhandled packet {} (no handler registered)",
             DescribePacket(header.id));
    return;
  }

  const uint8_t* body = packet + kHeaderSize;
  const uint16_t body_size = static_cast<uint16_t>(packet_size - kHeaderSize);
  it->second(session, body, body_size);
}

}  // namespace game::core
