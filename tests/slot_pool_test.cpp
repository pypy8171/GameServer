// SlotPool: 세션 풀 자유목록의 순수 코어(ADR-F/B/W). Session·asio 무관 — 고정상한
//   슬롯 인덱스 할당기. 급소: (1) cap 개까지 획득·초과는 nullopt(예외 아님),
//   (2) 반납 후 재획득, (3) LIFO(최근 반납=먼저 재발급, 캐시웜), (4) occupied 정확,
//   (5) 멀티스레드 동시 acquire/release 에서 동일 슬롯 이중발급 0(MPMC 안전 — ADR-F).
#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "core/net/slot_pool.h"

using game::core::SlotPool;

// 빈 풀에서 cap 개를 전부 획득하면 서로 다른 인덱스가 나오고, 초과 획득은 nullopt.
TEST(SlotPool, AcquiresUpToCapacityThenNullopt)
{
  SlotPool pool(3);
  EXPECT_EQ(pool.capacity(), 3u);
  auto a = pool.TryAcquire();
  auto b = pool.TryAcquire();
  auto c = pool.TryAcquire();
  ASSERT_TRUE(a && b && c);
  EXPECT_NE(*a, *b);
  EXPECT_NE(*b, *c);
  EXPECT_NE(*a, *c);
  EXPECT_LT(*a, 3u);
  EXPECT_LT(*b, 3u);
  EXPECT_LT(*c, 3u);
  EXPECT_FALSE(pool.TryAcquire());  // 상한 초과 — nullopt(예외 아님)
  EXPECT_EQ(pool.occupied(), 3u);
}

// 반납하면 슬롯이 자유목록에 돌아와 다시 획득할 수 있다(점유 수도 회복).
TEST(SlotPool, ReleaseReturnsSlotForReuse)
{
  SlotPool pool(1);
  auto a = pool.TryAcquire();
  ASSERT_TRUE(a);
  EXPECT_FALSE(pool.TryAcquire());  // cap=1 소진
  EXPECT_EQ(pool.occupied(), 1u);
  pool.Release(*a);
  EXPECT_EQ(pool.occupied(), 0u);
  auto b = pool.TryAcquire();  // 반납분 재획득
  ASSERT_TRUE(b);
  EXPECT_EQ(*b, *a);  // 유일 슬롯이므로 동일 인덱스
}

// LIFO: 마지막에 반납한 슬롯이 다음 획득에서 먼저 나온다(최근 반납=캐시웜, ADR-F).
TEST(SlotPool, ReusesMostRecentlyReleasedFirst)
{
  SlotPool pool(3);
  auto a = pool.TryAcquire();
  auto b = pool.TryAcquire();
  auto c = pool.TryAcquire();
  ASSERT_TRUE(a && b && c);
  pool.Release(*a);  // 자유: a
  pool.Release(*b);  // 자유: b -> a (LIFO 머리=b)
  EXPECT_EQ(*pool.TryAcquire(), *b);  // 가장 최근 반납(b) 먼저
  EXPECT_EQ(*pool.TryAcquire(), *a);  // 그다음 a
  EXPECT_FALSE(pool.TryAcquire());    // c 는 여전히 점유 중
}

// 멀티스레드: 여러 스레드가 동시에 acquire/release 해도 같은 슬롯이 동시에 둘에게
//   발급되지 않는다(MPMC 접근 안전성 — ADR-F). 각 스레드는 획득한 슬롯을 자기만
//   쥐었는지 플래그로 확인 후 반납. 종료 후 전부 자유여야 한다.
TEST(SlotPool, ConcurrentAcquireReleaseNoDoubleIssue)
{
  constexpr std::size_t kCap = 8;
  SlotPool pool(kCap);
  std::atomic<bool> in_use[kCap];
  for (auto& f : in_use)
  {
    f.store(false);
  }
  std::atomic<bool> double_issue{false};

  auto worker = [&] {
    for (int i = 0; i < 20000; ++i)
    {
      auto idx = pool.TryAcquire();
      if (!idx)
      {
        continue;  // 상한 도달 — 경합 상 정상
      }
      // 이 슬롯은 나 혼자 쥐어야 한다. 이미 true 면 다른 스레드에 이중발급된 것.
      if (in_use[*idx].exchange(true))
      {
        double_issue = true;
      }
      // 탐지창을 벌린다: 소유 상태를 잠깐 유지하며 여러 번 확인해야, 실제 이중발급이
      //   나는 버그가 있을 때 다른 스레드의 exchange 와 겹칠 확률이 생긴다(즉시 해제
      //   하면 창이 0 이라 헛 테스트). 확인 중 플래그가 뒤집히면 그 자체가 이중발급.
      for (int k = 0; k < 8; ++k)
      {
        if (!in_use[*idx].load())
        {
          double_issue = true;
        }
        std::this_thread::yield();
      }
      in_use[*idx].store(false);
      pool.Release(*idx);
    }
  };

  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t)
  {
    ts.emplace_back(worker);
  }
  for (auto& t : ts)
  {
    t.join();
  }

  EXPECT_FALSE(double_issue) << "동일 슬롯이 동시에 두 스레드에 발급됐다";
  EXPECT_EQ(pool.occupied(), 0u) << "종료 후 모든 슬롯이 자유여야 한다";
}
