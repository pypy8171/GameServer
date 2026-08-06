#pragma once

#include <asio.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/net/send_budget.h"
#include "core/net/token_bucket.h"

namespace game::core
{

class Dispatcher;
class Session;
class SessionRegistry;

using SessionId = std::uint64_t;
using SessionPtr = std::shared_ptr<Session>;

// send 큐 바이트 상한 기본값(백프레셔, ADR-E). config(sendqueue_max_bytes)로 override.
//   단일 최대 패킷(kMaxPacketSize=16KB) 보다 충분히 커야 정상 패킷이 안 잘린다.
inline constexpr std::size_t kDefaultSendQueueCapBytes = 256 * 1024;  // 256KB

// 세션 보안 정책 knob 묶음. 전부 기본 '비활성'이라 미지정 시 동작 변화 없음
//   (echo/chat 등 기존 배선·테스트 호환). 값은 config 에서 주입(game_server).
//   생성자 인자 폭발을 막고 정책을 한 덩어리로 다루기 위한 값 타입.
struct SessionPolicy
{
  // 핸드셰이크 타임아웃(S-1/ADR-S): 접속 후 이 시간 내 미인증이면 강제 종료.
  //   슬로로리스형(연결만 잡고 인증 안 함) 연결 고갈 방어. 0 = 비활성.
  //   활동으로 리셋되지 않는 '절대 마감'이다(미인증 트래픽으로 연장 불가).
  std::chrono::milliseconds handshake_timeout{0};
  // 유휴 타임아웃(S-1/ADR-S): 인증 후 이 시간 동안 수신 활동이 없으면 강제 종료.
  //   half-open(상대 소멸을 TCP 가 늦게 감지) 좀비 세션 회수. 활동마다 리셋. 0 = 비활성.
  std::chrono::milliseconds idle_timeout{0};
  // 인바운드 rate-limit(S-4/ADR-R) 토큰버킷: burst=용량, per_sec=초당 리필.
  //   burst<=0 이면 비활성. 초과 시 세션 강제 종료(플러딩 방어).
  double rate_burst{0.0};
  double rate_per_sec{0.0};
};

// 하나의 TCP 연결. 헤더->바디 2단계 비동기 읽기로 패킷 경계를 복원하고,
// 송신은 strand 로 직렬화한 큐를 통해 순서/스레드 안전을 보장한다.
//
// 수명주기(M0.5):
//   - Start() 시 레지스트리에 자기 자신을 등록.
//   - Close() 는 여러 경로(읽기/쓰기 에러, 잘못된 패킷)에서 불릴 수 있으나
//     closed_ 가드로 '정확히 1회' 정리(등록 해제 + 종료 훅)만 수행한다.
//   - 모든 Close 호출은 자기 strand 위에서 일어난다(가드/신원 필드 접근이 strand 안).
class Session : public std::enable_shared_from_this<Session>
{
 public:
  Session(asio::ip::tcp::socket socket, const Dispatcher& dispatcher,
          SessionRegistry& registry,
          std::size_t send_queue_cap_bytes = kDefaultSendQueueCapBytes,
          const SessionPolicy& policy = {});

  void Start();

  // 완성된 프레임([헤더+페이로드])을 전송한다. 어느 스레드에서 호출해도 안전.
  void Send(std::vector<uint8_t> packet);

  SessionId id() const
  {
    return id_;
  }
  asio::ip::tcp::endpoint remote() const
  {
    return remote_;
  }

  // --- 세션 신원(인증) — strand 안에서만 접근 ---
  // 코어는 "신원이 확립됐는가"(authenticated_)와 "불투명 신원 문자열"(principal_)
  // 만 안다. principal 의 의미는 앱/게임이 정하며 코어는 해석하지 않는다
  //   (chat=닉네임, game=계정 id). 게임 콘텐츠의 코어 누수를 막는 경계. [ADR-L]
  bool authenticated() const
  {
    return authenticated_;
  }
  const std::string& principal() const
  {
    return principal_;
  }
  // 세션 신원을 최초 1회 확립(입장/로그인 게이트 통과). 이미 확립됐으면 false.
  bool Authenticate(std::string principal);

  // 종료 시 1회 호출되는 앱 훅(예: "X 퇴장" 브로드캐스트). 등록 해제 직후 실행.
  void set_on_disconnect(std::function<void(const SessionPtr&)> hook)
  {
    on_disconnect_ = std::move(hook);
  }

 private:
  void ReadHeader();
  void ReadBody(uint16_t body_size);
  void DoWrite();
  void Close();

  // --- 타임아웃(S-1/ADR-S) — 전부 strand 안에서만 호출 ---
  // 핸드셰이크 절대 마감을 건다(접속 시 1회). 활동으로 리셋되지 않는다.
  void ArmHandshakeDeadline();
  // 유휴 마감을 (재)무장한다(인증 성공 시 최초, 이후 수신 활동마다 리셋).
  void ArmIdleDeadline();

  asio::ip::tcp::socket socket_;
  asio::strand<asio::any_io_executor> strand_;
  const Dispatcher& dispatcher_;
  SessionRegistry& registry_;
  asio::ip::tcp::endpoint remote_;
  const SessionId id_;

  std::vector<uint8_t> recv_buf_;                  // 조립 중인 패킷 [헤더+바디]
  std::deque<std::vector<uint8_t>> send_queue_;    // strand 로 보호됨
  SendBudget send_budget_;                         // 큐 바이트 상한 (strand 안, ADR-E)
  bool writing_ = false;
  bool closed_ = false;                            // Close 멱등 가드 (strand 안)

  // 타임아웃 타이머(S-1). strand 실행기로 생성 → 콜백이 strand 안에서 돈다.
  //   재무장(expires_after)은 이전 대기를 operation_aborted 로 취소한다.
  asio::steady_timer timer_;
  const std::chrono::milliseconds handshake_timeout_;  // 0 = 비활성
  const std::chrono::milliseconds idle_timeout_;       // 0 = 비활성
  TokenBucket rate_bucket_;  // 인바운드 rate-limit (strand 안, S-4). 비활성 가능

  std::string principal_;      // 불투명 신원 (strand 안): chat=닉네임, game=계정id
  bool authenticated_ = false;
  std::function<void(const SessionPtr&)> on_disconnect_;
};

}  // namespace game::core
