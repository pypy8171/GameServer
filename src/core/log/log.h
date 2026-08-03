#pragma once
// ------------------------------------------------------------
//  로깅 파사드 (얇은 래퍼)
//  구현은 spdlog(비동기)지만, 나머지 코드는 아래 LOG_* 매크로만 쓴다.
//    → 로거 교체(quill/자체 MPSC 구현)가 이 파일 하나로 국소화된다.
//
//  레벨:  DEBUG < INFO < WARN < ERROR < FATAL
//    DEBUG  개발 흐름 추적. Release 빌드에선 SPDLOG_ACTIVE_LEVEL 로 컴파일 제거(비용 0).
//    INFO   정상 운영 이벤트(접속/종료/릴레이).
//    WARN   예상 밖이나 복구됨·서버 정상(잘못된 패킷 드롭, 미등록 핸들러 등).
//    ERROR  한 작업이 실제 실패(세션/요청 단위). 서버는 생존.
//    FATAL  프로세스 지속 불가 → 종료(포트 바인드 실패 등).
//
//  성능: 비동기 백그라운드 스레드 1개가 파일 flush 를 전담한다.
//    io 스레드(producer N)는 큐에 넣기만 → consumer 1 = MPSC(CLAUDE.md 계약과 일치).
//    io 스레드가 파일 I/O 로 블로킹되지 않아 저지연을 유지(4-5k CCU 목표).
// ------------------------------------------------------------

#include <string>

#include <spdlog/spdlog.h>

namespace game::core::log {

enum class Level { kDebug, kInfo, kWarn, kError, kFatal };

// 비동기 로거 초기화. 스레드 시작 전에 한 번 호출한다.
//   file_path : 로그 파일 경로(부모 디렉터리 자동 생성). 크기 기반 로테이션.
//   min_level : 이 레벨 미만은 기록하지 않는다(런타임 필터).
//   console   : true 면 콘솔에도 미러링한다.
void Init(const std::string& file_path, Level min_level, bool console);

// 종료 시 호출 — 비동기 큐를 flush 하고 백그라운드 스레드를 정리한다.
//   호출 후에도 LOG_* 가 안전하도록 동기 stdout 기본 로거로 복원한다.
void Shutdown();

}  // namespace game::core::log

// ---- LOG_* 매크로 -------------------------------------------------
//   포맷은 fmt 스타일:  LOG_INFO("accepted {} (id={})", ep, id)
//   ⚠️ 사용자 입력은 반드시 '인자'로 넘길 것(포맷 문자열에 넣으면 {} 오해석).
//   컴파일타임 레벨 컷오프는 SPDLOG_ACTIVE_LEVEL(빌드 설정에서 주입)이 제어한다.
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_FATAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
