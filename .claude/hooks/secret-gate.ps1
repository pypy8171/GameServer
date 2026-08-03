# secret-gate.ps1 — PreToolUse 결정론적 시크릿 차단 훅 (GameServer 판)
#
# git commit / git push 명령을 가로채, 스테이징된 변경(또는 push 예정 범위)에
# DB 접속정보·서버 운영키·세션 시크릿·개인정보가 섞여있으면 기계적으로 차단한다.
# @committer 에이전트의 보안 게이트를 사람 판단에 의존하지 않는 하드 백스톱으로 보강한다.
#
# 차단 시: exit 2 (PreToolUse를 deny, stderr 메시지가 Claude에게 전달됨)
# 통과/무관/에러 시: exit 0 (정상 워크플로우를 절대 깨뜨리지 않음 — fail-open)
#
# 출처: Quant/.claude/hooks/secret-gate.ps1 — 트레이딩(KIS 키/계좌번호) 표면을
#       게임 서버 표면(DB 커넥션스트링/운영키/세션시크릿)으로 교체.

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
# repo 루트는 한글 경로 리터럴을 피해 $PSScriptRoot에서 유도한다.
# (훅 위치: <repo>\.claude\hooks\secret-gate.ps1)
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

# ── commit/push 명령에만 작동 (그 외 명령은 즉시 통과) ─────────────────────
$isCommit = $cmd -match '\bgit\b[^\n]*\bcommit\b'
$isPush   = $cmd -match '\bgit\b[^\n]*\bpush\b'
if (-not ($isCommit -or $isPush)) { exit 0 }

# ── 검사 대상 텍스트 수집 ─────────────────────────────────────────────────
$added   = @()   # diff에서 추가된(+) 줄
$staged  = @()   # 스테이징된 파일명
$blocks  = @()   # 차단 사유 누적

try {
    if ($isCommit) {
        $staged = @(git -C $repo diff --cached --name-only 2>$null)
        $diff   = @(git -C $repo diff --cached -U0 2>$null)
    } else {
        $range = git -C $repo rev-parse --abbrev-ref '@{u}' 2>$null
        if ($LASTEXITCODE -eq 0 -and $range) {
            $staged = @(git -C $repo diff '@{u}..HEAD' --name-only 2>$null)
            $diff   = @(git -C $repo diff '@{u}..HEAD' -U0 2>$null)
        } else {
            $diff = @()   # 업스트림 미설정 → 내용 스캔 생략(파일추적 검사만)
        }
    }
} catch { $diff = @(); $staged = @() }

$added = $diff | Where-Object { $_ -like '+*' -and $_ -notlike '+++*' }

# ── (a) 위험 파일명이 스테이징/푸시 범위에 포함 ────────────────────────────
# 게임 서버 민감 파일: 접속정보 설정·인증서·키·환경파일. .example/.sample은 통과.
$dangerFile = '(^|/)(appsettings\.json|secrets\.json|server\.config|db\.config)$|connection.*\.(json|xml|config)$|\.pfx$|\.pem$|\.key$|\.p12$|(^|/)\.env$|id_rsa'
foreach ($f in $staged) {
    if ($f -match $dangerFile -and $f -notmatch '\.(example|sample|template)$') {
        $blocks += "위험 파일이 포함됨: $f"
    }
}

# ── (b) 추적되는 시크릿 파일 누수 (실값 파일이 git에 올라가 있는가) ─────────
try {
    $tracked = @(git -C $repo ls-files 2>$null) |
        Where-Object { $_ -match 'appsettings\.json$|secrets\.json$|(^|/)\.env$|\.pfx$|\.pem$|\.key$|\.p12$' -and $_ -notmatch '\.(example|sample|template)$' }
    foreach ($t in $tracked) { $blocks += "추적 중인 시크릿 파일 누수: $t (git rm --cached 필요)" }
} catch {}

# ── (c) 추가된 내용에서 실제 값 패턴 스캔 ──────────────────────────────────
# 플레이스홀더/필드명은 통과, 실제 값으로 보이는 것만 차단
$placeholder = 'YOUR_|CHANGEME|REDACTED|EXAMPLE|SAMPLE|<[^>]*>|x{4,}|\.\.\.|0{6,}|localhost|127\.0\.0\.1'

foreach ($line in $added) {
    $L = $line.TrimStart('+')

    # DB 커넥션 스트링: Password= / pwd= 뒤에 실값
    if ($L -match '(?i)(password|pwd)\s*=\s*([^;"''\s]{4,})') {
        $val = $Matches[2]
        if ($val -notmatch "(?i)$placeholder") {
            $blocks += "DB/접속 비밀번호로 보이는 값: $($L.Trim().Substring(0,[Math]::Min(60,$L.Trim().Length)))..."
        }
    }
    # 커넥션 스트링 통째 (Data Source= / Server= + User Id= 조합)
    if ($L -match '(?i)(data source|server)\s*=[^;]+;.*(user\s*id|uid)\s*=' -and $L -notmatch "(?i)$placeholder") {
        $blocks += "DB 커넥션 스트링으로 보이는 줄 발견"
    }
    # 일반 키/토큰/시크릿: key=value 형태에서 값이 20자 이상 실값일 때만
    if ($L -match '(?i)(api[_-]?key|access[_-]?token|auth[_-]?token|bearer|secret[_-]?key|jwt[_-]?secret|session[_-]?secret|client[_-]?secret|private[_-]?key|credential)\s*["'':=]+\s*["'']?([A-Za-z0-9+/=._-]{20,})') {
        $val = $Matches[2]
        if ($val -notmatch "(?i)$placeholder") {
            $blocks += "실값으로 보이는 시크릿 할당: $($L.Trim().Substring(0,[Math]::Min(60,$L.Trim().Length)))..."
        }
    }
    # PEM/개인키 블록
    if ($L -match '-----BEGIN') { $blocks += "PEM/키 블록 발견: $($L.Trim())" }
    # 개인정보: 주민등록번호 / 휴대폰 / 이메일(도메인 포함 실주소로 보이면)
    if ($L -match '\b\d{6}-[1-4]\d{6}\b') { $blocks += "주민번호로 보이는 패턴 발견" }
    if ($L -match '01[016789]-?\d{3,4}-?\d{4}') { $blocks += "휴대폰 번호로 보이는 패턴 발견" }
}

# ── 판정 ──────────────────────────────────────────────────────────────────
if ($blocks.Count -gt 0) {
    $uniq = $blocks | Select-Object -Unique
    $msg  = "[SECRET-GATE 차단] 커밋/푸시를 막았습니다. 시크릿/개인정보 의심 항목:`n"
    $msg += ($uniq | ForEach-Object { "  - $_" }) -join "`n"
    $msg += "`n조치: 해당 값을 제거하거나 .example/플레이스홀더로 치환 후 다시 시도하세요. "
    $msg += "오탐이면 사용자에게 확인받아 진행하세요. (이 게이트는 우회하지 마세요)"
    [Console]::Error.WriteLine($msg)
    exit 2   # PreToolUse deny
}

exit 0
