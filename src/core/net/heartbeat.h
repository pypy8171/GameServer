#pragma once

namespace game::core
{

// heartbeat 틱에서 취할 동작(ADR-V). 순수 판정의 결과 — I/O 는 호출측(Session)이 한다.
enum class HeartbeatAction
{
  Ping,   // 살아있음 — ping 프로브 발신 후 타이머 재무장
  Close,  // 마지막 활동 이후 timeout 초과 — 죽은 세션으로 간주해 강제 종료
};

// 세션 생존성 판정(ADR-V, ADR-S 의 능동 짝). 순수 값 타입 — asio·clock 을 모르고
//   '시각'을 인자로 받아 단위 테스트한다. 세션 strand 안에서만 접근(락 없음).
//
//   생존 신호 = any-recv: 모든 수신 활동(pong 포함)이 데드라인을 리셋한다(OnActivity).
//   heartbeat 틱마다 Classify 로 판정 — 마지막 활동 이후 경과가 timeout 을 넘으면 Close,
//   아니면 Ping(유휴 클라에 트래픽을 유도하는 능동 프로브이자 NAT keepalive).
//   ping 주기(틱 간격)는 이 순수 모듈의 관심사가 아니다 — steady_timer 주기가 정한다.
//   timeout<=0 이면 비활성(기본). ping 발신 자체가 활동은 아니다(수신만 리셋).
class Heartbeat
{
 public:
  Heartbeat() = default;
  explicit Heartbeat(double timeout_sec) : timeout_sec_(timeout_sec) {}

  bool enabled() const { return timeout_sec_ > 0.0; }
  double timeout_sec() const { return timeout_sec_; }

  // 수신 활동 기록. now_sec = 단조 시계(steady_clock)의 초 단위 값.
  void OnActivity(double now_sec) { last_activity_sec_ = now_sec; }

  // heartbeat 틱에서 호출. 마지막 활동 이후 경과 > timeout 이면 Close, 아니면 Ping.
  //   활동 미기록(음수) 상태에서는 아직 죽음을 단정할 수 없어 Ping(무장 시 OnActivity
  //   로 기준 시각을 잡으므로 정상 경로에서 이 분기는 도달하지 않는다).
  HeartbeatAction Classify(double now_sec) const
  {
    if (last_activity_sec_ >= 0.0 && now_sec - last_activity_sec_ > timeout_sec_)
    {
      return HeartbeatAction::Close;
    }
    return HeartbeatAction::Ping;
  }

 private:
  double timeout_sec_ = 0.0;
  double last_activity_sec_ = -1.0;  // 음수 = 활동 미기록(무장 시 OnActivity 로 설정)
};

}  // namespace game::core
