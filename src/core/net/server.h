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
  // draining_reserve: 세션 풀 여유분(ADR-W). 풀 용량 = max_sessions + 이 값. 0(기본)
  //   이면 max(256, max_sessions/20)=5% 로 자동. config(draining_reserve) 주입 지점.
  //   max_sessions==0(무제한, 풀 없음) 이면 무의미(무시). 자세히는 ResolveDrainingReserve.
  Server(asio::io_context& io, uint16_t port, const Dispatcher& dispatcher,
         SessionRegistry& registry,
         std::size_t send_queue_cap_bytes = kDefaultSendQueueCapBytes,
         const SessionPolicy& policy = {}, std::size_t max_sessions = 0,
         std::size_t draining_reserve = 0);

  // 각 세션 종료 시 실행할 앱 훅(예: "X 퇴장" 브로드캐스트). 선택.
  void set_on_disconnect(std::function<void(const SessionPtr&)> hook)
  {
    on_disconnect_ = std::move(hook);
  }

  void Start();

  // 세션 풀 draining tail 게이지(ADR-W §9-3) — 강제 close 됐으나 in-flight async op 이
  //   슬롯을 아직 붙들고 있는 '반납 대기 꼬리' 세션 수. 반납 지연·수명 누수의 조기 관측
  //   신호다(값이 draining_reserve 에 근접하면 백스톱 트립 임박). 풀 비활성(무제한 모드
  //   = pool_ 없음)이면 0. 스냅샷 관측용(풀·registry 락을 서로 다른 순간에 읽음).
  //   주의: Acquire(occupied++) 와 Start→registry.Add(live++) 사이의 '시작 중' 세션도
  //   이 차(occupied-live)에 순간 섞인다 → 실제 draining 보다 최대 '동시 accept 수'(현
  //   구조상 ≤1)만큼 낙관적으로 튈 수 있다. 순간적·무해하나 정밀 임계 판정 시 감안.
  std::size_t draining_tail() const;

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
