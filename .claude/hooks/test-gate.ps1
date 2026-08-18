# test-gate.ps1 — PreToolUse 결정론적 테스트 게이트 훅 (GameServer 판)
#
# git commit 명령을 가로채, 스테이징된 변경에 코드/빌드 영향 파일이 있으면
# build.ps1 -Test (core_tests 증분 빌드 + 실행)를 돌려 그린이 아닐 때 커밋을 막는다.
# CLAUDE.md 의 "테스트 게이트가 코드로 존재할 때만 자율 루프" 규율을, /dev-loop 를
# 사람이 잊어도 무너지지 않는 하드 백스톱으로 보강한다(secret-gate 와 동일 철학).
#
# 차단 시: exit 2 (PreToolUse deny, stderr 가 Claude 에게 전달됨)
# 통과/무관/게이트자체오류 시: exit 0 (정상 워크플로우를 깨뜨리지 않음 — fail-open)
#
# 설계 메모:
#  - push 는 게이트하지 않는다(커밋 시점에 이미 초록임을 보장하므로 중복).
#  - 코드/빌드 무관 커밋(순수 설정 외)엔 전체 빌드를 돌리지 않아 지연을 줄인다.
#    검사 대상: src/ tests/ proto/ 아래 소스, 그리고 CMake/preset 파일.
#  - 빌드/테스트 실패는 곧 "커밋 금지" 사유이므로 build.ps1 의 비정상 종료(throw 포함)를
#    그대로 차단 신호로 쓴다. 게이트 자체가 실행조차 못하면(예외) fail-open.

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
# repo 루트는 한글 경로 리터럴을 피해 $PSScriptRoot 에서 유도한다.
# (훅 위치: <repo>\.claude\hooks\test-gate.ps1)
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

# ── stdin(JSON)에서 실행될 명령 추출 ──────────────────────────────────────
try {
    $raw = [Console]::In.ReadToEnd()
    $payload = $raw | ConvertFrom-Json
    $cmd = [string]$payload.tool_input.command
} catch {
    exit 0   # 페이로드 파싱 실패 → 통과
}
if ([string]::IsNullOrWhiteSpace($cmd)) { exit 0 }

# ── git commit 명령에만 작동 (push/그 외는 즉시 통과) ──────────────────────
$isCommit = $cmd -match '\bgit\b[^\n]*\bcommit\b'
if (-not $isCommit) { exit 0 }

# ── 스테이징된 변경이 빌드에 영향 있는지 판정 (없으면 스킵) ─────────────────
try {
    $staged = @(git -C $repo diff --cached --name-only 2>$null)
} catch {
    exit 0   # git 조회 실패 → fail-open
}
if ($staged.Count -eq 0) { exit 0 }   # amend 등으로 스테이징 없음 → 통과

$codeTouch = $false
foreach ($f in $staged) {
    if ($f -match '^(src|tests|proto)/.*\.(cpp|cc|cxx|h|hpp|hxx|proto)$' -or
        $f -match '(^|/)CMakeLists\.txt$' -or
        $f -match '(^|/)CMakePresets\.json$' -or
        $f -match '\.cmake$') {
        $codeTouch = $true
        break
    }
}
if (-not $codeTouch) { exit 0 }   # 코드/빌드 무관 커밋 → 테스트 스킵

# ── build.ps1 -Test 실행 (증분 빌드 + core_tests) ──────────────────────────
$buildScript = Join-Path $repo 'scripts\build.ps1'
if (-not (Test-Path $buildScript)) { exit 0 }   # 빌드 스크립트 없음 → fail-open

try {
    # 출력은 콘솔에 흘리되(진행상황 가시성), 종료코드로만 판정한다.
    & $buildScript -Test *>&1 | Out-Null
    $code = $LASTEXITCODE
} catch {
    # build.ps1 이 throw(빌드/구성 실패) → 커밋 금지 신호로 취급
    $code = 1
}

if ($code -ne 0) {
    $msg  = "[TEST-GATE 차단] core_tests 가 그린이 아니라 커밋을 막았습니다 (build.ps1 -Test exit=$code).`n"
    $msg += "조치: 'scripts\build.ps1 -Test' 를 직접 돌려 빌드/테스트 실패를 고친 뒤 다시 커밋하세요.`n"
    $msg += "이 게이트는 /dev-loop 규율의 하드 백스톱입니다 — 우회하지 마세요. (오탐이면 사용자 확인 후 진행)"
    [Console]::Error.WriteLine($msg)
    exit 2   # PreToolUse deny
}

exit 0
