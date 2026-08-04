#pragma once

#include <string>
#include <string_view>

namespace game::logic {

// ------------------------------------------------------------
//  비밀번호 해시 (ADR-P)
//
//  ⚠️ 데모용 **비암호학적** 해시다. 절대 실서비스에 쓰지 마라.
//  실서비스는 argon2id/bcrypt(느린 KDF + 계정별 랜덤 솔트)로 교체한다.
//  이 유닛의 목적은 "저장소가 평문 비밀번호를 보관하지 않는다"는 계약과
//  (솔트 + 해시 저장 → 검증 시 재계산 비교) 흐름을 표현하는 데 있다.
//  해시 함수만 교체하면 되도록 호출부(저장소)는 이 파사드 뒤에 격리한다.
// ------------------------------------------------------------

// 솔트와 비밀번호를 결합해 16진 문자열 다이제스트를 만든다. 결정적(같은 입력→같은 출력).
std::string HashPassword(std::string_view password, std::string_view salt);

// 후보 비밀번호가 저장된 해시와 일치하는지 검증한다(솔트로 재계산 후 비교).
bool VerifyPassword(std::string_view password, std::string_view salt,
                    std::string_view expected_hash);

}  // namespace game::logic
