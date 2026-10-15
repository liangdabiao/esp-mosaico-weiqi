#!/usr/bin/env python3
"""生成 raylib-lite 用的中文位图字模子集（wq_cjk_glyph_data.h）。

raylib-lite 的 DrawText 只有 5x7 ASCII，不支持 CJK。本工具用 Pillow 从
Noto Sans CJK SC 母字体把「页面用到的那一小撮字符」光栅化成 16x16 位图，
打包成 C 表（每行 16bit），由 main/wq_cjk_font.c 做 glyph blit。

字符清单：
  * 页面 UI 文案（UI_STRINGS，集中维护、自动去重）；
  * main/wq/wq_content.c 里全部 C 字符串字面量中的 CJK、全角标点与需要的符号
    （7 章标题 + 154 关的题面/题干/选项，BRIEF 页题面正文需要它们）。

只收 CJK + 全角标点是不够的：题面里还有通用标点与图形符号（“ ” … ▲ 等），
它们都 < 0x2E80，按旧规则会被漏掉，表现是设备上那几个字**整块消失**。
所以这里显式列出要收的符号区间。

母字体缺失的码点会直接报错退出 —— 宁可在生成期失败，也不要在设备上留空白框。

用法（仓库根目录或本目录均可）：
    python tools/weiqi/gen_cjk_font.py          # 写出字模表
    python tools/weiqi/gen_cjk_font.py --check  # 只校验是否与源码/题库同步
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "main" / "wq_cjk_glyph_data.h"
CONTENT_SRC = ROOT / "main" / "wq" / "wq_content.c"
FONT_PATH = Path("D:/esp/fontsrc/NotoSansCJKsc-Regular.otf")

# game.c 页面里出现的中文 UI 文案（纯中文；ASCII 数字/符号走 raylib DrawText）。
# 题面正文来自 wq_content.c，不在此重复罗列。
UI_STRINGS = [
    "章节选择", "开始", "返回", "下一关", "回列表", "过关",
    "音量",
]

CELL = 16          # 字模边长 16x16
SIZE = 16          # 光栅字号（与 CELL 同档，预留内边距）

# CJK 之外的符号区间。这些都是题面/界面真会用到的：
#   U+00B7 间隔中点 ·、U+00D7 乘号 ×
#   U+2000-U+206F 通用标点（“ ” ‘ ’ … — ‥ 等，中文引号与省略号都在这里）
#   U+2460-U+24FF 带圈/带括号数字
#   U+25A0-U+25FF 几何图形（▲ □ ○ ◆ ● 等，题面用它标注棋子）
#   U+3000-U+303F 已在 0x2E80 以上，被下面的 CJK 分支覆盖
EXTRA_RANGES = (
    (0x00B7, 0x00B7),
    (0x00D7, 0x00D7),
    (0x2010, 0x206F),
    (0x2460, 0x24FF),
    (0x25A0, 0x25FF),
)


def _is_wanted(c: str) -> bool:
    o = ord(c)
    # CJK 统一表意、扩展、全角标点（『』、，。（）：；？）与间隔中点、标记符号。
    if o >= 0x2E80:
        return True
    if c in "·『』、，。（）：；？":
        return True
    return any(lo <= o <= hi for lo, hi in EXTRA_RANGES)


def collect_chars() -> list[str]:
    chars: set[str] = set()
    for s in UI_STRINGS:
        for c in s:
            if _is_wanted(c):
                chars.add(c)
    # 自动收录题库全部文案：从 wq_content.c 的 C 字符串字面量提取，
    # 避免每加一关就要回来手改这份清单。
    src = CONTENT_SRC.read_text(encoding="utf-8")
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', src):
        for c in lit:
            if _is_wanted(c):
                chars.add(c)
    return sorted(chars)


def missing_from_font(chars: list[str]) -> list[str]:
    """母字体里没有的码点。生成期报错，不在设备上留空白框。"""
    from fontTools.ttLib import TTFont
    with TTFont(str(FONT_PATH), fontNumber=0, lazy=True) as tt:
        cmap = set(tt.getBestCmap().keys())
    return [c for c in chars if ord(c) not in cmap]


def rasterize(font, ch: str) -> list[int]:
    """返回 CELL 行、每行 16bit（bit15 = 最左列）。"""
    from PIL import Image, ImageDraw
    im = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(im)
    # 居中摆放：按字的边界框把它放进 16x16。
    bbox = font.getbbox(ch)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    ox = (CELL - w) // 2 - bbox[0]
    oy = (CELL - h) // 2 - bbox[1]
    d.text((ox, oy), ch, font=font, fill=255)
    px = im.load()
    rows = []
    for y in range(CELL):
        bits = 0
        for x in range(CELL):
            if px[x, y] > 96:  # 二值化阈值
                bits |= 1 << (15 - x)
        rows.append(bits)
    return rows


def generate() -> str:
    if not FONT_PATH.is_file():
        raise SystemExit(f"找不到母字体：{FONT_PATH}")
    chars = collect_chars()
    missing = missing_from_font(chars)
    if missing:
        raise SystemExit(
            "母字体缺少这些码点，请改用别的字形或从内容里去掉："
            + " ".join(f"{c}(U+{ord(c):04X})" for c in missing)
        )
    from PIL import ImageFont
    font = ImageFont.truetype(str(FONT_PATH), SIZE)

    glyphs = [(ch, rasterize(font, ch)) for ch in chars]

    lines = [
        "// SPDX-License-Identifier: Apache-2.0",
        "// 由 tools/weiqi/gen_cjk_font.py 自动生成 —— 勿手改。",
        f"// 母字体：Noto Sans CJK SC Regular（SIL OFL 1.1）；{len(chars)} 个字模，{CELL}x{CELL}。",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define WQ_CJK_CELL {CELL}",
        "",
        "typedef struct { uint16_t cp; uint16_t rows[WQ_CJK_CELL]; } wq_cjk_glyph_t;",
        "",
        f"#define WQ_CJK_GLYPH_COUNT {len(glyphs)}",
        "static const wq_cjk_glyph_t WQ_CJK_GLYPHS[WQ_CJK_GLYPH_COUNT] = {",
    ]
    for ch, rows in glyphs:
        cp = ord(ch)
        rows_str = ",".join(f"0x{r:04x}" for r in rows)
        lines.append(f"    {{ 0x{cp:04x}, {{ {rows_str} }} }},  /* {ch} */")
    lines.append("};")
    lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="只校验生成物是否与源码/题库同步，过期就失败")
    args = parser.parse_args()

    text = generate()

    if args.check:
        if not OUT.is_file() or OUT.read_text(encoding="utf-8") != text:
            print(f"STALE: {OUT.relative_to(ROOT)} —— 字符清单已变化，请重跑 gen_cjk_font.py",
                  file=sys.stderr)
            return 1
        print(f"up to date: {OUT.relative_to(ROOT)}")
        return 0

    OUT.write_text(text, encoding="utf-8", newline="\n")
    count = text.count("    { 0x")
    print(f"wrote {OUT.relative_to(ROOT)}: {count} glyphs, {count * CELL * 2} bytes bitmap")
    return 0


if __name__ == "__main__":
    sys.exit(main())
