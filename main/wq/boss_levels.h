/*
 * boss_levels.h —— BOSS 关数据（与 AI 对弈的"先提 N 子获胜"关卡）。
 *
 * 数据口径移植自 online-go.com go-game 的 src/game/chapters/boss.ts（手写章节，
 * 非脚本生成）；字段语义对照 src/game/types.ts 的 LevelAiGame（types.ts:75-89）。
 * 本文件只声明数据与结构，不含任何游戏逻辑（AI 走子、胜负判定由 P0/P1 阶段实现）。
 *
 * 与 wq_level_t 的关系：
 *   wq_level_t 是"脚本化正解路径"题库（puzzle/choice/ending），靠 correct[] 路径
 *   前缀判对错；BOSS 关是开放式对弈，没有正解路径、没有题干选项，二者语义不同，
 *   因此这里**显式扩展**出一个兄弟结构 wq_boss_level_t，而不是硬塞进 wq_level_t。
 * 复用的约定：棋子颜色宏（0/1/2）与点下标编码（y*size+x）与 wq_content.h 一致。
 *
 * 本文件为表头静态数据表（static const），P0/P1 搬入工程时可整体保留，
 * 或按需拆成 .h 声明 + .c 定义。
 */
#pragma once

#include <stdint.h>

/* 与 wq_content.h 的颜色编码保持一致（0 空 / 1 黑 / 2 白）。 */
#ifndef WQ_COLOR_EMPTY
#define WQ_COLOR_EMPTY 0
#define WQ_COLOR_BLACK  1
#define WQ_COLOR_WHITE  2
#endif

/* BOSS 关棋盘上限：本应用只做 7×7 与 9×9 两关，缓冲按 9×9=81 点开。 */
#define WQ_BOSS_BOARD_MAX   9
#define WQ_BOSS_POINTS_MAX  (WQ_BOSS_BOARD_MAX * WQ_BOSS_BOARD_MAX)  /* 81 */
#define WQ_BOSS_LEVEL_COUNT 2

typedef struct {
    const char *title;            /* 关卡标题 */
    const char *instruction;      /* 题面说明 */
    uint8_t  board_size;          /* 棋盘边长：7 或 9（正方形） */
    uint8_t  player_color;         /* 玩家执子：固定 WQ_COLOR_BLACK */
    uint8_t  ai_color;             /* AI 执子：固定 WQ_COLOR_WHITE */
    uint8_t  target_captures;      /* 先提掉对方 N 子即胜 */
    uint16_t max_moves;            /* 双方合计步数上限，用尽判负 */
    int16_t  komi;                 /* 贴目：提子制固定 0 */
    /* 初始盘面：board_size*board_size 个点，下标 = y*board_size + x，
     * 取值 WQ_COLOR_*。与 boss.ts 一致，两关都从空盘开始（见 boss_design.md）。 */
    const uint8_t *initial_board;
    uint16_t initial_stone_count;  /* 初始盘面已布子数（空盘 = 0） */
} wq_boss_level_t;

/* ---- 初始盘面：两关均为空盘（boss.ts 不布初始子），全 0 占位 ---- */
/* 7×7 = 49 点 */
static const uint8_t boss_7x7_board[7 * 7] = {
    /* 全空 */
    0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,
};

/* 9×9 = 81 点 */
static const uint8_t boss_9x9_board[9 * 9] = {
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,
};

static const wq_boss_level_t wq_boss_levels[WQ_BOSS_LEVEL_COUNT] = {
    {
        .title = "7×7 · 王者对决",
        .instruction =
            "执黑先行，在 7×7 棋盘上与 AI 对弈：先提掉对方 10 颗棋子者获胜。",
        .board_size = 7,
        .player_color = WQ_COLOR_BLACK,
        .ai_color = WQ_COLOR_WHITE,
        .target_captures = 10,
        .max_moves = 200,
        .komi = 0,
        .initial_board = boss_7x7_board,
        .initial_stone_count = 0,
    },
    {
        .title = "9×9 · 王座争锋",
        .instruction =
            "升级到 9×9 战场。执黑先行，与 AI 对弈：先提掉对方 20 颗棋子者获胜。",
        .board_size = 9,
        .player_color = WQ_COLOR_BLACK,
        .ai_color = WQ_COLOR_WHITE,
        .target_captures = 20,
        .max_moves = 400,
        .komi = 0,
        .initial_board = boss_9x9_board,
        .initial_stone_count = 0,
    },
};
