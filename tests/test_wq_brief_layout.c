// tests/test_wq_brief_layout.c —— BRIEF 题面版式预算（宿主，无需硬件）。
//
// 断言的是「按 2 倍字号排版时，整本题库都排得出来，且行数在版式允许的范围内」。
// 题面放大到 2 倍后每行字数减半，行数翻倍 —— 一旦有人改字号、改行宽或加长题面，
// 这里会立刻发现「有几关放不进一屏、要滚多少行」这类结论失效。
//
// 版式常量与 main/game.c 的 WQ_BRIEF_* 是一对，改一边就要改另一边：
//   每行 12 字（字距 18*2=36px，正文可用宽 448px）
//   行高 40px（20*2），视口 = 480-132-52 = 296px
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wq_content.h"
#include "wq_wrap.h"

enum {
    CHARS_PER_LINE = 12,               /* game.c: WQ_BRIEF_CHARS_PER_LINE */
    LINE_H = 40,                       /* game.c: WQ_BRIEF_LINE_H */
    VIEW_H = 480 - 132 - 52,           /* game.c: WQ_BRIEF_BOTTOM - WQ_BRIEF_TEXT_Y */
};

static int failures = 0;

static void check(bool ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok " : "FAIL", what);
    if (!ok) failures++;
}

static int line_count(const char *text)
{
    int n = 0;
    while (*text) {
        n++;
        while (*text && *text != '\n') text++;
        if (*text == '\n') text++;
    }
    return n;
}

int main(void)
{
    static char buf[WQ_WRAP_CAPACITY];
    int max_lines = 0, max_bytes = 0, max_level = 0, need_scroll = 0;

    for (int i = 0; i < WQ_LEVEL_COUNT; i++) {
        const size_t n = wq_wrap_utf8(wq_levels[i].instruction, CHARS_PER_LINE,
                                      buf, sizeof(buf));
        if (n == 0) {
            printf("FAIL 第 %d 关折行失败（缓冲 %d 字节不够）\n",
                   i + 1, WQ_WRAP_CAPACITY);
            failures++;
            continue;
        }
        const int lines = line_count(buf);
        if ((int)n > max_bytes) max_bytes = (int)n;
        if (lines > max_lines) {
            max_lines = lines;
            max_level = i + 1;
        }
        if (lines * LINE_H > VIEW_H) need_scroll++;
    }

    printf("题库 %d 关：最多 %d 行（第 %d 关），需要滚动的 %d 关，"
           "折行后最大 %d 字节（容量 %d）\n",
           WQ_LEVEL_COUNT, max_lines, max_level, need_scroll,
           max_bytes, WQ_WRAP_CAPACITY);

    check(max_bytes < WQ_WRAP_CAPACITY, "折行缓冲容量够用（不会整段渲染不出来）");
    // 最长题面 = 第 26 关「领地」10 行。题库或字号变了这个数就会变，
    // 届时请连同 brief_relayout 的滚动上限一起复核后再改这里。
    check(max_lines == 10, "最长题面 10 行（题库/字号未变）");
    // 需要滚动说明「一屏放不下」，此时 brief_relayout 会给出滚动上限。
    check(need_scroll == 5, "需要滚动的关卡 5 关（其余 149 关一屏放得下）");
    // 滚动上限取整行：10 行内容 - 7.4 行视口，最多滚到第 4 行开头（120px）。
    const int scroll_max = ((max_lines * LINE_H - VIEW_H) + LINE_H - 1) / LINE_H * LINE_H;
    check(scroll_max == 120, "最长关的滚动上限 120px（整行对齐）");

    printf("\n%s\n", failures == 0 ? "test_wq_brief_layout: PASS" : "test_wq_brief_layout: FAIL");
    return failures == 0 ? 0 : 1;
}
