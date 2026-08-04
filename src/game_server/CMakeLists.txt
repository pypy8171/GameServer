# game_server 실행 셸(MG). 게임 핸들러(로그인/이동)는 MG-⑤+ 에서 별도
# 라이브러리(game_logic)로 분리해 실행파일·통합테스트가 공유하게 한다.
add_executable(game_server main.cpp)
target_link_libraries(game_server PRIVATE game_core game_proto game_logic)
