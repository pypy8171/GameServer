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

# ---- 설정파일 배포 헬퍼 ----
# 서버 실행파일 옆(출력 디렉터리)으로 config/<cfg_name> 을 복사한다.
#   코드는 bare 파일명을 CWD 기준으로 열고, VS/직접 실행 시 CWD 가 exe 폴더이므로
#   여기로 복사해두면 CWD 무관하게 설정 로딩이 성공한다.
#
#   [U-6] ALL 커스텀 타겟으로 매 빌드마다 배포한다 — 타겟 (재)링크 여부와 무관하게
#   항상 최신 cfg 를 exe 옆으로 복사한다(과거 POST_BUILD 는 재링크될 때만 돌아 cfg-only
#   수정이 누락됐다). copy_if_different 라 내용이 같으면 무동작 → 매 빌드 비용은 사실상 0.
#   DEPENDS ${target} 으로 exe 를 먼저 빌드해 출력 디렉터리($<TARGET_FILE_DIR>) 존재를 보장.
function(deploy_config target cfg_name)
  add_custom_target(${target}_deploy_cfg ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/config/${cfg_name}
            $<TARGET_FILE_DIR:${target}>/${cfg_name}
    DEPENDS ${target}
    COMMENT "Deploy config/${cfg_name} -> $<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endfunction()

# ---- 서브프로젝트 ----
add_subdirectory(proto)     # game_proto : .proto -> C++ 생성
add_subdirectory(src)       # game_core + echo_server + chat_server
add_subdirectory(examples)  # chat_client (검증 하네스)

enable_testing()
add_subdirectory(tests)     # gtest: core_tests
