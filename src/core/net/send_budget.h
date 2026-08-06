#pragma once

#include <cstddef>

namespace game::core
{

// send 큐의 바이트 예산(백프레셔, ADR-E). strand 안에서만 접근 — 락 없음.
//   큐 누적 바이트 상한을 강제한다. 판정 단위는 '개수'가 아니라 '바이트 합':
//   16KB 1개와 소패킷 1만개는 OOM 위험이 전혀 다르다(바이트가 자원).
//   상한 초과 프레임은 '수용 거부' → 호출측이 세션을 강제 close(초과분 drop 아님:
//   게임 패킷 무음 유실은 클라 상태 손상. ADR-E 는 종료로 확정).
//
// asio 를 모르는 순수 값 타입 — 결정을 I/O 에서 분리해 단위 테스트한다.
class SendBudget
{
 public:
  explicit SendBudget(std::size_t cap_bytes) : cap_(cap_bytes) {}

  // 이 프레임을 큐에 넣어도 상한 이내인가. 이내면 큐잉분을 예산에 더하고 true.
  //   초과면 예산 불변(수용 안 함) + false → 호출측이 강제 close.
  //   (queued_ <= cap_, frame <= kMaxPacketSize 불변식이라 합산 오버플로 없음.)
  bool TryAdmit(std::size_t frame_bytes)
  {
    if (queued_ + frame_bytes > cap_)
    {
      return false;
    }
    queued_ += frame_bytes;
    return true;
  }

  // 전송 완료(큐에서 빠진) 프레임 바이트를 예산에서 회수한다.
  //   항상 TryAdmit 로 더한 만큼만 회수한다(호출측 불변식 — 언더플로 방지).
  void OnSent(std::size_t frame_bytes) { queued_ -= frame_bytes; }

  std::size_t queued() const { return queued_; }
  std::size_t cap() const { return cap_; }

 private:
  std::size_t cap_;
  std::size_t queued_ = 0;
};

// 설정된 send 큐 상한(cap)을 하한 min_cap(=단일 최대 프레임 크기)으로 끌어올린다.
//   cap < min_cap 이면 정상 최대 크기 패킷 1개조차 TryAdmit 에서 거부돼(빈 큐에서도
//   frame > cap) 세션이 첫 대형 전송에서 강제 close 된다 — 설정 실수가 정상 트래픽을
//   끊는 자해. cap 은 항상 최소 한 프레임은 담을 수 있어야 한다는 불변식을 여기서 강제.
//   순수 함수(asio·config 무관) — 주입값이라 min_cap 은 호출측이 kMaxPacketSize 로 넘긴다.
constexpr std::size_t ClampSendQueueCap(std::size_t cap, std::size_t min_cap)
{
  return cap < min_cap ? min_cap : cap;
}

}  // namespace game::core
