#pragma once

#include <asio.hpp>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace game::core {

class Dispatcher;

// 하나의 TCP 연결. 헤더->바디 2단계 비동기 읽기로 패킷 경계를 복원하고,
// 송신은 strand 로 직렬화한 큐를 통해 순서/스레드 안전을 보장한다.
class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket, const Dispatcher& dispatcher);

  void Start();

  // 완성된 프레임([헤더+페이로드])을 전송한다. 어느 스레드에서 호출해도 안전.
  void Send(std::vector<uint8_t> packet);

  asio::ip::tcp::endpoint remote() const { return remote_; }

 private:
  void ReadHeader();
  void ReadBody(uint16_t body_size);
  void DoWrite();
  void Close();

  asio::ip::tcp::socket socket_;
  asio::strand<asio::any_io_executor> strand_;
  const Dispatcher& dispatcher_;
  asio::ip::tcp::endpoint remote_;

  std::vector<uint8_t> recv_buf_;                  // 조립 중인 패킷 [헤더+바디]
  std::deque<std::vector<uint8_t>> send_queue_;    // strand 로 보호됨
  bool writing_ = false;
};

}  // namespace game::core
