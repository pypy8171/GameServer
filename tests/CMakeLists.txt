find_package(GTest CONFIG REQUIRED)

add_executable(core_tests
  server_config_test.cpp
  send_budget_test.cpp
  token_bucket_test.cpp
  heartbeat_test.cpp
  slot_pool_test.cpp
  session_pool_test.cpp
  accept_policy_test.cpp
  packet_test.cpp
  serializer_test.cpp
  dispatcher_test.cpp
  registry_test.cpp
  group_test.cpp
  group_pool_leak_test.cpp
  presence_sweep_test.cpp
  chat_relay_test.cpp
  log_test.cpp
  password_hash_test.cpp
  account_repository_test.cpp
  login_handler_test.cpp
  world_test.cpp
  world_room_membership_test.cpp
  game_entry_test.cpp
  session_backpressure_test.cpp
  session_policy_test.cpp
  session_start_test.cpp
)
target_link_libraries(core_tests PRIVATE
  game_core game_proto chat_logic game_logic
  GTest::gtest GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(core_tests)
