// host_tests/test_wq_ai.c —— BOSS 对弈贪心 AI 宿主测试。
//
// 覆盖（语义对齐 goAI.ts）：
//   1. 合法/非法判定：占位、自杀、提子后合法
//   2. 模拟落子：单子/多子提子计数、盘面更新、非法不写盘
//   3. 贪心优先级：能提子选最大提子 → 无提子救打吃 → 扩展 → 中央兜底
//   4. 空盘首手：9×9 落中心 (4,4)；7×7 落中心 (3,3)
//   5. 全 BOSS 关数据核验：7×7 提 10、9×9 提 20，空盘开局
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wq_ai.h"
#include "boss_levels.h"

static uint8_t at(int size, int x, int y) { return (uint8_t)(y * size + x); }

static void test_occupied_rejected(void)
{
    uint8_t b[9 * 9];
    memset(b, 0, sizeof(b));
    b[at(9, 4, 4)] = WQ_COLOR_BLACK;
    assert(!wq_ai_legal(b, 9, 4, 4, WQ_COLOR_WHITE));
    assert(wq_ai_simulate(b, 9, 4, 4, WQ_COLOR_WHITE, NULL) == -1);
    puts("ok  占位被拒");
}

static void test_suicide_rejected(void)
{
    /* 黑 ba(点1) ab(点9) 围住 a 角：白在 aa(点0) 落子自杀。 */
    uint8_t b[9 * 9];
    memset(b, 0, sizeof(b));
    b[at(9, 1, 0)] = WQ_COLOR_BLACK; /* ba */
    b[at(9, 0, 1)] = WQ_COLOR_BLACK; /* ab */
    assert(!wq_ai_legal(b, 9, 0, 0, WQ_COLOR_WHITE));
    assert(wq_ai_simulate(b, 9, 0, 0, WQ_COLOR_WHITE, NULL) == -1);
    puts("ok  自杀被拒");
}

static void test_capture_single(void)
{
    /* 白 aa 黑 ab ac：黑在 ba 落子提 aa。 */
    uint8_t b[9 * 9];
    memset(b, 0, sizeof(b));
    b[at(9, 0, 0)] = WQ_COLOR_WHITE;
    b[at(9, 0, 1)] = WQ_COLOR_BLACK;
    b[at(9, 0, 2)] = WQ_COLOR_BLACK;
    uint8_t out[9 * 9];
    const int c = wq_ai_simulate(b, 9, 1, 0, WQ_COLOR_BLACK, out);
    assert(c == 1);
    assert(out[at(9, 0, 0)] == WQ_COLOR_EMPTY);
    assert(out[at(9, 1, 0)] == WQ_COLOR_BLACK);
    puts("ok  单子被提（模拟写盘正确）");
}

static void test_capture_prefer_biggest(void)
{
    /* 局面：白 aa、ab 两子，唯一气在 ba(1,0)，黑落 ba 一提二；
       另有白孤子 (6,6) 唯一气 (6,7)，黑落 (6,7) 只提一。
       贪心应选 ba 提两子。 */
    uint8_t b[9 * 9];
    memset(b, 0, sizeof(b));
    b[at(9, 0, 0)] = WQ_COLOR_WHITE; /* aa */
    b[at(9, 0, 1)] = WQ_COLOR_WHITE; /* ab */
    b[at(9, 1, 1)] = WQ_COLOR_BLACK;  /* bb：堵死白块下侧 */
    b[at(9, 2, 0)] = WQ_COLOR_BLACK;  /* ca */
    b[at(9, 0, 2)] = WQ_COLOR_BLACK;  /* ac */
    b[at(9, 6, 6)] = WQ_COLOR_WHITE;  /* 孤子 */
    b[at(9, 5, 6)] = WQ_COLOR_BLACK;
    b[at(9, 7, 6)] = WQ_COLOR_BLACK;
    b[at(9, 6, 5)] = WQ_COLOR_BLACK;
    const int m = wq_ai_find_move(b, 9, WQ_COLOR_BLACK);
    assert(m == at(9, 1, 0));
    puts("ok  提子优先且选最大提子");
}

