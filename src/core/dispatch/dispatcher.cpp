#include "core/dispatch/dispatcher.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include "core/log/log.h"
#include "core/net/session.h"
#include "core/packet/packet.h"

namespace game::core {

namespace {
// "ChatSayRequest(id=0x1002)" 형태로 포맷 — 로그에서 패킷을 한눈에 식별.
std::string DescribePacket(uint16_t id) {
  std::ostringstream os;
  os << PacketIdName(id) << "(id=0x" << std::hex << std::uppercase
     << std::setw(4) << std::setfill('0') << id << ')';
  return os.str();
}
}  // namespace

const ISerializer& Dispatcher::DefaultSerializer() {
  static const ProtobufSerializer kDefault;
  return kDefault;
}

Dispatcher::Dispatcher(const ISerializer& serializer)
    : serializer_(serializer) {}

void Dispatcher::Register(PacketId id, PacketHandler handler) {
  handlers_[static_cast<uint16_t>(id)] = std::move(handler);
}

void Dispatcher::AllowUnauthenticated(PacketId id) {
  preauth_.insert(static_cast<uint16_t>(id));
}

void Dispatcher::OnParseFailed(uint16_t id) const {
  // ADR-G: 역직렬화 실패 = 빌드 무관 drop. 관측을 위해 로그(rate-limit 은 M2).
  LOG_WARN("parse failed for {} — dropped", DescribePacket(id));
}

void Dispatcher::Dispatch(const SessionPtr& session, const uint8_t* packet,
                          uint16_t packet_size) const {
  const PacketHeader header = DecodeHeader(packet);

  // 방향 검증(ADR-D): S->C 대역 패킷을 서버가 수신 = 프로토콜 위반 → 드롭.
  if (IsServerToClient(header.id)) {
    LOG_WARN("wrong-direction (S->C) {} dropped", DescribePacket(header.id));
    return;
  }

  // [ADR-J] 미인증 게이트: 신원 없는 세션은 preauth allowlist(로그인/입장) 외의
  // 패킷을 역직렬화조차 못 하게 여기서 drop 한다(파싱 이전 미들웨어). 이로써
  // "핸들러가 파싱 후에야 인증 검사"하던 M1 사각지대를 구조적으로 닫는다.
  // (session 이 null 인 저수준 테스트 경로는 게이트를 건너뛴다 — 실서버는 항상 세션 보유)
  if (session && !session->authenticated() && !preauth_.count(header.id)) {
    // 이 로그는 인증·핸들러 이전 단계라 미인증 연결만으로 증폭될 수 있다.
    // rate-limit 은 unhandled/parse-fail 로그와 함께 M2 로 이월(ADR-G 와 동일 규율).
    LOG_WARN("unauthenticated {} from id={} dropped (not in preauth allowlist)",
             DescribePacket(header.id), session->id());
    return;
  }

  auto it = handlers_.find(header.id);
  if (it == handlers_.end()) {
    LOG_WARN("unhandled packet {} (no handler registered)",
             DescribePacket(header.id));
    return;
  }

  const uint8_t* body = packet + HeaderSize;
  const uint16_t body_size = static_cast<uint16_t>(packet_size - HeaderSize);
  it->second(session, body, body_size);
}

}  // namespace game::core
