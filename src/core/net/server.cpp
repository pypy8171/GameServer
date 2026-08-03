#include "core/net/server.h"

#include <memory>
#include <string>
#include <utility>

#include "core/log/log.h"
#include "core/net/session.h"

namespace game::core {

namespace {
// asio 엔드포인트를 "ip:port" 문자열로. (로그 인자용)
std::string EndpointStr(const asio::ip::tcp::endpoint& ep) {
  std::error_code ec;
  return ep.address().to_string(ec) + ":" + std::to_string(ep.port());
}
}  // namespace

Server::Server(asio::io_context& io, uint16_t port, const Dispatcher& dispatcher,
               SessionRegistry& registry)
    : io_(io),
      acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      dispatcher_(dispatcher),
      registry_(registry) {}

void Server::Start() {
  LOG_INFO("listening on port {}", acceptor_.local_endpoint().port());
  DoAccept();
}

void Server::DoAccept() {
  acceptor_.async_accept(
      [this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
          auto session = std::make_shared<Session>(std::move(socket),
                                                   dispatcher_, registry_);
          LOG_INFO("accepted {} (id={})", EndpointStr(session->remote()),
                   session->id());
          if (on_disconnect_) {
            session->set_on_disconnect(on_disconnect_);
          }
          session->Start();
        } else {
          LOG_ERROR("accept error: {}", ec.message());
        }
        DoAccept();  // 다음 연결 대기
      });
}

}  // namespace game::core
