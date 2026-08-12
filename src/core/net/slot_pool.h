#pragma once

#include <cassert>
#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace game::core
{

// 고정 상한 슬롯 인덱스 할당기(ADR-F/B/W) — 세션 풀 자유목록의 순수 코어.
//   Session·asio 를 모르는 자료구조라 단위 테스트한다. **mutex + intrusive LIFO**.
//
//   [0, capacity) 의 슬롯 인덱스를 발급/회수한다. 세션 바이트 저장소·placement-new·
//   shared_ptr deleter 배선은 이 위(통합 계층)의 관심사고, 여기선 "어느 슬롯이 비었나"
//   만 관리한다. 획득 실패는 **예외가 아니라 nullopt**(ADR-B 기법 B — accept 게이트가
//   예외 없이 거부 경로로 흐른다). LIFO 인 이유: 최근 반납 슬롯을 먼저 재발급하면
//   캐시가 따뜻하다(ADR-F). 자유목록은 슬롯 셀(next_)에 링크를 심어 op 당 무할당.
//
//   MPMC 접근(다수 io 스레드가 alloc/free 양쪽) 안전성은 mutex 로 확보한다 —
//   락프리 전환은 경합이 실측될 때만(§3 open, 선제 도입 금지).
class SlotPool
{
 public:
  explicit SlotPool(std::size_t capacity)
      : capacity_(capacity), next_(capacity)
  {
    // 초기 자유목록: 0 -> 1 -> ... -> capacity-1 -> kNil, 머리=0.
    //   (초기 획득 순서는 무의미 — 반납 후 LIFO 만 계약이다.)
    for (std::size_t i = 0; i + 1 < capacity_; ++i)
    {
      next_[i] = i + 1;
    }
    if (capacity_ > 0)
    {
      next_[capacity_ - 1] = kNil;
      free_head_ = 0;
    }
  }

  // 자유 슬롯 인덱스 1개 획득. 상한 도달 시 nullopt(예외 없음).
  std::optional<std::size_t> TryAcquire()
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (free_head_ == kNil)
    {
      return std::nullopt;  // 상한 도달 — 자유 슬롯 없음
    }
    const std::size_t index = free_head_;
    free_head_ = next_[index];  // 머리를 다음 자유로 전진
    ++occupied_;
    return index;
  }

  // 인덱스 반납. LIFO 로 자유목록 앞에 놓여 다음 획득 시 먼저 재발급된다.
  //
  //   전제조건(호출측 계약): index 는 [0,capacity) 범위의 **현재 획득된** 슬롯이며
  //   정확히 1회만 반납한다. 위반 시(범위 밖=OOB UB, 이중 반납=자유목록 사이클→동일
  //   슬롯 이중발급, 미획득 반납=occupied 언더플로) 미정의. 통합 계층은 shared_ptr
  //   deleter(refcount 0 에서 1회 발화)로 이 계약을 구조적으로 보장한다(ADR-B). 아래
  //   assert 는 디버그 빌드에서 배선 버그(재시딩류)를 조기에 잡는 방어선(릴리즈 무비용).
  void Release(std::size_t index)
  {
    std::lock_guard<std::mutex> lock(mu_);
    assert(index < capacity_ && "SlotPool::Release: index out of range");
    assert(occupied_ > 0 && "SlotPool::Release: release without matching acquire");
    next_[index] = free_head_;  // 반납 슬롯을 머리에 push(LIFO)
    free_head_ = index;
    --occupied_;
  }

  std::size_t capacity() const { return capacity_; }

  std::size_t occupied() const
  {
    std::lock_guard<std::mutex> lock(mu_);
    return occupied_;
  }

 private:
  static constexpr std::size_t kNil = ~static_cast<std::size_t>(0);

  mutable std::mutex mu_;
  std::size_t capacity_;
  std::size_t free_head_ = kNil;   // 자유목록 머리(kNil = 빔)
  std::size_t occupied_ = 0;
  std::vector<std::size_t> next_;  // next_[i] = i 가 자유일 때 다음 자유 인덱스
};

}  // namespace game::core
