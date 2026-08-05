#include "core/serialize/serializer.h"

namespace game::core
{

bool ProtobufSerializer::Parse(const uint8_t* data, uint16_t size,
                               google::protobuf::MessageLite& out) const
{
  // 빈 바디(size=0)는 proto3 에서 전 필드 기본값인 유효 메시지다.
  // nullptr 역참조를 피하려 선처리한다.
  if (size == 0)
  {
    out.Clear();
    return true;
  }
  return out.ParseFromArray(data, static_cast<int>(size));
}

bool ProtobufSerializer::SerializeBody(const google::protobuf::MessageLite& msg,
                                       std::vector<uint8_t>& out) const
{
  out.resize(msg.ByteSizeLong());
  return msg.SerializeToArray(out.data(), static_cast<int>(out.size()));
}

}  // namespace game::core
