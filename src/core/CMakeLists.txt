# 게임 무관 서버 코어 (네트워크 · 패킷 · 디스패치)
add_library(game_core STATIC
  packet/packet.cpp
  dispatch/dispatcher.cpp
  net/session.cpp
  net/server.cpp
)

# include 루트를 src/ 로 두어  #include "core/net/session.h" 형태 사용.
target_include_directories(game_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(game_core PUBLIC ASIO_STANDALONE)
target_link_libraries(game_core PUBLIC asio::asio game_proto)
