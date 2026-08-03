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
//
// 방향 대역(ADR-D): 수신 검증에 쓴다.
//   C->S : [0x1000, 0x2000)
//   S->C : [0x2000, 0x3000)
//   게임 확장 : 0x8000+
// echo(1,2)는 M0 레거시로 대역 밖(방향 검증 예외).
enum class PacketId : uint16_t {
  kInvalid = 0,

  // --- Echo (M0 수직 슬라이스, 레거시) ---
  kEchoRequest = 1,
  kEchoResponse = 2,

  // --- Chat : C->S (0x1000 대역) ---
  kChatJoin = 0x1001,
  kChatSay = 0x1002,

  // --- Chat : S->C (0x2000 대역) ---
  kChatJoinResult = 0x2001,
  kChatBroadcast = 0x2002,
  kChatSystem = 0x2003,
};

constexpr size_t kHeaderSize = sizeof(PacketHeader);
constexpr size_t kMaxPacketSize = 16 * 1024;  // 코어 상한 (악성 size 방어)
constexpr size_t kMaxChatTextBytes = 1024;    // 채팅 본문 상한

// 방향 판정. 디스패처가 수신 패킷이 S->C 대역이면 거부한다(방향 위반).
constexpr bool IsClientToServer(uint16_t id) {
  return id >= 0x1000 && id < 0x2000;
}
constexpr bool IsServerToClient(uint16_t id) {
  return id >= 0x2000 && id < 0x3000;
}

// NOTE(endianness): 헤더는 현재 호스트 바이트오더로 직렬화한다.
// x64 Windows(리틀엔디안) 단일 플랫폼 기준의 의도적 단순화이며,
// 이기종 클러스터로 확장 시 htons/ntohs 정규화가 필요하다. (PROGRESS.md 참조)

// PacketId → 사람이 읽을 이름. 로깅/디버깅용. 미등록 id는 "unknown".
//   (숫자 id만으론 어떤 패킷인지 알기 어려워 관측성 저하 → 이름 병기)
const char* PacketIdName(uint16_t id);

// protobuf 메시지를 [헤더+페이로드] 프레임으로 직렬화한다.
std::vector<uint8_t> MakePacket(PacketId id,
                                const google::protobuf::MessageLite& body);

}  // namespace game::core