static void test_save_atari_before_extend(void)
{
    /* 黑单子 e4（9×9 中心 4,4）仅剩 e5（4,5）一口气（被打吃），
       周围无白子可提。黑应落 e5 自救，而不是随便扩展。 */
    uint8_t b[9 * 9];
    memset(b, 0, sizeof(b));
    b[at(9, 4, 4)] = WQ_COLOR_BLACK;
    b[at(9, 3, 4)] = WQ_COLOR_WHITE;
    b[at(9, 5, 4)] = WQ_COLOR_WHITE;
    b[at(9, 4, 3)] = WQ_COLOR_WHITE;
    /* 上方 e5(4,5) 留空 → 唯一气。 */
    const int m = wq_ai_find_move(b, 9, WQ_COLOR_BLACK);
    assert(m == at(9, 4, 5));
    puts("ok  打吃时优先救己方块");
}

static void test_extend_adjacent(void)
{
    /* 无提子、无打吃：黑应贴自己的子扩展（邻己子最多的合法点）。
       黑 e4 邻点 e5/e3/d4/f4 各计 1 分，取行优先第一个（d4？行优先 x 从 0：
       d4=(3,4) 先于 e3=(4,3)？行优先 = y 外层，先扫 (3,4) 即 d4 之前是 (0..2,4)… 
       实际 (3,4) 比 (4,3) 早：y=4 行 x=3。断言取 d4=(3,4)？不对——(4,3) 在 y=3 行更早。
       行优先：y=0..8, x=0..8。e3=(4,3) 在 y=3 行第 5 列；d4=(3,4) 在 y=4 行第 4 列。
       y=3 < y=4，所以 (4,3) 先扫到，分相同取 (4,3)=e3。 */
    uint8_t b[9 * 9];
    memset(b, 0, sizeof(b));
    b[at(9, 4, 4)] = WQ_COLOR_BLACK;
    const int m = wq_ai_find_move(b, 9, WQ_COLOR_BLACK);
    assert(m == at(9, 4, 3)); /* e3：行优先下第一个邻点 */
    puts("ok  无仗可打时贴己扩展");
}

static void test_empty_boards_open_center(void)
{
    uint8_t b9[9 * 9];
    memset(b9, 0, sizeof(b9));
    assert(wq_ai_find_move(b9, 9, WQ_COLOR_BLACK) == at(9, 4, 4));

    uint8_t b7[7 * 7];
    memset(b7, 0, sizeof(b7));
    assert(wq_ai_find_move(b7, 7, WQ_COLOR_WHITE) == at(7, 3, 3));
    puts("ok  空盘首手落中心（9×9→(4,4)，7×7→(3,3)）");
}

static void test_boss_data_contract(void)
{
    assert(WQ_BOSS_LEVEL_COUNT == 2);
    /* 7×7：提 10 子，空盘开局 */
    assert(wq_boss_levels[0].board_size == 7);
    assert(wq_boss_levels[0].target_captures == 10);
    assert(wq_boss_levels[0].initial_stone_count == 0);
    assert(wq_boss_levels[0].player_color == WQ_COLOR_BLACK);
    assert(wq_boss_levels[0].ai_color == WQ_COLOR_WHITE);
    /* 9×9：提 20 子 */
    assert(wq_boss_levels[1].board_size == 9);
    assert(wq_boss_levels[1].target_captures == 20);
    assert(wq_boss_levels[1].initial_stone_count == 0);
    /* 空盘核验：整表全空 */
    for (int i = 0; i < 49; i++)
        assert(wq_boss_levels[0].initial_board[i] == WQ_COLOR_EMPTY);
    for (int i = 0; i < 81; i++)
        assert(wq_boss_levels[1].initial_board[i] == WQ_COLOR_EMPTY);
    puts("ok  BOSS 关数据契约（7×7 提 10 / 9×9 提 20，空盘）");
}

int main(void)
{
    test_occupied_rejected();
    test_suicide_rejected();
    test_capture_single();
    test_capture_prefer_biggest();
    test_save_atari_before_extend();
    test_extend_adjacent();
    test_empty_boards_open_center();
    test_boss_data_contract();
    puts("test_wq_ai: PASS");
    return 0;
}
