#requires -Version 5.1
# GameServer 빌드 래퍼 — cmake PATH 부재 + 한글 %TEMP% LNK1104 를 흡수한다.
#
# cmake 은 PATH -> vswhere(VS 설치경로) -> VS2022 번들 경로 순으로 자동 해석하므로
# VS 버전이 올라가도 깨지지 않는다. 한글 사용자폴더 %TEMP% 로 인한 MSVC LNK1104 도
# 한 진입점에서 회피한다.
#
# 사용 예:
#   .\scripts\build.ps1                       # core_tests(Debug) 증분 빌드
#   .\scripts\build.ps1 -Target echo_server   # 특정 타겟
#   .\scripts\build.ps1 -Test                 # 빌드 후 core_tests 실행
#   .\scripts\build.ps1 -Config Release -Target ALL_BUILD
#   .\scripts\build.ps1 -Configure            # 프리셋 재구성(최초 1회/캐시 손상 시)
[CmdletBinding()]
param(
  [string]$Target = 'core_tests',
  [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Config = 'Debug',
  [switch]$Test,
  [switch]$Configure
)
$ErrorActionPreference = 'Stop'
# scripts/ 디렉터리 = 이 스크립트의 위치. $PSScriptRoot 가 비면($null) 호출 방식에
# 따라 MyInvocation 로 폴백한다(& 로 간접 호출 시 스코프에 따라 빌 수 있음).
$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$root = Split-Path -Parent $scriptDir             # scripts/ 의 부모 = repo 루트
$build = Join-Path $root 'build'

# 1) 한글 %TEMP% -> MSVC LNK1104 회피 (CLAUDE.md 빌드 메모)
$tmp = 'C:\build_tmp'
if (-not (Test-Path $tmp)) { New-Item -ItemType Directory $tmp | Out-Null }
$env:TEMP = $tmp
$env:TMP = $tmp

# 2) cmake 해석: PATH -> vswhere -> 알려진 VS2022 번들 경로
function Resolve-CMake {
  $onPath = Get-Command cmake -ErrorAction SilentlyContinue
  if ($onPath) { return $onPath.Source }
  $pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
  $vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -property installationPath
    if ($vs) {
      $c = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
      if (Test-Path $c) { return $c }
    }
  }
  throw 'cmake.exe 를 찾지 못했습니다. VS2022 C++ CMake tools 또는 standalone CMake 설치가 필요합니다.'
}
$cmake = Resolve-CMake
Write-Host "[build] cmake  = $cmake" -ForegroundColor DarkGray
Write-Host "[build] TEMP   = $env:TEMP" -ForegroundColor DarkGray
Write-Host "[build] target = $Target ($Config)" -ForegroundColor DarkGray

# 3) 구성 — 캐시 없거나 -Configure 시에만
$cacheFile = Join-Path $build 'CMakeCache.txt'
if ($Configure -or -not (Test-Path $cacheFile)) {
  Write-Host '[build] configuring (windows-msvc preset)...' -ForegroundColor Cyan
  & $cmake --preset windows-msvc
  if ($LASTEXITCODE -ne 0) { throw "configure 실패 (exit $LASTEXITCODE)" }
}

# 4) 빌드
& $cmake --build $build --config $Config --target $Target
if ($LASTEXITCODE -ne 0) { throw "빌드 실패 (exit $LASTEXITCODE)" }

# 5) 테스트 실행
if ($Test) {
  $exe = Join-Path $build "tests\$Config\core_tests.exe"
  if (-not (Test-Path $exe)) { throw "테스트 실행파일 없음: $exe (먼저 -Target core_tests 빌드)" }
  Write-Host "[build] running tests: $exe" -ForegroundColor Cyan
  & $exe
  exit $LASTEXITCODE
}
