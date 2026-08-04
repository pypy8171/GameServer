# 게임 무관 서버 코어 (네트워크 · 패킷 · 디스패치 · 로깅)
add_library(game_core STATIC
  config/server_config.cpp
  packet/packet.cpp
  serialize/serializer.cpp
  dispatch/dispatcher.cpp
  net/session.cpp
  net/session_registry.cpp
  net/server.cpp
  log/log.cpp
)

# include 루트를 src/ 로 두어  #include "core/net/session.h" 형태 사용.
target_include_directories(game_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(game_core PUBLIC ASIO_STANDALONE)
# spdlog 은 PUBLIC: log.h 가 <spdlog/spdlog.h> 를 노출하므로 소비자도 include 필요.
target_link_libraries(game_core PUBLIC asio::asio game_proto spdlog::spdlog)
