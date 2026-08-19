#pragma once

#include <asio.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/net/group.h"    // Group (멤버 팬아웃, 소유)
#include "core/net/session.h"  // SessionId, SessionPtr

namespace game::core
{

using RoomId = std::uint64_t;

// 코어 Room(ADR-X 결정 B): strand + 멤버 Group + create/destroy 수명 + 게임-무지 훅.
//   인프라만 안다(수명·strand·팬아웃) — 게임 규칙/콘텐츠는 모른다(경계 리트머스 통과).
//   집합체(World/Zone)는 이걸 has-a 로 포함, 잎(MatchRoom 등)은 상속해 훅을 채운다.
//
//   경계 리트머스(ADR-X): 이 클래스는 "누구에게 뿌리고, 언제 방을 여닫는가"만 안다.
//     "무슨 규칙의 방인가"(전투·거래·존)는 서브클래스(game_logic)가 훅으로 채운다 →
//     훅 시그니처는 코어가 아는 타입(SessionId·시간)만. 게임 타입 절대 미참조(의존
//     방향 game_logic→game_core 가 이를 물리적으로 강제하기도 한다).
//   동시성(ADR-A/O 재사용): 멤버십 변이·팬아웃은 방 strand 위 호출 전제. 교차-strand
//     진입은 PostLeave 처럼 대상 strand 로 post(락 아님 — ADR-O 핸드오프).
//   수명(W-1): 멤버 Group 은 세션을 강참조(shared_ptr)로 붙든다 → 세션이 방을 떠나기
//     전(Leave)까지 풀 슬롯을 점유한다(draining tail 과 달리 영구). disconnect 시 소속
//     전 방의 일괄 Leave 는 게임층 Presence 가 배선한다 — 코어 Session 은 자신이 어느
//     방에 속하는지 모른다(Group ⊥ 신원, group.h). 미배선 시 registry 보다 나쁜 누수.
class Room
{
 public:
  Room(asio::io_context& io, RoomId id);
  virtual ~Room() = default;
  Room(const Room&) = delete;  // Group(mutex) + strand 소유 → 비복사·비이동
  Room& operator=(const Room&) = delete;

  RoomId id() const { return id_; }
  std::size_t Count() const { return members_.Count(); }
  asio::strand<asio::any_io_executor>& strand() { return strand_; }

  // 멤버십(방 strand 위 호출 전제). Group 을 직접 노출하지 않고 이 파사드로 funnel —
  //   그래야 OnEnter/OnLeave 훅이 발화하고 "멤버십 변이 = 한 입구" 불변식이 선다.
  void Join(const SessionPtr& s);  // members_.Join(s) → OnEnter
  void Leave(SessionId member);    // members_.Leave(member) → OnLeave (멱등, 실제 멤버였을 때만 훅)

  // 교차-strand 진입(ADR-O: 대상 strand 로 post). Presence sweep 이 부른다 — 세션
  //   객체는 이미 파괴됐을 수 있어 SessionId 값만 받는다(World::PostLeave 규율).
  void PostLeave(SessionId member);

  // scoped 팬아웃(멤버 Group 위임, copy-per-recipient ADR-G).
  void Broadcast(const std::vector<uint8_t>& frame, SessionId except = 0);

 protected:
  // 게임-무지 훅. base = no-op. 서브클래스(game_logic)가 규칙을 채운다.
  //   ⚠️ 파라미터는 코어가 아는 타입만(SessionId·시간). 게임 타입 절대 미참조.
  virtual void OnEnter(SessionId /*member*/) {}
  virtual void OnLeave(SessionId /*member*/) {}
  virtual void OnTick(double /*dt_seconds*/) {}  // 틱 드라이버는 후속(현재 미구동)

 private:
  asio::strand<asio::any_io_executor> strand_;
  Group members_;        // Room 이 멤버 Group 을 소유(has-a, by value)
  const RoomId id_;
};

}  // namespace game::core
