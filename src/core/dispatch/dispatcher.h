#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#include "core/packet/packet.h"
#include "core/serialize/serializer.h"

namespace game::core {

class Session;
using SessionPtr = std::shared_ptr<Session>;

// 원시 핸들러 시그니처: (세션, payload 시작 포인터, payload 길이).
// 저수준 등록(Register)용. 대부분의 핸들러는 아래 RegisterTyped 를 쓴다.
using PacketHandler =
    std::function<void(const SessionPtr&, const uint8_t*, uint16_t)>;

// 타입드 핸들러: 이미 역직렬화된 메시지를 받는다. 핸들러는 파싱을 신경 쓰지 않는다.
template <class TMsg>
using TypedHandler = std::function<void(const SessionPtr&, const TMsg&)>;

// 패킷 ID -> 핸들러 라우팅 테이블.
// 서버 기동 시 한 번 Register 하고, 이후 읽기 전용으로만 조회하므로 락 불필요.
// (자료구조: unordered_map 유지 — PacketId 가 방향대역+게임확장으로 sparse 라
//  dense array 는 부적합. ADR-H 참조.)
class Dispatcher {
 public:
  // 기본 직렬화는 Protobuf. 다른 포맷으로 교체하려면 커스텀 ISerializer 를 주입한다.
  // 주입 시 그 serializer 는 Dispatcher 보다 오래 살아야 한다(참조 보관).
  explicit Dispatcher(const ISerializer& serializer = DefaultSerializer());
  // 임시객체 주입 금지: serializer_ 가 참조 보관이라 임시면 생성 직후 댕글링(UAF).
  // rvalue 오버로드를 삭제해 컴파일 타임에 막는다. (lvalue 는 위 const& 로 바인딩)
  explicit Dispatcher(ISerializer&&) = delete;

  // 복사/이동 금지: RegisterTyped 람다가 this 를 캡처해 handlers_ 에 보관하므로
  // 사본은 원본 this 를 가리켜 원본 소멸 시 UAF. 참조 전달로만 쓴다.
  Dispatcher(const Dispatcher&) = delete;
  Dispatcher(Dispatcher&&) = delete;
  Dispatcher& operator=(const Dispatcher&) = delete;
  Dispatcher& operator=(Dispatcher&&) = delete;

  // 저수준 등록: 바디 파싱을 핸들러가 직접 한다.
  void Register(PacketId id, PacketHandler handler);

  // 타입드 등록: 등록 지점에서 TMsg 로 역직렬화한 뒤 핸들러를 호출한다.
  // 파싱 실패면 핸들러를 부르지 않고 drop(빌드 무관) + 관측 로그(ADR-G).
  template <class TMsg>
  void RegisterTyped(PacketId id, TypedHandler<TMsg> handler) {
    const uint16_t raw = static_cast<uint16_t>(id);
    handlers_[raw] = [this, raw, h = std::move(handler)](
                         const SessionPtr& s, const uint8_t* body,
                         uint16_t size) {
      TMsg msg;
      if (!serializer_.Parse(body, size, msg)) {
        OnParseFailed(raw);  // ADR-G: 빌드 무관 drop + 관측
        return;
      }
      h(s, msg);
    };
  }

  // 완성된 한 패킷([헤더+페이로드])을 라우팅한다.
  // 알 수 없는 id 는 무시(로그)한다.
  void Dispatch(const SessionPtr& session, const uint8_t* packet,
                uint16_t packet_size) const;

 private:
  // 기본 직렬화기(프로세스 수명 static). 참조 안정성 보장.
  static const ISerializer& DefaultSerializer();
  // 파싱 실패 시 관측 로그(구현은 .cpp — LOG/포맷 의존을 헤더에서 격리).
  void OnParseFailed(uint16_t id) const;

  const ISerializer& serializer_;
  std::unordered_map<uint16_t, PacketHandler> handlers_;
};

}  // namespace game::core
