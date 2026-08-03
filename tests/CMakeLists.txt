find_package(GTest CONFIG REQUIRED)

add_executable(core_tests
  packet_test.cpp
  registry_test.cpp
  chat_relay_test.cpp
  log_test.cpp
)
target_link_libraries(core_tests PRIVATE
  game_core game_proto chat_logic
  GTest::gtest GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(core_tests)
