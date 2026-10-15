// tests/test_wq_choice_layout.c —— 选择题作答页版式预算（宿主，无需硬件）。
//
// 选择题作答页要在同一屏里放下「题干 + 棋盘 + 选项」，所以棋盘按剩余空间缩小。
// 这里把 solve_layout() 的算术复算一遍（常量与 main/game.c 的 WQ_SOLVE_* 是一对，
// 改一边就要改另一边），并对整本题库断言四件事：
//
//   1. 题干折行不超 Q_MAX_LINES        —— 超了会被截断，玩家读不到完整题目
//   2. 棋盘格距不低于 CELL_MIN         —— 再小就点不准（手指落在 9x9 的点上）
//   3. 最外圈棋子不被底部选项条切掉    —— 棋盘外沿 + 一个棋子半径 <= 选项条上沿
//   4. 选项数与选项文本在预算内        —— 竖排回退只该是极少数关
//
// 改了字号、行宽、选项高度或题目文案，这些结论都会变，这里会立刻发现。
//
// 两个刻意的约定：
//   - 宽度用**保守估计**（ASCII 记满 16px，实际 raylib 字体更窄）。运行时用真实
//     MeasureText 判「横排放不放得下」，只会比这里更宽松，所以这里判"放得下"
//     之后运行时一定也放得下。
//   - 输出一律 **ASCII**：本测试由 build_host_tests.ps1 用管道捞进 PowerShell，
//     中文在 GBK 解码下会变乱码，失败信息就白写了（见 known-issues #20 同类问题）。
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wq_content.h"
#include "wq_wrap.h"

enum {
    SCREEN_W = 480,
    SCREEN_H = 480,
    Q_TOP = 48,          /* game.c: WQ_SOLVE_Q_TOP */
    Q_CHARS = 24,        /* game.c: WQ_SOLVE_Q_CHARS */
    Q_LINE_H = 20,       /* game.c: WQ_SOLVE_Q_LINE_H */
    Q_MAX_LINES = 6,     /* game.c: WQ_SOLVE_Q_MAX_LINES */
    GAP = 8,             /* game.c: WQ_SOLVE_GAP */
    OPT_H = 56,          /* game.c: WQ_SOLVE_OPT_H */
    OPT_ROW_H = 36,      /* game.c: WQ_SOLVE_OPT_ROW_H */
    OPT_GAP = 8,         /* game.c: WQ_SOLVE_OPT_GAP */
    SIDE = 16,           /* game.c: WQ_SOLVE_SIDE */
    LABEL_W = 26,        /* game.c: WQ_SOLVE_LABEL_W */
    BOTTOM = 12,         /* game.c: WQ_SOLVE_BOTTOM */
    CELL_MIN = 26,       /* game.c: WQ_SOLVE_CELL_MIN */
    CELL_FULL = 48,      /* game_config.h: WQ_CELL */
    STONE_R_FULL = 20,   /* game_config.h: WQ_STONE_RADIUS */
    LINES = 9,
};

static int failures = 0;

static void check(bool ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* 折行结果有几行。 */
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

/* 码点数（不是字节数）：长度上限按"字"算，用 strlen 会把 4 个汉字算成 12。 */
static int utf8_count(const char *utf8)
{
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
        if ((*p & 0xC0) != 0x80) n++;
    }
    return n;
}

/* 保守宽度：全角按字距 18px，ASCII 按满字号 16px（真实字体更窄）。 */
static int width_upper(const char *utf8)
{
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
        if (*p < 0x80) { w += 16; continue; }
        if ((*p & 0xC0) == 0x80) continue; /* 续字节不占位 */
        w += 18;
    }
    return w;
}

