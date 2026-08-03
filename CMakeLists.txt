cmake_minimum_required(VERSION 3.21)
project(GameServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc)
  add_compile_definitions(_WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# 로그 레벨 컴파일타임 컷오프: Debug 는 DEBUG 부터, Release 는 INFO 부터 컴파일.
#   → Release 빌드에서 LOG_DEBUG(...) 는 코드에서 완전히 사라진다(비용 0, 문자열도 미포함).
add_compile_definitions(
  $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
  $<$<NOT:$<CONFIG:Debug>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)

# ---- 의존성 (vcpkg manifest) ----
find_package(asio CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ---- 서브프로젝트 ----
add_subdirectory(proto)     # game_proto : .proto -> C++ 생성
add_subdirectory(src)       # game_core + echo_server + chat_server
add_subdirectory(examples)  # chat_client (검증 하네스)

enable_testing()
add_subdirectory(tests)     # gtest: core_tests
