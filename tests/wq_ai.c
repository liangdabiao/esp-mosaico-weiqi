// logic/wq_ai.c —— BOSS 对弈 AI：离线贪心走子。
// 语义与 online-go.com go-game src/game/ai/goAI.ts 的 greedy fallback 对齐，
// 用 wq_content.h 的点下标编码（y*size+x）与颜色宏实现。

#include "wq_ai.h"

#include <string.h>

/* 四邻。 */
static const int DIR_X[4] = { -1, 1, 0, 0 };
static const int DIR_Y[4] = { 0, 0, -1, 1 };

/* 从 (x,y) 出发收集同色连通块：返回块内棋子数；lib 非空时写入
 * size*size 的布尔气点表（lib[i]!=0 表示点 i 是这块的气）。 */
static int group_fill(const uint8_t *cells, uint8_t size, int x, int y,
                      uint8_t *lib)
{
    const uint8_t color = cells[y * size + x];
    uint8_t seen[WQ_BOSS_POINTS_MAX];
    uint8_t queue[WQ_BOSS_POINTS_MAX];
    memset(seen, 0, sizeof(seen));
    if (lib)
        memset(lib, 0, (size_t)size * size);

    int head = 0, tail = 0;
    const uint8_t start = (uint8_t)(y * size + x);
    queue[tail++] = start;
    seen[start] = 1;

    int count = 0;
    while (head < tail) {
        const uint8_t p = queue[head++];
        const int px = p % size, py = p / size;
        count++;
        for (int d = 0; d < 4; d++) {
            const int nx = px + DIR_X[d], ny = py + DIR_Y[d];
            if (nx < 0 || nx >= size || ny < 0 || ny >= size)
                continue;
            const uint8_t np = (uint8_t)(ny * size + nx);
            const uint8_t nc = cells[np];
            if (nc == WQ_COLOR_EMPTY) {
                if (lib)
                    lib[np] = 1;
            } else if (nc == color && !seen[np]) {
                seen[np] = 1;
                queue[tail++] = np;
            }
        }
    }
    return count;
}

/* (x,y) 处的敌块是否无气；是则返回其块内棋子数，否则返回 0。 */
static int enemy_group_no_liberty(const uint8_t *cells, uint8_t size,
                                  int x, int y, uint8_t opp)
{
    if (cells[y * size + x] != opp)
        return 0;
    uint8_t lib[WQ_BOSS_POINTS_MAX];
    const int n = group_fill(cells, size, x, y, lib);
    for (int i = 0; i < size * size; i++)
        if (lib[i])
            return 0;
    return n;
}

bool wq_ai_legal(const uint8_t *cells, uint8_t size, int x, int y, uint8_t color)
{
    return wq_ai_simulate(cells, size, x, y, color, NULL) >= 0;
}

int wq_ai_simulate(const uint8_t *cells, uint8_t size, int x, int y,
                   uint8_t color, uint8_t *out_board)
{
    if (x < 0 || x >= size || y < 0 || y >= size)
        return -1;
    if (cells[y * size + x] != WQ_COLOR_EMPTY)
        return -1;

    uint8_t b[WQ_BOSS_POINTS_MAX];
    memcpy(b, cells, (size_t)size * size);
    b[y * size + x] = color;

    const uint8_t opp = (color == WQ_COLOR_BLACK) ? WQ_COLOR_WHITE : WQ_COLOR_BLACK;
    int captured = 0;

    for (int d = 0; d < 4; d++) {
        const int nx = x + DIR_X[d], ny = y + DIR_Y[d];
        if (nx < 0 || nx >= size || ny < 0 || ny >= size)
            continue;
        const int n = enemy_group_no_liberty(b, size, nx, ny, opp);
        if (n > 0) {
            /* 整块清空。 */
            uint8_t seen[WQ_BOSS_POINTS_MAX];
            memset(seen, 0, sizeof(seen));
            uint8_t queue[WQ_BOSS_POINTS_MAX];
            int head = 0, tail = 0;
            const uint8_t start = (uint8_t)(ny * size + nx);
            queue[tail++] = start;
            seen[start] = 1;
            while (head < tail) {
                const uint8_t p = queue[head++];
                const int px = p % size, py = p / size;
                b[p] = WQ_COLOR_EMPTY;
                captured++;
                for (int dd = 0; dd < 4; dd++) {
                    const int nnx = px + DIR_X[dd], nny = py + DIR_Y[dd];
                    if (nnx < 0 || nnx >= size || nny < 0 || nny >= size)
                        continue;
                    const uint8_t nnp = (uint8_t)(nny * size + nnx);
                    if (b[nnp] == opp && !seen[nnp]) {
                        seen[nnp] = 1;
                        queue[tail++] = nnp;
                    }
                }
            }
        }
    }

    /* 提子后己方组必须仍有气，否则自杀。 */
    uint8_t lib[WQ_BOSS_POINTS_MAX];
    group_fill(b, size, x, y, lib);
    int liberties = 0;
    for (int i = 0; i < size * size; i++)
        if (lib[i])
            liberties++;
    if (liberties == 0)
        return -1;

    if (out_board)
        memcpy(out_board, b, (size_t)size * size);
    return captured;
}

