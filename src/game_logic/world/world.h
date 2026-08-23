#pragma once

#include <asio.hpp>
#include <cstddef>
#include <optional>
#include <unordered_map>

#include "core/net/room.h"     // Room, RoomId (멤버 팬아웃 + strand 소유)
#include "core/net/session.h"  // SessionId, SessionPtr
#include "game.pb.h"
#include "game_logic/world/player_entity.h"

namespace game::logic
{

// 단일 전역 World 가 담기는 코어 Room 의 고정 id(ADR-X 결정 B: World=Room 1개 특수case).
//   다중 존/매치가 생기면 Room 마다 고유 id — 지금은 이 하나뿐이다.
inline constexpr game::core::RoomId kWorldRoomId = 1;

// 게임 월드: 입장한 플레이어 엔티티의 소유자 + 코어 Room 을 has-a 로 포함(ADR-X 결정 B).
//   [ADR-N] 엔티티는 SessionId 키로 World 가 소유한다. 코어 Session 은 엔티티를
//     모른다(게임 콘텐츠의 코어 누수 차단 — ADR-L 과 동일 규율).
//   [ADR-O] 엔티티 가변상태·멤버십 변이·팬아웃은 전부 **하나의 strand**(room_.strand())
//     안에서만 일어난다. 엔티티맵과 멤버 Group 을 같은 strand 로 묶어 "입장=Enter+Join,
//     퇴장=Leave+멤버해제"가 원자적으로 도는 걸 보장한다(교차-strand 레이스 봉인).
//   [ADR-X 결정 B] World 는 방들의 관리자(집합체)라 Room 을 **포함**한다(상속 아님 —
//     잎 시뮬레이션 단위만 상속, 두 층위 규율). 지금은 방 1개(=전역 월드)뿐이지만,
//     팬아웃은 코어 Room 의 멤버 Group 을 통과한다(전역 registry all_ 가 아니라). 이로써
//     WorldEntered 한 플레이어에게만 월드 알림이 가고(미인증 연결엔 안 감), 방 다중화 시
//     각 방이 자기 수신자만 갖는 구조로 자연 확장된다.
//   [W-1] Room 이 멤버를 강참조로 붙들어 풀 슬롯을 점유한다 → disconnect 시 PostLeave 가
//     엔티티 제거 **와** 방 멤버 해제를 같은 strand 에서 함께 처리해 슬롯을 반납한다.
//     한 세션이 여러 방에 걸치는 다중 멤버십 sweep(게임층 Presence)은 잎 상속 단계의 몫.
class World
{
 public:
  explicit World(asio::io_context& io);

  // World 가 포함하는 코어 Room(멤버 팬아웃 + strand). 배선층이 멤버십/브로드캐스트를
  //   방 스코프로 다루거나(향후 Presence 배선), strand 를 공유하는 데 쓴다.
  game::core::Room& room() { return room_; }

  // ---- 순수 코어 (월드 strand 위 호출 전제; 단일스레드 테스트에서 직접 호출) ----
  // 입장: 스폰 위치를 부여해 엔티티를 삽입하고 스냅샷을 돌려준다.
  //   이미 입장한 SessionId 면 nullopt(중복 입장 가드 — 재입장은 무시).
  std::optional<PlayerEntity> Enter(game::core::SessionId sid, PlayerId pid);
  // 퇴장: 엔티티 제거. 존재했으면 true, 없던 sid 면 false(멱등).
  bool Leave(game::core::SessionId sid);
  std::size_t Count() const;
  // 조회: 없으면 nullptr. 반환 포인터는 맵 변경 전까지만 유효(월드 strand 안).
  const PlayerEntity* Find(game::core::SessionId sid) const;
  // 이동: 입장한 세션 엔티티의 위치를 갱신하고 갱신된 스냅샷을 돌려준다.
  //   미입장(또는 이미 퇴장)한 sid 면 nullopt(유령 이동 가드 — 상태 변이 없음).
  std::optional<PlayerEntity> Move(game::core::SessionId sid, float x, float y);

  // ---- strand 배선 (I/O glue) ----
  // 로그인 성공 콜백에서 호출. 월드 strand 로 진입해 입장시키고(엔티티 삽입 +
  //   방 멤버 Join), 성공하면 해당 세션에 WorldEnteredNotify(스폰)를 보낸다. s 를
  //   shared_ptr 로 캡처해 입장 처리 동안 세션 수명을 보장한다(콜백 중 파괴 → UAF 방지).
  void PostEnter(const game::core::SessionPtr& s, PlayerId pid);
  // 세션 종료 훅에서 호출. 월드 strand 로 진입해 퇴장 처리(엔티티 제거 + 방 멤버
  //   해제 → 마지막 강참조 놓아 풀 슬롯 반납, W-1). SessionId 값 캡처 — 세션 객체가
  //   이미 파괴됐을 수 있으므로 id 만 넘긴다.
  void PostLeave(game::core::SessionId sid);

  // 이동 요청 핸들러에서 호출. 월드 strand 로 진입해 위치를 변이하고, 성공하면
  //   MoveNotify 를 방 멤버 전원(본인 포함)에 팬아웃한다(room_.Broadcast). sid 는
  //   세션 신원값(클라 주장 아님 — 스푸핑 차단). 팬아웃이 코어 Room 의 멤버 Group 을
  //   통과하므로 World 는 SessionRegistry 를 직접 알 필요가 없다(seam 이 Room 으로 흡수).
  void PostMove(game::core::SessionId sid, float x, float y);

 private:
  // Room 이 strand 를 소유한다 — World 는 그 strand 를 엔티티맵 변이에도 공유한다
  //   (엔티티 상태와 방 멤버십을 한 strand 로 묶어 원자성 확보, 위 클래스 주석 참조).
  game::core::Room room_;
  std::unordered_map<game::core::SessionId, PlayerEntity> players_;  // 월드 strand 전용
};

// 엔티티 스냅샷 → WorldEnteredNotify 매핑(순수, 테스트 대상).
game::proto::WorldEnteredNotify MakeWorldEnteredNotify(const PlayerEntity& e);

// 엔티티 스냅샷 → MoveNotify 매핑(순수, 테스트 대상). player_id 는 서버가 세션
//   신원에서 채운 값 — 클라 주장 아님(스푸핑 차단).
game::proto::MoveNotify MakeMoveNotify(const PlayerEntity& e);

}  // namespace game::logic
