#pragma once

#include <asio.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "core/net/session.h"  // kDefaultSendQueueCapBytes

namespace game::core
{

class Dispatcher;
class Session;
class SessionRegistry;
class SessionPool;
using SessionPtr = std::shared_ptr<Session>;

// TCP Acceptor. 연결을 받을 때마다 Session 을 만들어 레지스트리에 등록하고
// 읽기 루프를 시작한다.
class Server
{
 public:
  // send_queue_cap_bytes: 각 세션의 send 큐 바이트 상한(백프레셔, ADR-E). 초과 시
  //   해당 세션만 강제 종료. config(sendqueue_max_bytes)에서 주입, 미지정 시 기본값.
  // policy: 세션 보안 정책(타임아웃 S-1 / rate-limit S-4). 기본 전부 비활성.
  // max_sessions: 동시 접속 상한(S-3/ADR-T). 0 = 무제한(기본). 도달 시 새 연결은
  //   조용히 닫고 accept 는 계속 돈다. 현재 동접은 registry.Count() 로 관측한다.
  Server(asio::io_context& io, uint16_t port, const Dispatcher& dispatcher,
         SessionRegistry& registry,
         std::size_t send_queue_cap_bytes = kDefaultSendQueueCapBytes,
         const SessionPolicy& policy = {}, std::size_t max_sessions = 0);

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
  asio::steady_timer accept_backoff_;  // accept 에러 후 재시도 백오프(U-4)
  const Dispatcher& dispatcher_;
  SessionRegistry& registry_;
  const std::size_t send_queue_cap_bytes_;
  const SessionPolicy policy_;       // 새 세션에 주입할 보안 정책(S-1/S-4)
  const std::size_t max_sessions_;   // 동접 상한(S-3). 0 = 무제한
  // 세션 풀(ADR-B/W). max_sessions>0 일 때만 존재(무제한 모드는 힙 make_shared 유지 —
  //   상한 없이는 저장소 크기를 못 정한다). shared_ptr 인 이유는 세션 deleter 가 풀
  //   강참조를 쥐어 풀 수명을 봉인하기 때문(SessionPool 주석 (3)).
  std::shared_ptr<SessionPool> pool_;
  std::function<void(const SessionPtr&)> on_disconnect_;
};

}  // namespace game::core
