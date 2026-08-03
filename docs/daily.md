# DAILY

> 날짜별 작업 일지(journal). **최신 날짜가 맨 위**, 아래로 누적한다.
> "무엇을·왜·다음"을 하루 단위로 짧게. 상세 근거·로드맵은 [PROGRESS.md](PROGRESS.md)·[DESIGN_LOG.md](DESIGN_LOG.md).

---

## 2026-08-03

오늘 하루로 M0 → M0.5 → 관측성(파일 로그 + FATAL) 까지 수직 슬라이스를 관통하고,
지적받은 문서 정합성·로드맵 순서를 교정했다.

### 완료
- **M0 에코 서버 수직 슬라이스** — 연결→헤더/바디 수신→protobuf 파싱→반송. net/packet/dispatch 코어 관통. (커밋 `f4cca0f`)
- **M0.5 채팅 릴레이** — 클라 2개 접속→서로 릴레이. `SessionRegistry`·세션 신원·수명주기·동시성 수직 슬라이스. @reviewer 6불변식 전부 통과·🔴 0. gtest 배선(테스트 게이트 확보). (커밋 `ac65d31`에 파일 로그와 함께 포함)
- **파일 로그 시스템(M5 착수)** — `std::cout/cerr` 임시 로그를 spdlog 비동기 파일 로거로 전면 교체. `core/log` 파사드의 `LOG_*` 매크로만 사용(로거 교체 국소화). rotating 5MB×3, 콘솔 미러 옵션, `[시각][레벨][tid][파일:줄]` 패턴, Release 에서 `LOG_DEBUG` 컴파일 아웃.
- **FATAL 동기 flush(리뷰 B 해소)** — FATAL 전용 *동기* 로거를 async 로거와 같은 sink 위에 별도 생성. `LOG_FATAL` 은 호출 스레드에서 즉시 디스크 기록+flush 후 리턴 → 프로세스가 직후 죽어도 로그 보존. 회귀 테스트(`Log.FatalIsFlushedSynchronouslyBeforeShutdown`) 추가. **10/10 테스트 PASS**.

### 문서 정합성 교정 (지적 반영)
- **[D1, 차단]** CLAUDE.md 동시성 계약에 "오브젝트 풀 = MPMC" 추가 — 풀 코드 짜기 전에 계약 확정.
- **M3 표기 정직화** `🔶 일부 선반영` → `⬜ (설계만)`. 실제 코드는 `make_shared<Session>`, 풀 미구현임을 명시.
- **MA 계정생성 TOCTOU** — 정합성의 진짜 방어선은 "존재 확인"이 아니라 계정명 UNIQUE + INSERT duplicate-key 캐치→Nak 변환임을 설계에 못박음.
- **세션풀 + 동접 카운터 커플링** — 풀 고갈=접속거부를 관측 가능하게, 풀 구현(M3)과 최소 메트릭(동접 카운터)을 동반하도록 로드맵에 명시.
- **[D5, P2 남김]** ADR 표가 DESIGN_LOG(A~G)·PROGRESS(A~F)로 갈라짐 → `docs/DECISIONS.md` 승격 예정(별도 작업).

### 로드맵 결정 (재배치)
- **M2.5 설정 로딩 → M1 직후로 앞당김.** 하드코딩값(세션풀 5000·chat 1024B) 누적 차단, M2/M3 가 처음부터 config 참조.
- **엔디안 정규화(D6/F4) → M1 헤더 (de)serialize 헬퍼로 흡수.** no-op 래퍼를 흩뿌리는 대신 부채를 한 곳으로 특정 + 실제 정규화 동시 해결.

### 정직한 한계
- FATAL 동기 flush 는 `LOG_FATAL` 을 **거친** 임종만 보장. 세그폴트/`std::terminate` 등 매크로를 못 거치는 크래시, 그리고 FATAL 직전 async 큐에 남은 맥락 로그는 여전히 유실 가능 → 크래시 핸들러/전면 drain 은 후속(M5+, 측정 후 `/perf-debate`).

### 다음
- **M1 — ISerializer 추상화** (+ 헤더 직렬화 헬퍼에 엔디안 정규화 흡수) → **M2.5 설정 로딩**.
- P2: ADR A~G 를 `docs/DECISIONS.md` 로 승격(@planner/arch-doc).
