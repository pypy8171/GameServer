add_executable(chat_client main.cpp)
target_link_libraries(chat_client PRIVATE game_core game_proto)
