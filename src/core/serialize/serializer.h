#pragma once

#include <cstdint>
#include <vector>

#include <google/protobuf/message_lite.h>

namespace game::core {

// 직렬화 추상화(seam). 지금은 Protobuf 뒤에 있지만, 핫패스에서 FlatBuffers/커스텀
// 포맷으로 교체할 때 이 인터페이스 구현만 갈아끼우면 되도록 한 지점으로 격리한다.
// (CLAUDE.md 기술스택: "ISerializer 추상화 뒤 — 핫패스는 이후 교체 가능")
//
// 프레이밍(헤더+바디)은 packet.h 의 MakePacket/DecodeHeader 담당이고,
// 여기서는 "바디(payload) ↔ 메시지" 변환만 책임진다.
class ISerializer {
 public:
  virtual ~ISerializer() = default;

  // 바디 바이트 → 메시지. 실패(malformed/사이즈 불일치)면 false.
  // 호출측은 false 를 받으면 그 패킷을 drop 한다 — 빌드(Debug/Release) 무관. (ADR-G)
  virtual bool Parse(const uint8_t* data, uint16_t size,
                     google::protobuf::MessageLite& out) const = 0;

  // 메시지 → 바디 바이트. 실패면 false.
  virtual bool SerializeBody(const google::protobuf::MessageLite& msg,
                             std::vector<uint8_t>& out) const = 0;
};

// Protocol Buffers 구현.
class ProtobufSerializer final : public ISerializer {
 public:
  bool Parse(const uint8_t* data, uint16_t size,
             google::protobuf::MessageLite& out) const override;
  bool SerializeBody(const google::protobuf::MessageLite& msg,
                     std::vector<uint8_t>& out) const override;
};

}  // namespace game::core
