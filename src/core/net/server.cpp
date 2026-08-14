#include "core/net/server.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "core/log/log.h"
#include "core/net/accept_policy.h"
#include "core/net/session.h"
#include "core/net/session_pool.h"
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

// 세션 풀 여유분(draining reserve, ADR-W). 풀 용량 = max_sessions + 이 여유분.
//   라이브 게이트(registry.Count()<max_sessions)로 신규 수용을 막아도, Close 후 아직
//   shared_ptr 참조가 남아 소멸 못한 '꼬리(draining tail)' 세션이 슬롯을 붙들 수 있다.
//   그 지연분을 여유분이 흡수해 풀 고갈(TryAcquire 실패)을 사실상 안 나게 한다.
//   기본 = max(256, 5%). config 주입은 후속(⑤ 카운터 이관 웨이브).
std::size_t DrainingReserve(std::size_t max_sessions)
{
  return std::max<std::size_t>(256, max_sessions / 20);
}
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
  // 상한이 있을 때만 풀을 만든다. 무제한(0) 모드는 저장소 크기를 못 정하므로 기존
  //   힙 make_shared 경로를 유지한다(echo/chat 등 상한 미지정 배선 무변화).
  if (max_sessions_ > 0)
  {
    pool_ = SessionPool::Create(max_sessions_ + DrainingReserve(max_sessions_));
  }
}

void Server::Start()
{
  LOG_INFO("listening on port {}", acceptor_.local_endpoint().port());
  DoAccept();
}

std::size_t Server::draining_tail() const
{
  // 순수 정산(DrainingTail)에 두 관측값을 넘긴다: 풀 점유(occupied) - 라이브(registry).
  //   무제한 모드(pool_ 없음)는 풀 자체가 없어 draining 개념이 성립 안 함 → 0.
  return pool_ ? DrainingTail(pool_->occupied(), registry_.Count()) : 0;
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

        // 세션 생성: 풀이 있으면(상한 모드) 풀에서, 없으면(무제한) 힙에서.
        SessionPtr session;
        if (pool_)
        {
          // 인자는 지연 forward — TryAcquire 가 먼저 실패하면 socket 은 이동되지 않아
          //   아래 백스톱에서 그대로 닫을 수 있다(Session 생성자에 도달해야 이동 발생).
          session = pool_->Acquire(std::move(socket), dispatcher_, registry_,
                                   send_queue_cap_bytes_, policy_);
          if (!session)
          {
            // 방어 백스톱(ADR-W): 라이브 게이트를 통과했는데도 풀에 자유 슬롯이 없다
            //   (draining 꼬리가 여유분을 다 삼킨 극단). 이 연결만 닫고 계속 accept.
            std::error_code ignore;
            socket.close(ignore);
            LOG_WARN("세션 풀 고갈(cap={}) — 연결 거부(백스톱)", pool_->capacity());
            DoAccept();
            return;
          }
        }
        else
        {
          session = std::make_shared<Session>(std::move(socket), dispatcher_,
                                              registry_, send_queue_cap_bytes_,
                                              policy_);
        }
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
