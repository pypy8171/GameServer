// accept 수용 제어(U-4 백오프 / S-3 접속상한, ADR-T)의 순수 판정 로직 테스트.
//   asio glue(steady_timer 재무장)와 분리해 '무엇을 할지'만 검증한다:
//   성공→다음수락, 취소→정지, 그 외 에러→백오프. 그리고 cap 판정.
#include <gtest/gtest.h>

#include <asio.hpp>

#include "core/net/accept_policy.h"

using game::core::AcceptAction;
using game::core::AcceptWithinCap;
using game::core::ClassifyAcceptResult;
using game::core::DrainingTail;
using game::core::ResolveDrainingReserve;

// 성공(ec 없음) → 곧바로 다음 연결 수락.
TEST(AcceptPolicy, SuccessAcceptsNext)
{
  EXPECT_EQ(ClassifyAcceptResult(std::error_code{}), AcceptAction::kAcceptNext);
}

// operation_aborted(acceptor 취소/종료) → 정지. 재무장하면 종료가 안 끝난다.
TEST(AcceptPolicy, AbortedStops)
{
  const std::error_code aborted = asio::error::make_error_code(
      asio::error::operation_aborted);
  EXPECT_EQ(ClassifyAcceptResult(aborted), AcceptAction::kStop);
}

// 그 외 에러(자원 고갈 등) → 즉시 재무장 대신 백오프 후 재시도(busy-spin 방지).
TEST(AcceptPolicy, OtherErrorBacksOff)
{
  const std::error_code refused = asio::error::make_error_code(
      asio::error::connection_refused);
  EXPECT_EQ(ClassifyAcceptResult(refused), AcceptAction::kBackoffThenAccept);
}

// 접속 상한: cap==0 은 무제한, 그 외엔 current<cap 일 때만 수용(경계 포함 확인).
TEST(AcceptPolicy, CapGatesByCurrentCount)
{
  EXPECT_TRUE(AcceptWithinCap(/*current=*/1000000, /*cap=*/0));  // 0=무제한
  EXPECT_TRUE(AcceptWithinCap(0, 2));   // 0<2
  EXPECT_TRUE(AcceptWithinCap(1, 2));   // 1<2
  EXPECT_FALSE(AcceptWithinCap(2, 2));  // 2>=2 — 한계 도달, 거부
  EXPECT_FALSE(AcceptWithinCap(3, 2));  // 초과
}

// draining tail 게이지(ADR-W §9-3): 풀이 붙든 슬롯(occupied) 중 라이브가 아닌
//   '반납 대기 꼬리' 수 = occupied - live. Close 가 registry 에서 즉시 빼도(라이브
//   하락) in-flight async op 이 refcount>0 로 슬롯을 붙들면 occupied > live 가 된다 —
//   그 차이가 곧 반납 지연/수명 누수의 조기 관측 신호다.
TEST(AcceptPolicy, DrainingTailIsOccupiedMinusLive)
{
  EXPECT_EQ(DrainingTail(/*occupied=*/10, /*live=*/7), 3u);  // 3개가 반납 대기 꼬리
  EXPECT_EQ(DrainingTail(1, 0), 1u);
}

// 전부 라이브면 꼬리는 0(반납 지연 없음).
TEST(AcceptPolicy, DrainingTailIsZeroWhenAllOccupiedAreLive)
{
  EXPECT_EQ(DrainingTail(5, 5), 0u);
  EXPECT_EQ(DrainingTail(0, 0), 0u);
}

// live > occupied 는 두 카운터를 서로 다른 순간에 읽는 레이스 스냅샷에서만 나온다
//   (occupied 읽은 뒤 새 세션이 Start→registry 등록되면 live 가 잠깐 앞선다). 음수
//   래핑(size_t 언더플로) 대신 0 으로 클램프해 게이지가 거대값으로 튀지 않게 한다.
TEST(AcceptPolicy, DrainingTailClampsToZeroWhenLiveExceedsOccupied)
{
  EXPECT_EQ(DrainingTail(3, 5), 0u);
  EXPECT_EQ(DrainingTail(0, 1), 0u);
}

// draining_reserve 결정(ADR-W, M3⑤ config 주입): 미설정(configured==0)이면 기본 =
//   max(256, max_sessions/20)=5%. 소규모는 바닥값 256 로 고정, 대규모는 5% 비례 확장.
TEST(AcceptPolicy, ResolveDrainingReserveDefaultsToFivePercentFloor256)
{
  EXPECT_EQ(ResolveDrainingReserve(/*configured=*/0, /*max_sessions=*/1000), 256u);
  EXPECT_EQ(ResolveDrainingReserve(0, 0), 256u);        // 극소 — 바닥값
  EXPECT_EQ(ResolveDrainingReserve(0, 5120), 256u);     // 5%=256 경계
  EXPECT_EQ(ResolveDrainingReserve(0, 100000), 5000u);  // 5% 비례
}

// 운영자가 명시(configured>0)하면 기본식을 무시하고 그 값을 그대로 존중한다 —
//   기본보다 작아도(다른 config knob 과 동일 규율, 운영자 책임).
TEST(AcceptPolicy, ResolveDrainingReserveHonorsConfiguredValue)
{
  EXPECT_EQ(ResolveDrainingReserve(/*configured=*/777, /*max_sessions=*/100000), 777u);
  EXPECT_EQ(ResolveDrainingReserve(1, 100000), 1u);  // 기본(5000)보다 작아도 존중
  EXPECT_EQ(ResolveDrainingReserve(4096, 0), 4096u);
}
