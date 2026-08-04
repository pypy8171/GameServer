#pragma once

#include <asio.hpp>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace game::core {

class Dispatcher;
class Session;
class SessionRegistry;

using SessionId = std::uint64_t;
using SessionPtr = std::shared_ptr<Session>;

// 하나의 TCP 연결. 헤더->바디 2단계 비동기 읽기로 패킷 경계를 복원하고,
// 송신은 strand 로 직렬화한 큐를 통해 순서/스레드 안전을 보장한다.
//
// 수명주기(M0.5):
//   - Start() 시 레지스트리에 자기 자신을 등록.
//   - Close() 는 여러 경로(읽기/쓰기 에러, 잘못된 패킷)에서 불릴 수 있으나
//     closed_ 가드로 '정확히 1회' 정리(등록 해제 + 종료 훅)만 수행한다.
//   - 모든 Close 호출은 자기 strand 위에서 일어난다(가드/신원 필드 접근이 strand 안).
class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket, const Dispatcher& dispatcher,
          SessionRegistry& registry);

  void Start();

  // 완성된 프레임([헤더+페이로드])을 전송한다. 어느 스레드에서 호출해도 안전.
  void Send(std::vector<uint8_t> packet);

  SessionId id() const { return id_; }
  asio::ip::tcp::endpoint remote() const { return remote_; }

  // --- 세션 신원(인증) — strand 안에서만 접근 ---
  // 코어는 "신원이 확립됐는가"(authenticated_)와 "불투명 신원 문자열"(principal_)
  // 만 안다. principal 의 의미는 앱/게임이 정하며 코어는 해석하지 않는다
  //   (chat=닉네임, game=계정 id). 게임 콘텐츠의 코어 누수를 막는 경계. [ADR-L]
  bool authenticated() const { return authenticated_; }
  const std::string& principal() const { return principal_; }
  // 세션 신원을 최초 1회 확립(입장/로그인 게이트 통과). 이미 확립됐으면 false.
  bool Authenticate(std::string principal);

  // 종료 시 1회 호출되는 앱 훅(예: "X 퇴장" 브로드캐스트). 등록 해제 직후 실행.
  void set_on_disconnect(std::function<void(const SessionPtr&)> hook) {
    on_disconnect_ = std::move(hook);
  }

 private:
  void ReadHeader();
  void ReadBody(uint16_t body_size);
  void DoWrite();
  void Close();

  asio::ip::tcp::socket socket_;
  asio::strand<asio::any_io_executor> strand_;
  const Dispatcher& dispatcher_;
  SessionRegistry& registry_;
  asio::ip::tcp::endpoint remote_;
  const SessionId id_;

  std::vector<uint8_t> recv_buf_;                  // 조립 중인 패킷 [헤더+바디]
  std::deque<std::vector<uint8_t>> send_queue_;    // strand 로 보호됨
  bool writing_ = false;
  bool closed_ = false;                            // Close 멱등 가드 (strand 안)

  std::string principal_;      // 불투명 신원 (strand 안): chat=닉네임, game=계정id
  bool authenticated_ = false;
  std::function<void(const SessionPtr&)> on_disconnect_;
};

}  // namespace game::core
