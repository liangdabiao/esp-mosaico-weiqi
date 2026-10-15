# build_weiqi.ps1 —— weiqi-quest 固件构建脚本（固化工作流）。
#
# 背景：本机 ninja 的 .ninja_deps 会记录 GSP 生成文件的 /D:/ 风格路径，
# 增量构建时 FindFirstFileExA 报错（ninja: error ... syntax is incorrect）。
# 修复：每次构建前删除 .ninja_deps，让 ninja 全量重算依赖（约 90 秒）。
param(
    [switch]$Clean   # 额外清理整个 build 目录（配置也重跑）
)

$ErrorActionPreference = "Stop"
$workspace = "D:\esp-mo\esp-mosaico-vibe-main"
$project = "projects/weiqi-quest"
$build = "$workspace\projects\weiqi-quest\build"
$idf = "D:\esp-mo\esp-idf"
$script = "$workspace\submodule\esp-mosaico-utils\mosaico-tools\skills\idf-low-noise-build\scripts\idf_low_noise_build.py"

if ($Clean -and (Test-Path $build)) {
    Write-Host "[build] clean: removing $build"
    Remove-Item $build -Recurse -Force
}

if (Test-Path "$build\.ninja_deps") {
    Write-Host "[build] removing stale .ninja_deps (ninja /D:/ path bug)"
    Remove-Item "$build\.ninja_deps" -Force
}

Write-Host "[build] building $project ..."
Push-Location $workspace
try {
    $env:IDF_PATH = $idf
    python $script --project $project build
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "[build] OK"
