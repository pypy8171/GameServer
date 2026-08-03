#include "core/dispatch/dispatcher.h"

#include <cstring>
#include <iostream>

namespace game::core {

void Dispatcher::Register(PacketId id, PacketHandler handler) {
  handlers_[static_cast<uint16_t>(id)] = std::move(handler);
}

void Dispatcher::Dispatch(const SessionPtr& session, const uint8_t* packet,
                          uint16_t packet_size) const {
  PacketHeader header;
  std::memcpy(&header, packet, kHeaderSize);

  auto it = handlers_.find(header.id);
  if (it == handlers_.end()) {
    std::cerr << "[dispatch] unknown packet id=" << header.id << '\n';
    return;
  }

  const uint8_t* body = packet + kHeaderSize;
  const uint16_t body_size = static_cast<uint16_t>(packet_size - kHeaderSize);
  it->second(session, body, body_size);
}

}  // namespace game::core
