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

}  // namespace game::core
