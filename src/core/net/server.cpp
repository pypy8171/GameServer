#include "core/net/server.h"

#include <iostream>
#include <memory>
#include <utility>

#include "core/net/session.h"

namespace game::core {

Server::Server(asio::io_context& io, uint16_t port, const Dispatcher& dispatcher)
    : io_(io),
      acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      dispatcher_(dispatcher) {}

void Server::Start() {
  std::cout << "[server] listening on port " << acceptor_.local_endpoint().port()
            << '\n';
  DoAccept();
}

void Server::DoAccept() {
  acceptor_.async_accept(
      [this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
          auto session = std::make_shared<Session>(std::move(socket), dispatcher_);
          std::cout << "[server] accepted " << session->remote() << '\n';
          session->Start();
        } else {
          std::cerr << "[server] accept error: " << ec.message() << '\n';
        }
        DoAccept();  // 다음 연결 대기
      });
}

}  // namespace game::core
