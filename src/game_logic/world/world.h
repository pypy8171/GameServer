#pragma once

#include <asio.hpp>
#include <cstddef>
#include <optional>
#include <unordered_map>

#include "core/net/session.h"  // SessionId, SessionPtr
#include "game.pb.h"
#include "game_logic/world/player_entity.h"

namespace game::logic
{

// 게임 월드: 입장한 플레이어 엔티티의 소유자.
//   [ADR-N] 엔티티는 SessionId 키로 World 가 소유한다. 코어 Session 은 엔티티를
//     모른다(게임 콘텐츠의 코어 누수 차단 — ADR-L 과 동일 규율).
//   [ADR-O] 엔티티 가변상태는 단일 World strand 안에서만 변경한다. 아래 순수
//     메서드(Enter/Leave/Count/Find)는 '월드 strand 위에서 호출' 전제이며,
//     Post* 래퍼가 그 strand 로 진입시킨다(다중 io 스레드에서의 맵 레이스 봉인).
class World
{
 public:
  explicit World(asio::io_context& io);

  // ---- 순수 코어 (월드 strand 위 호출 전제; 단일스레드 테스트에서 직접 호출) ----
  // 입장: 스폰 위치를 부여해 엔티티를 삽입하고 스냅샷을 돌려준다.
  //   이미 입장한 SessionId 면 nullopt(중복 입장 가드 — 재입장은 무시).
  std::optional<PlayerEntity> Enter(game::core::SessionId sid, PlayerId pid);
  // 퇴장: 엔티티 제거. 존재했으면 true, 없던 sid 면 false(멱등).
  bool Leave(game::core::SessionId sid);
  std::size_t Count() const;
  // 조회: 없으면 nullptr. 반환 포인터는 맵 변경 전까지만 유효(월드 strand 안).
  const PlayerEntity* Find(game::core::SessionId sid) const;

  // ---- strand 배선 (I/O glue) ----
  // 로그인 성공 콜백에서 호출. 월드 strand 로 진입해 입장시키고, 성공하면 해당
  //   세션에 WorldEnteredNotify(스폰)를 보낸다. s 를 shared_ptr 로 캡처해 입장
  //   처리 동안 세션 수명을 보장한다(콜백 중 파괴 → UAF 방지).
  void PostEnter(const game::core::SessionPtr& s, PlayerId pid);
  // 세션 종료 훅에서 호출. 월드 strand 로 진입해 퇴장 처리(SessionId 값 캡처 —
  //   세션 객체가 이미 파괴됐을 수 있으므로 id 만 넘긴다).
  void PostLeave(game::core::SessionId sid);

 private:
  asio::strand<asio::any_io_executor> strand_;
  std::unordered_map<game::core::SessionId, PlayerEntity> players_;  // 월드 strand 전용
};

// 엔티티 스냅샷 → WorldEnteredNotify 매핑(순수, 테스트 대상).
game::proto::WorldEnteredNotify MakeWorldEnteredNotify(const PlayerEntity& e);

}  // namespace game::logic
