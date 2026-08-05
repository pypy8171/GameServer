#pragma once

#include <asio.hpp>
#include <cstdint>
#include <functional>
#include <memory>

namespace game::core
{

class Dispatcher;
class Session;
class SessionRegistry;
using SessionPtr = std::shared_ptr<Session>;

// TCP Acceptor. 연결을 받을 때마다 Session 을 만들어 레지스트리에 등록하고
// 읽기 루프를 시작한다.
class Server
{
 public:
  Server(asio::io_context& io, uint16_t port, const Dispatcher& dispatcher,
         SessionRegistry& registry);

  // 각 세션 종료 시 실행할 앱 훅(예: "X 퇴장" 브로드캐스트). 선택.
  void set_on_disconnect(std::function<void(const SessionPtr&)> hook)
  {
    on_disconnect_ = std::move(hook);
  }

  void Start();

 private:
  void DoAccept();

  asio::io_context& io_;
  asio::ip::tcp::acceptor acceptor_;
  const Dispatcher& dispatcher_;
  SessionRegistry& registry_;
  std::function<void(const SessionPtr&)> on_disconnect_;
};

}  // namespace game::core
