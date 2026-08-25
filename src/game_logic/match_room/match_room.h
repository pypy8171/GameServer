#pragma once

#include <asio.hpp>

#include "core/net/room.h"     // Room, RoomId, SessionId
#include "game_logic/presence/presence.h"  // Presence (역인덱스)

namespace game::logic
{

// 잎(leaf) Room — 인스턴스 매치/파티 방(ADR-X 두 층위: 집합체=has-a Room, 잎=상속).
//   World(집합체)와 달리 core::Room 을 **상속**해 게임-무지 훅 OnEnter/OnLeave 를 채운다.
//   훅은 방 strand 위에서 Presence(SessionId→Room* 역인덱스)를 Track/Untrack 한다 →
//   한 세션이 World+이 방에 **동시** 소속될 때(사이클②), disconnect 시 소속 전 방을
//   일괄 Leave 하는 sweep(ADR-Y, 사이클③)이 이 방까지 커버하게 배선한다(W-1 슬롯 반납).
//
//   경계 리트머스(ADR-X): 코어 Room 은 "누구에게·언제"만 알고 "무슨 규칙"은 모른다.
//     MatchRoom 은 game_logic 에 살며 규칙(=Presence 배선)을 채운다. 훅 파라미터는
//     코어가 아는 타입(SessionId)뿐이라 게임 타입이 코어로 새지 않는다.
//   동시성: Track/Untrack 은 방 strand 위 호출 전제(Room::Join/Leave 규율 상속). Presence
//     자체 mutex 가 역인덱스를 보호하므로 다른 방 strand 와도 안전.
class MatchRoom : public game::core::Room
{
 public:
  MatchRoom(asio::io_context& io, game::core::RoomId id, Presence& presence);

 protected:
  void OnEnter(game::core::SessionId member) override;
  void OnLeave(game::core::SessionId member) override;

 private:
  Presence& presence_;  // 역인덱스(소유 아님 — 배선층이 수명 보유)
};

}  // namespace game::logic
