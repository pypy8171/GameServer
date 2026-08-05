add_executable(echo_server main.cpp)
target_link_libraries(echo_server PRIVATE game_core game_proto)
deploy_config(echo_server echo_server.cfg)  # cfg 를 exe 옆으로 배포
