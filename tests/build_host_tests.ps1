# build_host_tests.ps1 -- Weiqi Quest host logic unit tests (zig cc / clang).
#
# Compiles and runs 6 pure-logic unit tests against the SAME C code that runs on
# device (projects/weiqi-quest/main/wq):
#   test_wq_engine    7 rule checks + 154-level correct-path replay
#   test_wq_content   chapter splits / required fields / 7 chapter titles
#   test_wq_progress  2bit stars / serialize roundtrip / corrupt-blob rejection
#   test_wq_session   nav / judge / wrong-reset + full 154-level playthrough (incl touch API)
#   test_wq_wrap      CJK wrap hard guarantee
#   test_wq_brief_layout  BRIEF 题面 2 倍字号的版式预算（行数 / 缓冲 / 滚动上限）
#   test_wq_ai        retained AI asset (not wired to UI): capture / ko / move pick
#
# Does NOT compile wq_store.c (ESP-IDF NVS) or device stubs; these tests only
# touch the pure-logic layer.
# Usage:  powershell -ExecutionPolicy Bypass -File build_host_tests.ps1
# Exit code is non-zero if any test fails.

$ErrorActionPreference = "Stop"

$TestsDir   = $PSScriptRoot
$ProjectDir = Split-Path -Parent $TestsDir
$MainWq     = Join-Path $ProjectDir "main\wq"
$BuildDir   = Join-Path $TestsDir "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# zig toolchain (same one mosaico.py game sim uses).
$zig = "C:\Users\49707\AppData\Local\Microsoft\WinGet\Packages\zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe\zig-x86_64-windows-0.17.0\zig.exe"
if (-not (Test-Path $zig)) {
    $cc = Get-Command cc -ErrorAction SilentlyContinue
    if ($cc) { $zig = $cc.Source } else { throw "zig.exe not found" }
}

$cases = @(
    @{ name = "test_wq_engine";   src = "test_wq_engine.c";   deps = @("wq_engine.c", "wq_content.c");   depDir = $MainWq; extraDeps = @(); extraDepDir = $MainWq },
    @{ name = "test_wq_content"; src = "test_wq_content.c"; deps = @("wq_content.c");                    depDir = $MainWq; extraDeps = @(); extraDepDir = $MainWq },
    @{ name = "test_wq_progress";  src = "test_wq_progress.c";  deps = @("wq_progress.c", "wq_content.c"); depDir = $MainWq; extraDeps = @(); extraDepDir = $MainWq },
    @{ name = "test_wq_session";   src = "test_wq_session.c";  deps = @("wq_session.c", "wq_engine.c", "wq_content.c"); depDir = $MainWq; extraDeps = @(); extraDepDir = $MainWq },
    @{ name = "test_wq_wrap";      src = "test_wq_wrap.c";      deps = @("wq_wrap.c", "wq_content.c");  depDir = $MainWq; extraDeps = @(); extraDepDir = $MainWq },
    @{ name = "test_wq_brief_layout"; src = "test_wq_brief_layout.c"; deps = @("wq_wrap.c", "wq_content.c"); depDir = $MainWq; extraDeps = @(); extraDepDir = $MainWq },
    @{ name = "test_wq_ai";        src = "test_wq_ai.c";        deps = @("wq_ai.c");                     depDir = $TestsDir; extraDeps = @("wq_engine.c", "wq_content.c"); extraDepDir = $MainWq }
)

$failCount = 0

foreach ($c in $cases) {
    Write-Host "=== $($c.name) ==="
    $exe = Join-Path $BuildDir ($c.name + ".exe")
    $srcPath = Join-Path $TestsDir $c.src
    $depPaths = @($c.deps | ForEach-Object { Join-Path $c.depDir $_ })
    if ($c.extraDeps.Count -gt 0) {
        $depPaths += ($c.extraDeps | ForEach-Object { Join-Path $c.extraDepDir $_ })
    }

    $compileArgs = @("cc", "-std=c11", "-Wall", "-Wextra",
                     "-I", $MainWq, "-I", $TestsDir,
                     $srcPath) + $depPaths + @("-o", $exe)
    & $zig @compileArgs 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "COMPILE FAILED: $($c.name)"
        $failCount++
        continue
    }

    & $exe 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "RUN FAILED: $($c.name) (exit $LASTEXITCODE)"
        $failCount++
    } else {
        Write-Host "PASS: $($c.name)"
    }
}

Write-Host ""
if ($failCount -eq 0) {
    Write-Host "ALL HOST TESTS GREEN"
    exit 0
} else {
    Write-Host ("{0} host test(s) FAILED" -f $failCount)
    exit 1
}
