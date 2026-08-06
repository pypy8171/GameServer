#include "core/net/server.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "core/log/log.h"
#include "core/net/accept_policy.h"
#include "core/net/session.h"
#include "core/net/session_registry.h"

namespace game::core
{

namespace
{
// asio 엔드포인트를 "ip:port" 문자열로. (로그 인자용)
std::string EndpointStr(const asio::ip::tcp::endpoint& ep)
{
  std::error_code ec;
  return ep.address().to_string(ec) + ":" + std::to_string(ep.port());
}

// accept 에러 후 재무장까지의 백오프. busy-spin(CPU 100%)만 막으면 되므로 짧게.
//   자원 고갈(EMFILE 등)이 잠깐 풀릴 시간을 주되 수용 지연은 최소화한다.
constexpr auto kAcceptBackoff = std::chrono::milliseconds(200);
}  // namespace

Server::Server(asio::io_context& io, uint16_t port, const Dispatcher& dispatcher,
               SessionRegistry& registry, std::size_t send_queue_cap_bytes,
               const SessionPolicy& policy, std::size_t max_sessions)
    : io_(io),
      acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      accept_backoff_(io),
      dispatcher_(dispatcher),
      registry_(registry),
      send_queue_cap_bytes_(send_queue_cap_bytes),
      policy_(policy),
      max_sessions_(max_sessions)
{
}

void Server::Start()
{
  LOG_INFO("listening on port {}", acceptor_.local_endpoint().port());
  DoAccept();
}

void Server::DoAccept()
{
  acceptor_.async_accept(
      [this](std::error_code ec, asio::ip::tcp::socket socket) {
        switch (ClassifyAcceptResult(ec))
        {
          case AcceptAction::kStop:
            // acceptor 취소(종료 중) — 재무장하지 않는다. io.run()이 곧 빈다.
            LOG_INFO("acceptor stopped: {}", ec.message());
            return;
          case AcceptAction::kBackoffThenAccept:
            // 일시적 에러: 즉시 재무장하면 같은 에러로 busy-spin → 짧게 백오프 후 재시도.
            LOG_ERROR("accept error: {} — {}ms 백오프 후 재시도", ec.message(),
                      static_cast<long long>(kAcceptBackoff.count()));
            accept_backoff_.expires_after(kAcceptBackoff);
            accept_backoff_.async_wait([this](std::error_code) { DoAccept(); });
            return;
          case AcceptAction::kAcceptNext:
            break;  // 아래에서 정상 수락 처리
        }

        // 접속 상한(S-3): 한계 도달 시 이 연결은 조용히 닫고 계속 accept.
        if (!AcceptWithinCap(registry_.Count(), max_sessions_))
        {
          std::error_code ep_ec;
          const auto ep = socket.remote_endpoint(ep_ec);
          LOG_WARN("접속 상한({}) 도달 — {} 거부", max_sessions_,
                   ep_ec ? std::string("?") : EndpointStr(ep));
          std::error_code ignore;
          socket.close(ignore);
          DoAccept();
          return;
        }

        auto session = std::make_shared<Session>(std::move(socket), dispatcher_,
                                                 registry_, send_queue_cap_bytes_,
                                                 policy_);
        LOG_INFO("accepted {} (id={})", EndpointStr(session->remote()),
                 session->id());
        if (on_disconnect_)
        {
          session->set_on_disconnect(on_disconnect_);
        }
        session->Start();
        DoAccept();  // 다음 연결 대기
      });
}

}  // namespace game::core