uint16_t wq_ai_count_stones(const uint8_t *cells, uint8_t size, uint8_t color)
{
    uint16_t n = 0;
    for (int i = 0; i < size * size; i++)
        if (cells[i] == color)
            n++;
    return n;
}

int wq_ai_find_move(const uint8_t *cells, uint8_t size, uint8_t color)
{
    /* 1. 提子优先：提子数最多的合法点。 */
    int best_point = -1, best_capture = 0;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (cells[y * size + x] != WQ_COLOR_EMPTY)
                continue;
            const int c = wq_ai_simulate(cells, size, x, y, color, NULL);
            if (c > best_capture) {
                best_capture = c;
                best_point = y * size + x;
            }
        }
    }
    if (best_point >= 0)
        return best_point;

    /* 2. 救己方被打吃块（气=1，块越大优先）。 */
    int save_size = 0, save_point = -1;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (cells[y * size + x] != color)
                continue;
            uint8_t lib[WQ_BOSS_POINTS_MAX];
            const int n = group_fill(cells, size, x, y, lib);
            int lib_count = 0;
            for (int i = 0; i < size * size; i++)
                if (lib[i])
                    lib_count++;
            if (lib_count != 1)
                continue;
            /* 这块被打吃。找它的气点，试走是否合法。 */
            for (int i = 0; i < size * size; i++) {
                if (!lib[i])
                    continue;
                const int lx = i % size, ly = i / size;
                if (wq_ai_legal(cells, size, lx, ly, color)) {
                    if (n > save_size) {
                        save_size = n;
                        save_point = i;
                    }
                }
            }
        }
    }
    if (save_point >= 0)
        return save_point;

    /* 3. 扩展：邻己方棋子数最多的合法点。 */
    int adj_score[WQ_BOSS_POINTS_MAX];
    memset(adj_score, 0, sizeof(adj_score));
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (cells[y * size + x] != color)
                continue;
            for (int d = 0; d < 4; d++) {
                const int nx = x + DIR_X[d], ny = y + DIR_Y[d];
                if (nx < 0 || nx >= size || ny < 0 || ny >= size)
                    continue;
                const uint8_t np = (uint8_t)(ny * size + nx);
                if (cells[np] == WQ_COLOR_EMPTY)
                    adj_score[np]++;
            }
        }
    }
    int ext_best = -1, ext_score = 0;
    for (int i = 0; i < size * size; i++) {
        if (cells[i] != WQ_COLOR_EMPTY || adj_score[i] == 0)
            continue;
        if (wq_ai_legal(cells, size, i % size, i / size, color)
            && adj_score[i] > ext_score) {
            ext_score = adj_score[i];
            ext_best = i;
        }
    }
    if (ext_best >= 0)
        return ext_best;

    /* 4. 兜底：最靠近中心的合法点。 */
    const int cx = size / 2, cy = size / 2;
    int best_center = -1, best_dist = 0x7fffffff;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (cells[y * size + x] != WQ_COLOR_EMPTY)
                continue;
            if (!wq_ai_legal(cells, size, x, y, color))
                continue;
            const int dx = x - cx < 0 ? cx - x : x - cx;
            const int dy = y - cy < 0 ? cy - y : y - cy;
            const int dist = dx + dy;
            if (dist < best_dist) {
                best_dist = dist;
                best_center = y * size + x;
            }
        }
    }
    return best_center;
}
