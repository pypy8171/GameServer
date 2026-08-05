#pragma once

#include "game_logic/account/account_repository.h"  // PlayerId

namespace game::logic
{

// 월드에 입장한 플레이어의 최소 가변상태.
//   소유·수명은 World 가 SessionId 키로 관리한다(ADR-N — 코어 Session 은
//   엔티티를 모른다). 가변상태(위치)는 단일 World strand 안에서만 변경한다(ADR-O).
struct PlayerEntity
{
  PlayerId id = 0;   // 로그인 시 서버가 부여한 플레이어 식별자(엔티티 키잉)
  float x = 0.0f;    // 월드 좌표
  float y = 0.0f;
};

}  // namespace game::logic
