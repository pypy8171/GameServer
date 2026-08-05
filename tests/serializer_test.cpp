#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "chat.pb.h"
#include "core/serialize/serializer.h"

using namespace game::core;
using namespace game::proto;

// 바디 직렬화 → 역직렬화 왕복. ISerializer 심(seam) 뒤의 Protobuf 구현.
TEST(ProtobufSerializer, RoundTripsBody)
{
  ProtobufSerializer ser;
  ChatSayRequest say;
  say.set_text("hello");

  std::vector<uint8_t> bytes;
  ASSERT_TRUE(ser.SerializeBody(say, bytes));

  ChatSayRequest got;
  ASSERT_TRUE(ser.Parse(bytes.data(), static_cast<uint16_t>(bytes.size()), got));
  EXPECT_EQ(got.text(), "hello");
}

// 빈 바디는 proto3 에서 모든 필드 기본값 → 유효(파싱 성공). 경계 확인.
TEST(ProtobufSerializer, ParsesEmptyBodyAsDefaults)
{
  ProtobufSerializer ser;
  ChatSayRequest got;
  EXPECT_TRUE(ser.Parse(nullptr, 0, got));
  EXPECT_TRUE(got.text().empty());
}

// 손상된 바이트열은 파싱 실패 → false. 호출측이 이걸로 drop 판정(ADR-G).
TEST(ProtobufSerializer, FailsOnMalformedBytes)
{
  ProtobufSerializer ser;
  // wire-format 상 유효하지 않은 바이트(field 0 태그 = 잘못된 태그).
  const uint8_t garbage[] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ChatJoinRequest got;
  EXPECT_FALSE(ser.Parse(garbage, sizeof(garbage), got));
}
