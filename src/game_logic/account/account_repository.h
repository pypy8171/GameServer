#pragma once

#include <cstdint>
#include <string>

namespace game::logic {

using PlayerId = std::uint64_t;

// 인증 결과. ok=true 면 player_id 가 유효(엔티티 키잉에 사용), ok=false 면 reason.
struct AuthResult {
  bool ok = false;
  PlayerId player_id = 0;
  std::string reason;  // 실패 사유. 사용자 열거 방지를 위해 일반 메시지 권장.
};

// 계정 저장소 이음매(seam) — 인증만 노출한다(ADR-P).
//   구현은 인메모리 스텁 → 이후 DB/외부 인증서버로 교체해도 호출부(로그인 핸들러)는
//   불변. 순수 동기 계약: Authenticate 는 블로킹 IO 를 하지 않는다(현재 스텁 기준).
//   ⚠️ 블로킹 DB 로 교체 시엔 로직 스레드에서 직접 호출 금지 — 비동기 경계 필요.
class IAccountRepository {
 public:
  virtual ~IAccountRepository() = default;

  // 계정/비밀번호(평문, TLS 이후 전제)를 검증한다. 저장소는 해시로만 보관·비교한다.
  virtual AuthResult Authenticate(const std::string& account,
                                  const std::string& password) const = 0;
};

}  // namespace game::logic
