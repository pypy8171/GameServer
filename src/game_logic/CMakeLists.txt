# 게임 서버측 도메인 로직 — 코어(네트워크/디스패치)와 분리된 "게임이 아는 것".
#   로그인/계정(신원 확립), 이후 World·엔티티가 여기 쌓인다. 코어는 이 lib 를 모른다
#   (의존 방향: game_logic → game_core, 역방향 금지 — 게임 콘텐츠의 코어 누수 차단).
add_library(game_logic STATIC
  account/password_hash.cpp
  account/in_memory_account_repository.cpp
  login/login_handlers.cpp
  presence/presence.cpp
  world/world.cpp
  game/game_handlers.cpp
)

# game_core 를 PUBLIC 링크하면 src/ include 루트(#include "game_logic/..."),
#   Dispatcher·Session·packet·proto 가 소비자(테스트/실행파일)에 전파된다.
target_link_libraries(game_logic PUBLIC game_core game_proto)
