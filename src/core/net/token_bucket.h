#pragma once

namespace game::core
{

// 인바운드 패킷 rate-limit 용 토큰버킷(ADR-R). 순수 값 타입 — '시각'을 인자로 받아
//   asio·clock 과 분리해 단위 테스트한다. 세션 strand 안에서만 접근(락 없음).
//
//   capacity(=burst) 만큼 토큰을 담고 초당 refill_per_sec 로 채운다. 인바운드 패킷
//   1개가 1토큰을 소비. 토큰이 모자라면 거부 → 호출측이 세션을 강제 종료(플러딩 방어,
//   백프레셔와 같은 "남용은 종료" 규율). capacity<=0 이면 비활성(항상 허용).
//
//   버스트 허용 + 지속률 제한이 동시에 필요한 이유: 정상 클라도 짧은 순간 여러 패킷을
//   몰아 보낼 수 있어(입장 직후 등) 단순 '초당 N개 초과 즉시 차단'은 오탐이 크다.
//   버킷은 버스트를 capacity 로 흡수하고 지속 초과만 걸러낸다.
class TokenBucket
{
 public:
  TokenBucket() = default;
  TokenBucket(double capacity, double refill_per_sec)
      : capacity_(capacity), refill_per_sec_(refill_per_sec), tokens_(capacity)
  {
  }

  bool enabled() const { return capacity_ > 0.0; }

  // now_sec: 단조 시계(steady_clock)의 초 단위 값. 직전 호출 이상이어야 한다(역행 무시).
  //   경과분만큼 리필한 뒤 cost(기본 1) 토큰 소비를 시도한다. 성공 true / 부족 false.
  bool TryConsume(double now_sec, double cost = 1.0)
  {
    if (capacity_ <= 0.0)
    {
      return true;  // 비활성 — rate-limit 미설정
    }
    Refill(now_sec);
    if (tokens_ < cost)
    {
      return false;
    }
    tokens_ -= cost;
    return true;
  }

  double tokens() const { return tokens_; }

 private:
  void Refill(double now_sec)
  {
    if (last_sec_ < 0.0)
    {
      last_sec_ = now_sec;  // 첫 호출: 기준 시각만 잡고 리필 없음(버킷은 이미 가득)
      return;
    }
    const double dt = now_sec - last_sec_;
    if (dt <= 0.0)
    {
      return;  // 동일 시각/시계 역행 — 리필하지 않는다(음수 리필 금지)
    }
    last_sec_ = now_sec;
    tokens_ += dt * refill_per_sec_;
    if (tokens_ > capacity_)
    {
      tokens_ = capacity_;  // 상한 클램프(무한 축적 방지)
    }
  }

  double capacity_ = 0.0;
  double refill_per_sec_ = 0.0;
  double tokens_ = 0.0;
  double last_sec_ = -1.0;  // 음수 = 첫 호출 미도래(기준 시각 미설정)
};

}  // namespace game::core
