#pragma once

#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/net/room.h"     // Room, RoomId
#include "core/net/session.h"  // SessionId

namespace game::logic
{

// SessionId → 소속 Room 집합 역인덱스(ADR-X W-1 안 b). game_logic 층에 산다:
//   "어느 방/파티 소속"은 게임 지식이라 코어 Session 은 Group-무지 유지(group.h ⊥ 신원).
//   disconnect 시 이 세션이 든 전 Room 을 일괄 Leave 시켜 방 멤버 Group 의 강참조를
//   놓게 하는 것이 존재 이유 — 미배선 시 registry 보다 나쁜 영구 슬롯 누수(W-1).
//
//   키 = SessionId(연결 스코프). disconnect 는 '연결' 이벤트라 "이 연결이 든 방을
//     떠난다"가 연결 스코프다. 멤버십이 재접속을 넘어 살아남는지(reconnect grace)는
//     후속 핸드셰이크 ADR — SessionId 키잉은 그 위에 PlayerId 키 durable 층을 얹는
//     additive 확장을 막지 않는다(결정 C 의 엔티티 재키잉과는 별개 축).
//   동시성: 자체 mutex 로 역인덱스를 보호. Sweep 은 락 안에서 소속 방 스냅샷만 뜨고
//     락 밖에서 각 방 strand 로 post(Group::SnapshotRecipients 와 동일 규율).
class Presence
{
 public:
  // 세션이 방에 입장할 때 호출(Room::Join 지점과 짝). 같은 (sid,room) 중복은 무시.
  void Track(game::core::SessionId sid, game::core::Room* room);
  // 세션이 방을 명시적으로 떠날 때 호출(Room::Leave 와 짝). 없으면 no-op(멱등).
  void Untrack(game::core::SessionId sid, game::core::Room* room);

  // disconnect 훅이 부른다: 이 세션이 든 전 Room 을 각자 strand 로 PostLeave 하고
  //   역인덱스 항목을 지운다. 교차-strand = 대상 strand 로 post(ADR-O). 세션 객체는
  //   이미 파괴됐을 수 있어 SessionId 값만 받는다.
  void SweepOnDisconnect(game::core::SessionId sid);

  // 관측/테스트용: 이 세션이 추적 중인 방 수.
  std::size_t RoomCountFor(game::core::SessionId sid) const;

 private:
  mutable std::mutex mu_;
  std::unordered_map<game::core::SessionId,
                     std::vector<game::core::Room*>>
      memberships_;
};

}  // namespace game::logic
