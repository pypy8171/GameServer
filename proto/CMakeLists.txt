find_package(protobuf CONFIG REQUIRED)

# .proto -> .pb.cc/.pb.h 생성 후 정적 라이브러리로 묶음.
add_library(game_proto STATIC packet.proto chat.proto)
target_link_libraries(game_proto PUBLIC protobuf::libprotobuf)

# 생성 헤더(packet.pb.h)를 소비자가 include 할 수 있게 노출.
target_include_directories(game_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

protobuf_generate(TARGET game_proto)
