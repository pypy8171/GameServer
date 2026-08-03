# 채팅 릴레이 로직(핸들러) — chat_server 실행파일과 통합테스트가 공유.
add_library(chat_logic STATIC chat_handlers.cpp)
target_link_libraries(chat_logic PUBLIC game_core game_proto)

add_executable(chat_server main.cpp)
target_link_libraries(chat_server PRIVATE chat_logic)
