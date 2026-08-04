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
//   C->S : [0x1000, 0x2000)         (인프라/세션 패킷: chat, login)
//   S->C : [0x2000, 0x3000)
//   게임 확장 0x8000+ 의 방향 하위관례(첫 게임패킷 전 확정):
//     게임 C->S : [0x8000, 0x9000)  (예: Move)
//     게임 S->C : [0x9000, 0xA000)  (예: WorldEntered, MoveNotify)
// echo(1,2)는 M0 레거시로 대역 밖(방향 검증 예외).
enum class PacketId : uint16_t {
  Invalid = 0,

  // --- Echo (M0 수직 슬라이스, 레거시) ---
  EchoRequest = 1,
  EchoResponse = 2,

  // --- Chat : C->S (0x1000 대역) ---
  ChatJoin = 0x1001,
  ChatSay = 0x1002,

  // --- Chat : S->C (0x2000 대역) ---
  ChatJoinResult = 0x2001,
  ChatBroadcast = 0x2002,
  ChatNotice = 0x2003,

  // --- Login : 인게임 핸드셰이크 (인프라 성격, 표준 방향대역) ---
  //   미인증 세션에도 허용되는 preauth 패킷(디스패치 게이트의 allowlist 대상).
  LoginRequest = 0x1010,  // C->S : 계정/비번 제출
  LoginResult = 0x2010,   // S->C : 인증 결과(+player_id)

  // --- Game : 게임 콘텐츠 (0x8000+ 확장 대역) ---
  //   방향 하위관례: C->S [0x8000,0x9000), S->C [0x9000,0xA000).
  Move = 0x8001,          // C->S : 이동 요청
  WorldEntered = 0x9001,  // S->C : 로그인 성공→월드입장 첫 스냅샷(내 스폰)
  MoveNotify = 0x9002,    // S->C : 이동 결과 브로드캐스트
};

constexpr size_t HeaderSize = sizeof(PacketHeader);
constexpr size_t MaxPacketSize = 16 * 1024;  // 코어 상한 (악성 size 방어)
constexpr size_t MaxChatTextBytes = 1024;    // 채팅 본문 상한

// 방향 판정. 디스패처가 수신 패킷이 S->C 대역이면 거부한다(방향 위반).
// 표준 대역(인프라)과 0x8000+ 게임 확장 대역을 모두 포함한다.
constexpr bool IsClientToServer(uint16_t id) {
  return (id >= 0x1000 && id < 0x2000) || (id >= 0x8000 && id < 0x9000);
}
constexpr bool IsServerToClient(uint16_t id) {
  return (id >= 0x2000 && id < 0x3000) || (id >= 0x9000 && id < 0xA000);
}

// NOTE(endianness): 헤더는 현재 호스트 바이트오더로 직렬화한다.
// x64 Windows(리틀엔디안) 단일 플랫폼 기준의 의도적 단순화이며,
// 이기종 클러스터로 확장 시 htons/ntohs 정규화가 필요하다. (PROGRESS.md 참조)

// 헤더 (de)serialize 를 한 곳으로 집약한다(D6/F4 엔디안 정규화 흡수).
// 와이어 포맷 = little-endian 고정. 바이트 단위 load/store 라 호스트 바이트오더에
// 무관하게 정답이며, x64(LE)에선 단일 mov 로 컴파일된다(no-op 래퍼 아님).
// 수신/송신/디스패치 어디서도 raw memcpy 로 헤더를 다루지 말고 이 헬퍼를 쓴다.
// (Session::ReadHeader 멤버와의 혼동을 피해 Decode/Encode 로 명명.)
PacketHeader DecodeHeader(const uint8_t* src);
void EncodeHeader(uint8_t* dst, const PacketHeader& header);

// PacketId → 사람이 읽을 이름. 로깅/디버깅용. 미등록 id는 "unknown".
//   (숫자 id만으론 어떤 패킷인지 알기 어려워 관측성 저하 → 이름 병기)
const char* PacketIdName(uint16_t id);

// protobuf 메시지를 [헤더+페이로드] 프레임으로 직렬화한다.
std::vector<uint8_t> MakePacket(PacketId id,
                                const google::protobuf::MessageLite& body);

}  // namespace game::core
