#!/usr/bin/env bash
# weiqi-quest 静态校验：生成物必须与内容源同步，宿主逻辑测试必须全绿。
#
# 注意：本文件此前是从别的工程（FoloToy AI Passport）整份拷来的，引用的
# main/ui_pixel_math.c、components/bsp/、tools/check_repo.py 在本工程都不存在，
# 跑起来必然失败。这里按本工程真实的目录结构重写，只做三件事：
#   1) 内容表（wq_content.c/h）与题库源（levels.txt）同步
#   2) 字库（wq_cjk_glyph_data.h）与内容 + 界面文案同步
#   3) 宿主逻辑测试全绿
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

PY="${PYTHON:-}"
if [[ -z "$PY" ]]; then
    for cand in python3 python py; do
        if command -v "$cand" >/dev/null 2>&1; then PY="$cand"; break; fi
    done
fi
if [[ -z "$PY" ]]; then
    echo "ERROR: 找不到 python 解释器（可用 PYTHON=... 指定）" >&2
    exit 1
fi

echo "== 内容表与题库同步 =="
"$PY" tools/weiqi/gen_content.py --check

echo "== 字库与内容/界面文案同步 =="
"$PY" tools/weiqi/gen_cjk_font.py --check

echo "== 宿主逻辑测试（围棋规则 / 题库 / 状态机 / 存档 / 折行 / AI） =="
if command -v pwsh >/dev/null 2>&1; then
    pwsh -NoProfile -File tests/build_host_tests.ps1
elif command -v powershell >/dev/null 2>&1; then
    powershell -ExecutionPolicy Bypass -File tests/build_host_tests.ps1
else
    echo "skip: 需要 PowerShell 运行 tests/build_host_tests.ps1" >&2
    echo "      （或手动用 cc 编译 tests/test_wq_*.c，不含 wq_store.c）" >&2
fi

echo "weiqi-quest 静态校验通过"
