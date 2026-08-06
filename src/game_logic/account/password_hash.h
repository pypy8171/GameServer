#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace game::logic
{

// ------------------------------------------------------------
//  비밀번호 해시 (ADR-P)
//
//  ⚠️ 데모용 **비암호학적** 해시다. 절대 실서비스에 쓰지 마라.
//  실서비스는 argon2id/bcrypt(느린 KDF + 계정별 랜덤 솔트)로 교체한다.
//  이 유닛의 목적은 "저장소가 평문 비밀번호를 보관하지 않는다"는 계약과
//  (솔트 + 해시 저장 → 검증 시 재계산 비교) 흐름을 표현하는 데 있다.
//  해시 함수만 교체하면 되도록 호출부(저장소)는 이 파사드 뒤에 격리한다.
// ------------------------------------------------------------

// 계정별 랜덤 솔트를 생성한다(hex 문자열, n_bytes 바이트의 엔트로피). CSPRNG
//   (std::random_device) 기반 — 솔트는 비밀이 아니라 '예측 불가·계정별 유일'이면 된다.
//   예측 가능한 솔트(예: 계정명 파생)는 레인보우 테이블/솔트 재사용에 취약하므로 금지.
//   [S-5] KDF(FNV→argon2id) 실제 교체는 전용 ADR/마일스톤으로 분리됐지만, 랜덤 솔트는
//   의존성 없이 지금 도입해 seam 을 실서비스 형태로 맞춘다.
std::string GenerateSalt(std::size_t n_bytes = 16);

// 솔트와 비밀번호를 결합해 16진 문자열 다이제스트를 만든다. 결정적(같은 입력→같은 출력).
std::string HashPassword(std::string_view password, std::string_view salt);

// 후보 비밀번호가 저장된 해시와 일치하는지 검증한다(솔트로 재계산 후 비교).
bool VerifyPassword(std::string_view password, std::string_view salt,
                    std::string_view expected_hash);

// 상수시간 문자열 비교 — 일치 여부가 '비교에 걸린 시간'으로 새지 않게 한다.
//   일반 `==`/`memcmp` 는 첫 불일치 바이트에서 조기 반환 → 공격자가 다이제스트를
//   바이트 단위로 맞춰 나가는 타이밍 사이드채널이 열린다. 여기선 길이가 같으면
//   전 바이트를 XOR 누적해 항상 끝까지 돈다. 길이는 비밀이 아니므로(해시 길이 고정)
//   길이 불일치는 즉시 false. 게임 무관 인프라지만 계정 유닛 안에서만 쓰여 여기 둔다.
bool ConstantTimeEquals(std::string_view a, std::string_view b);

}  // namespace game::logic
