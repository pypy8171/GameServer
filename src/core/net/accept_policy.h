#pragma once

#include <cstddef>
#include <system_error>

#include <asio.hpp>  // asio::error::operation_aborted

namespace game::core
{

// accept 결과에 대한 서버의 반응(U-4/ADR-T). busy-spin 방지가 핵심:
//   - kAcceptNext        : 성공. 곧바로 다음 연결을 받는다.
//   - kBackoffThenAccept : 일시적 에러(EMFILE/ENFILE 등 자원 고갈 포함). 즉시 재무장하면
//                          같은 에러로 CPU 100% busy-spin → 짧은 백오프 후 재시도.
//   - kStop              : acceptor 취소(operation_aborted). 정상 종료 경로 → 재무장 금지.
enum class AcceptAction
{
  kAcceptNext,
  kBackoffThenAccept,
  kStop,
};

inline AcceptAction ClassifyAcceptResult(const std::error_code& ec)
{
  if (!ec)
  {
    return AcceptAction::kAcceptNext;
  }
  if (ec == asio::error::operation_aborted)
  {
    return AcceptAction::kStop;  // acceptor.close()/cancel() — 종료 중
  }
  return AcceptAction::kBackoffThenAccept;  // 그 외 에러는 백오프 후 재시도
}

// 접속 상한(S-3/ADR-T). cap==0 이면 무제한. 현재 동접(current)이 cap 미만이면 수용.
//   한계 도달 시 새 연결은 조용히 닫고 accept 는 계속 돈다(서버는 살아있음).
inline bool AcceptWithinCap(std::size_t current, std::size_t cap)
{
  return cap == 0 || current < cap;
}

// 세션 풀 draining tail 게이지(ADR-W §9-3). 풀 용량 = max_sessions + draining_reserve
//   정산의 관측 짝이다(AcceptWithinCap = 라이브 게이트). Close 는 registry 에서 세션을
//   즉시 빼지만(라이브 하락) in-flight async op(async_write·타이머)이 refcount>0 로
//   슬롯을 계속 붙들 수 있어, 풀 점유(occupied)가 라이브(live)를 웃돈다. 그 차이가
//   '반납 대기 꼬리(draining tail)' = 반납 지연·수명 누수의 조기 관측 신호다.
//
//   occupied 와 live 는 서로 다른 락(풀 mutex / registry mutex)을 다른 순간에 읽은
//   스냅샷이라, 그 사이 새 세션이 Start→등록되면 순간적으로 live>occupied 가 될 수
//   있다. 그때 size_t 뺄셈이 언더플로해 게이지가 거대값으로 튀지 않도록 0 으로 클램프.
inline std::size_t DrainingTail(std::size_t occupied, std::size_t live)
{
  return occupied > live ? occupied - live : 0;
}

}  // namespace game::core
