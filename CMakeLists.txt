cmake_minimum_required(VERSION 3.21)
project(GameServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc)
  add_compile_definitions(_WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---- 의존성 (vcpkg manifest) ----
find_package(asio CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)

# ---- 서브프로젝트 ----
add_subdirectory(proto)   # game_proto : .proto -> C++ 생성
add_subdirectory(src)     # game_core + echo_server

enable_testing()
# add_subdirectory(tests) # 이후 마일스톤에서 gtest 연결
