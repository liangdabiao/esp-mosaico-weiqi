// logic/wq_ai.h —— BOSS 对弈 AI：离线贪心走子（纯 C，宿主可测）。
//
// 语义移植自 online-go.com go-game 的 src/game/ai/goAI.ts（greedy fallback），
// 数据口径与 wq_content.h / boss_levels.h 一致：颜色宏 0/1/2、点下标 = y*size+x。
// 棋盘边长参数化（7×7 / 9×9 通用），不依赖 ESP-IDF 与 LVGL，宿主测试直接编译。
//
// 策略优先级（与 goAI.ts findFallbackMove 一致）：
//   1. 能提子 → 提最大的（提子数最多）
//   2. 无提子 → 救己方被打吃块（气=1，块越大越优先，落其气点）
//   3. 否则 → 贴己方棋子的空点扩展（邻己子数最多的合法点）
//   4. 兜底 → 最靠近棋盘中心的合法点
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wq_content.h" /* WQ_COLOR_EMPTY / BLACK / WHITE */
#include "boss_levels.h" /* WQ_BOSS_POINTS_MAX */

// 判断 (x, y) 落 color 是否合法：占位拒绝；模拟提子后己方组仍有气即合法。
bool wq_ai_legal(const uint8_t *cells, uint8_t size, int x, int y, uint8_t color);

// 模拟落子。不修改原盘。
//   返回提子数（>=0）；非法（占位 / 自杀）返回 -1。
//   out_board 非空时写入模拟后的盘面（size*size 字节）。
int wq_ai_simulate(const uint8_t *cells, uint8_t size, int x, int y, uint8_t color,
                   uint8_t *out_board);

// 贪心选点：返回点下标（y*size + x）；无合法点返回 -1。
// 同分时取靠前的枚举顺序（行优先）。
int wq_ai_find_move(const uint8_t *cells, uint8_t size, uint8_t color);

// 盘面上 color 的棋子总数（BOSS 关用于校验提子目标进度）。
uint16_t wq_ai_count_stones(const uint8_t *cells, uint8_t size, uint8_t color);

// 坐标 <-> 点下标互转（尺寸参数化，供 AI 对战页使用）。
static inline uint8_t wq_ai_point_at(uint8_t size, int x, int y)
{
    return (uint8_t)(y * size + x);
}
