#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/net/session.h"  // SessionId, SessionPtr

namespace game::core {

// 살아있는 세션 집합. 게임 무관 코어 프리미티브.
//
// 동시성 계약(ADR-A):
//   - 컨테이너는 mutex 로 보호.
//   - Broadcast 는 락 안에서 shared_ptr '스냅샷'만 뜨고 락을 푼 뒤 순회한다.
//     (순회 중 Session::Send 가 대상 strand 로 post 하므로, 락을 잡은 채
//      네트워크 경로로 들어가지 않는다 = 락 범위 최소화 + 데드락 회피.)
//   - 스냅샷이 shared_ptr 이므로 순회 중 대상이 종료돼도 객체는 살아있다(UAF 방지).
//
// 팬아웃(ADR-G): Broadcast 는 프레임을 수신자마다 복사(copy-per-recipient)한다.
//   Session::Send 가 값으로 받으므로 각 호출이 세션별 단일소유 버퍼로 복사된다.
class SessionRegistry {
 public:
  void Add(const SessionPtr& s);
  void Remove(SessionId id);  // 멱등: 없는 id 는 no-op

  // frame([헤더+페이로드])을 현재 모든 세션에 전송한다.
  // except 와 같은 id 인 세션은 건너뛴다(0 이면 전원).
  void Broadcast(const std::vector<uint8_t>& frame, SessionId except = 0);

  std::size_t Count() const;

 private:
  mutable std::mutex mu_;
  std::unordered_map<SessionId, SessionPtr> sessions_;
};

}  // namespace game::core
