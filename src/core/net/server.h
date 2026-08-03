#pragma once

#include <asio.hpp>
#include <cstdint>

namespace game::core {

class Dispatcher;

// TCP Acceptor. 연결을 받을 때마다 Session 을 만들어 읽기 루프를 시작한다.
class Server {
 public:
  Server(asio::io_context& io, uint16_t port, const Dispatcher& dispatcher);

  void Start();

 private:
  void DoAccept();

  asio::io_context& io_;
  asio::ip::tcp::acceptor acceptor_;
  const Dispatcher& dispatcher_;
};

}  // namespace game::core
