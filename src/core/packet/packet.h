#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <google/protobuf/message_lite.h>

namespace game::core {

// ------------------------------------------------------------
//  패킷 프레이밍
//  [ PacketHeader (4B) ][ payload (직렬화 바이트) ]
//  TCP는 스트림이므로 하나의 recv 에 조각/여러 패킷이 섞여 온다.
//  header.size 로 한 패킷의 경계를 판정한다.
// ------------------------------------------------------------
#pragma pack(push, 1)
struct PacketHeader {
  uint16_t size;  // 헤더 포함 전체 바이트 수
  uint16_t id;    // PacketId
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == 4, "PacketHeader must be 4 bytes");

// 패킷 ID = 라우팅 키. 직렬화 포맷과 독립. 게임별로 확장한다.
enum class PacketId : uint16_t {
  kInvalid = 0,
  kEchoRequest = 1,
  kEchoResponse = 2,
};

constexpr size_t kHeaderSize = sizeof(PacketHeader);
constexpr size_t kMaxPacketSize = 16 * 1024;  // 코어 상한 (악성 size 방어)

// NOTE(endianness): 헤더는 현재 호스트 바이트오더로 직렬화한다.
// x64 Windows(리틀엔디안) 단일 플랫폼 기준의 의도적 단순화이며,
// 이기종 클러스터로 확장 시 htons/ntohs 정규화가 필요하다. (PROGRESS.md 참조)

// protobuf 메시지를 [헤더+페이로드] 프레임으로 직렬화한다.
std::vector<uint8_t> MakePacket(PacketId id,
                                const google::protobuf::MessageLite& body);

}  // namespace game::core