int main(void)
{
    static char wrapped[WQ_WRAP_CAPACITY];
    int choice_levels = 0, vertical = 0, wrapped_overflow = 0, clipped = 0, bad_option = 0;
    int min_cell = CELL_FULL, min_cell_level = 0;
    int max_q_lines = 0, max_q_level = 0, max_band = 0;

    for (int i = 0; i < WQ_LEVEL_COUNT; i++) {
        const wq_level_t *level = &wq_levels[i];
        if (level->kind != WQ_KIND_CHOICE || level->option_count == 0) continue;
        choice_levels++;

        /* --- 题干折行 --- */
        const size_t n = wq_wrap_utf8(level->question, Q_CHARS, wrapped, sizeof(wrapped));
        if (n == 0) {
            printf("FAIL level %d: question wrap failed (buffer %d bytes)\n",
                   i + 1, WQ_WRAP_CAPACITY);
            failures++;
            continue;
        }
        const int q_lines = line_count(wrapped);
        if (q_lines > Q_MAX_LINES) {
            wrapped_overflow++;
            printf("FAIL level %d: question is %d lines, over the %d-line cap\n",
                   i + 1, q_lines, Q_MAX_LINES);
        }
        if (q_lines > max_q_lines) { max_q_lines = q_lines; max_q_level = i + 1; }

        /* --- 选项：数量与文本预算 --- */
        if (level->option_count > WQ_OPTIONS_MAX) {
            printf("FAIL level %d: %u options over the max of %d\n",
                   i + 1, (unsigned)level->option_count, WQ_OPTIONS_MAX);
            failures++;
        }
        for (uint8_t o = 0; o < level->option_count; o++) {
            const char *opt = level->options[o];
            const int chars = opt ? utf8_count(opt) : 0;
            if (chars == 0) {
                printf("FAIL level %d option %c: empty\n", i + 1, (char)('A' + o));
                failures++;
                bad_option++;
            } else if (chars > WQ_OPTION_TEXT_MAX_CHARS) {
                printf("FAIL level %d option %c: %d chars over the max of %d\n",
                       i + 1, (char)('A' + o), chars, WQ_OPTION_TEXT_MAX_CHARS);
                failures++;
                bad_option++;
            }
        }

        /* --- 选项区：横排放不下就整关竖排（与 solve_layout 同判据） --- */
        const int count = level->option_count;
        const int bar_w = (SCREEN_W - 2 * SIDE - (count - 1) * OPT_GAP) / count;
        const int inner = bar_w - LABEL_W - 8;
        bool horizontal = true;
        for (uint8_t o = 0; o < level->option_count; o++) {
            if (width_upper(level->options[o]) > inner) horizontal = false;
        }
        const int band_h = horizontal ? OPT_H
                                      : count * OPT_ROW_H + (count - 1) * OPT_GAP;
        if (!horizontal) vertical++;
        if (band_h > max_band) max_band = band_h;
        const int band_y = SCREEN_H - BOTTOM - band_h;

        /* --- 棋盘：与 solve_layout 同一套算术 --- */
        const int board_top = Q_TOP + ((q_lines > 0) ? q_lines * Q_LINE_H + GAP : 0);
        int avail = band_y - GAP - board_top;
        const int avail_max = (LINES - 1) * CELL_FULL;
        if (avail > avail_max) avail = avail_max;
        int cell = (avail * CELL_FULL) / ((LINES - 1) * CELL_FULL + 2 * STONE_R_FULL);
        if (cell > CELL_FULL) cell = CELL_FULL;
        if (cell < CELL_MIN) cell = CELL_MIN;
        if (cell < min_cell) { min_cell = cell; min_cell_level = i + 1; }

        const int span = (LINES - 1) * cell;
        const int origin_y = board_top + (avail - span) / 2;
        const int stone_r = cell * STONE_R_FULL / CELL_FULL;

        /* 最外圈棋子的下沿不能越过选项条上沿。 */
        const int bottom_gap = band_y - (origin_y + span + stone_r);
        if (bottom_gap < 0) {
            clipped++;
            printf("FAIL level %d: bottom-row stones clipped by %d px (cell %d)\n",
                   i + 1, -bottom_gap, cell);
        }
        /* 顶行棋子不能顶到题干分隔线。 */
        if (origin_y - stone_r < board_top) {
            clipped++;
            printf("FAIL level %d: top-row stones overlap the question band by %d px\n",
                   i + 1, board_top - (origin_y - stone_r));
        }
    }

    printf("\nchoice levels %d: question max %d lines (level %d), vertical-layout %d,"
           " option band max %d px, board cell min %d (level %d)\n",
           choice_levels, max_q_lines, max_q_level, vertical, max_band,
           min_cell, min_cell_level);

    check(wrapped_overflow == 0, "every choice question wraps within the line cap");
    check(bad_option == 0, "every option is non-empty and within the length cap");
    check(clipped == 0, "no choice board gets its outer stones clipped");
    check(min_cell >= CELL_MIN, "board cell stays at or above the minimum (tap accuracy)");
    // The numbers below are tied to the current level table and layout: they change
    // when the questions or the WQ_SOLVE_* constants change, so re-verify against
    // main/game.c before updating them.
    check(choice_levels == 32, "32 choice levels (level table unchanged)");
    check(max_q_lines == 5, "longest question is 5 lines (level 11)");
    check(vertical == 1, "only 1 level needs the vertical option layout (level 133)");
    check(min_cell == 27, "tightest board cell is 27 px");

    printf("\n%s\n", failures == 0 ? "test_wq_choice_layout: PASS"
                                   : "test_wq_choice_layout: FAIL");
    return failures == 0 ? 0 : 1;
}
