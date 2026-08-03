#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

#include "core/packet/packet.h"

namespace game::core {

class Session;
using SessionPtr = std::shared_ptr<Session>;

// 핸들러 시그니처: (세션, payload 시작 포인터, payload 길이)
using PacketHandler =
    std::function<void(const SessionPtr&, const uint8_t*, uint16_t)>;

// 패킷 ID -> 핸들러 라우팅 테이블.
// 서버 기동 시 한 번 Register 하고, 이후 읽기 전용으로만 조회하므로 락 불필요.
class Dispatcher {
 public:
  void Register(PacketId id, PacketHandler handler);

  // 완성된 한 패킷([헤더+페이로드])을 라우팅한다.
  // 알 수 없는 id 는 무시(로그)한다.
  void Dispatch(const SessionPtr& session, const uint8_t* packet,
                uint16_t packet_size) const;

 private:
  std::unordered_map<uint16_t, PacketHandler> handlers_;
};

}  // namespace game::core
