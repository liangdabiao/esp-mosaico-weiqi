// SPDX-License-Identifier: Apache-2.0
// game.c —— 围棋闯关应用（触摸版）主逻辑，完整页面导航。
//
// 页面流（触摸热区，与 weiqi-quest 一致的章节/关卡/题面/作答/结算）：
//   WORLD  : 7 章列表（每行显示章节号 + 该章通关数/总星）→ 触摸行进 LEVELS
//   LEVELS : 本章关卡网格（每格显示关号 + 星级圆点）→ 触摸格进 BRIEF
//   BRIEF  : 题面摘要 + START / BACK 按钮
//   SOLVE  : 触摸落子（puzzle/移除/选项条/终局确认），左上 BACK 回题面
//   RESULT : 星级 + NEXT / CHAPTER 按钮
// 盘面与判题全部走 wq_session 状态机（wq_engine 规则），与宿主测试同一份 C 代码。
// 存档：过关即落盘（NVS），重启从上次关卡继续。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "game_config.h"
#include "mosaico_raylib_fast.h"
#include "wq/wq_session.h"
#include "wq/wq_store.h"
#include "wq/wq_sfx.h"
#include "wq/wq_volume.h"
#include "wq/wq_wrap.h"
#include "wq_cjk_font.h"

/* 触摸热区矩形。 */
typedef struct {
    int x, y, w, h;
} wq_btn_t;

struct game_t {
    game_config_t config;
    game_snapshot_t state;
    wq_session_t session;   /* 闯关状态机 */
    wq_progress_t progress; /* 跨局存档（NVS） */
    uint8_t volume;         /* 音量档位（0=关），独立于进度 blob 持久化 */
    int last_action;        /* 最近一次 wq_action_t，用于反馈 */
    /* 落子缩放动画：记录最近一次有效落子的格子与起始 tick，仅做半径缓动。 */
    bool anim_on;
    int anim_start_tick;
    int8_t anim_col, anim_row;

    /* BRIEF 页滚动。题面按 2 倍字号排版，最长的几关会超出一屏，
     * 所以题面区可上下拖动；scroll 单位是像素。
     *
     * 版式（折行结果 + 滚动上限）存在实例里，由 update 与 render 共用：
     * 手势处理跑在 update 之前，渲染却可能还没发生过，把 scroll_max 只放在
     * 渲染里算就会让第一次拖动读到 0（夹取成"不能滚"）。 */
    char brief_wrapped[WQ_WRAP_CAPACITY]; /* 题面折行结果 */
    int brief_scroll;
    int brief_scroll_max;
    uint16_t brief_layout_level;          /* 上面这行折行对应哪一关 */
    bool brief_layout_valid;
    int brief_touch_y;                    /* 本次手势按下时的 y */
    int brief_scroll_anchor;              /* 本次手势按下时的 scroll */
    bool brief_dragging;                  /* 本次手势是否落在题面区（可滚动） */
    wq_stage_t last_stage;                /* 用于检测阶段切换（进 BRIEF 时复位滚动） */

    /* SOLVE 页版式（棋盘几何 + 题干行数 + 选项矩形）。
     * 选择题要在同一屏里放下「题干 + 棋盘 + 选项」，棋盘按剩余空间缩小；
     * 其余题型沿用满屏棋盘。这些几何同时被手势命中使用，所以必须在 update
     * 路径上算好并缓存在实例里 —— 写在渲染里会让第一次触摸读到初值。 */
    char solve_q_wrapped[WQ_WRAP_CAPACITY]; /* 题干折行结果 */
    int solve_board_x, solve_board_y;       /* 棋盘左上角（第 0 条线的交点） */
    int solve_board_cell;                   /* 相邻两条线的间距 */
    int solve_stone_r;                      /* 棋子半径（随格距缩放） */
    int solve_q_lines;                      /* 题干行数（0 = 无题干） */
    wq_btn_t solve_opt[WQ_OPTIONS_MAX];     /* 选项 / 终局确认按钮的矩形 */
    uint8_t solve_opt_count;
    bool solve_opt_vertical;                /* 选项是竖排（文本横排放不下时） */
    const char *solve_action_label;         /* 终局态的确认按钮文案（否则 NULL） */
    uint16_t solve_layout_level;            /* 缓存键：上面这版式对应哪一关 */
    bool solve_layout_valid;
};

/* ---- 颜色 ---- */
static const Color BOARD_BG   = { 222, 184, 135, 255 }; /* burlywood */
static const Color GRID_LINE  = {  60,  40,  20, 255 };
static const Color STAR_POINT = {  60,  40,  20, 255 };
static const Color HIGHLIGHT  = { 255,  60,  60, 255 };
static const Color TEXT_DARK  = {  40,  30,  15, 255 };
static const Color TEXT_LIGHT = { 255, 255, 255, 255 };
static const Color BAR_ACTIVE = { 120, 180, 90, 255 };
static const Color BUTTON_BG  = {  90, 120, 80, 255 };
static const Color PANEL_BG   = { 245, 222, 179, 255 };

/* 9x9 star-point intersections (0-indexed): 4 corners of the center square + tengen. */
static const uint8_t star_cols[5] = { 2, 2, 6, 6, 4 };
static const uint8_t star_rows[5] = { 2, 6, 2, 6, 4 };

/* 棋盘几何不再是常量：选择题要缩盘给题干与选项让位，所以原点与格距由
 * solve_layout() 算进实例（update 与 render 共用），这里不再提供 grid_x/grid_y。
 * 满屏版式的基准值仍来自 game_config.h 的 WQ_BOARD_ORIGIN / WQ_CELL。 */

static inline bool btn_hit(const wq_btn_t *b, int x, int y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

/* ---- SOLVE 页（作答）版式常量 ----
 * 正文 16px、字距 pitch = 18px：448 / 18 = 24.8 -> 一行 24 个全角位。 */
#define WQ_SOLVE_Q_TOP       48   /* 题干区顶（跳过 44px 顶栏） */
#define WQ_SOLVE_Q_CHARS     24   /* 题干每行字数（按码点，硬保证） */
#define WQ_SOLVE_Q_LINE_H    20   /* 题干行距 */
#define WQ_SOLVE_Q_MAX_LINES  6   /* 题库实测最长 5 行（107 字），留 1 行余量 */
#define WQ_SOLVE_GAP          8   /* 区与区之间 */
#define WQ_SOLVE_OPT_H       56   /* 横排选项按钮高 */
#define WQ_SOLVE_OPT_ROW_H   36   /* 竖排选项每行高（3 行 + 间距仍给棋盘留得下） */
#define WQ_SOLVE_OPT_GAP      8   /* 选项之间 */
#define WQ_SOLVE_SIDE        16   /* 选项区左右留白 */
#define WQ_SOLVE_LABEL_W     26   /* 选项左侧字母栏宽度 */
#define WQ_SOLVE_BOTTOM      12   /* 选项区距底边 */
#define WQ_SOLVE_CELL_MIN    26   /* 棋盘格距下限（实测最紧一关为 30，留余量） */

/* WORLD 页右上角的音量档位按钮（全局设置，仅占一行）。 */
static inline wq_btn_t volume_btn(void)
{
    return (wq_btn_t){ GAME_WIDTH - 156, 8, 140, 38 };
}

/* 解一个 UTF-8 码点；返回字节数，*cp 写出。非法按 1 字节处理。 */
static int utf8_next_cp(const char *s, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xE0) == 0xC0) {
        *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0) {
        *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) |
              (u[2] & 0x3F);
        return 3;
    }
    *cp = u[0];
    return 1;
}

/* ASCII（A/B/C 标记、数字、关号）的基准字号；CJK 字模是 16px，两者对齐。 */
#define WQ_TEXT_PX 16

/* 画一行「中英混排」：CJK 走 16px 位图字模，ASCII 走 raylib 5x7 字体。
 * wq_cjk_draw 会跳过 ASCII，而题面里大量引用 A/B/C 标记，丢了会读不通，
 * 所以这里按码点分段：ASCII 攒成串用 DrawText，CJK 逐字 blit。返回画笔 x。
 * scale>1 时整行等比放大（BRIEF 正文用 2 倍）。 */
static int draw_line_mixed_scaled(const char *line, int x, int y, Color color,
                                  int scale)
{
    if (scale < 1) scale = 1;
    const int ascii_px = WQ_TEXT_PX * scale;
    /* ASCII 基线相对行顶的偏移：原来 16px 字号时是 +3，按比例放大。 */
    const int ascii_dy = 3 * scale;
    const char *s = line;
    int pen = x;
    char ascii[24];
    int an = 0;
    while (*s) {
        uint32_t cp = 0;
        const int step = utf8_next_cp(s, &cp);
        if (cp < 0x80) {
            ascii[an++] = (char)cp;
            if (an == (int)sizeof(ascii) - 1) {
                ascii[an] = '\0';
                DrawText(ascii, pen, y + ascii_dy, ascii_px, color);
                pen += MeasureText(ascii, ascii_px);
                an = 0;
            }
            s += step;
            continue;
        }
        /* 先把攒下的 ASCII 画掉。 */
        if (an > 0) {
            ascii[an] = '\0';
            DrawText(ascii, pen, y + ascii_dy, ascii_px, color);
            pen += MeasureText(ascii, ascii_px);
            an = 0;
        }
        /* 按码点实际长度拷贝（此处 step 只可能是 2 或 3；ASCII 已在上面 continue）。
         * 固定取 3 字节会在 2 字节字符处多读下一个字符的首字节，且 wq_cjk_draw
         * 会把这个残缺序列再解一次 → 越界读 + 乱码字形。 */
        char one[4] = { 0 };
        memcpy(one, s, (size_t)step);
        pen = wq_cjk_draw_scaled(one, pen, y, color, scale);
        s += step;
    }
    if (an > 0) {
        ascii[an] = '\0';
        DrawText(ascii, pen, y + ascii_dy, ascii_px, color);
        pen += MeasureText(ascii, ascii_px);
    }
    return pen;
}

/* 量一段混排文本的绘制宽度，步进与 draw_line_mixed_scaled 严格一致：
 * ASCII 走 MeasureText，CJK 走字模 pitch（不在字库的码点不占位）。
 * 版式要"先量后画"——混排的宽度不能靠猜，否则居中必然偏、放不放得下也判不准。 */
static int line_width_mixed(const char *line, int scale)
{
    if (!line) return 0;
    if (scale < 1) scale = 1;
    const int ascii_px = WQ_TEXT_PX * scale;
    const char *s = line;
    int w = 0;
    char ascii[24];
    int an = 0;
    while (*s) {
        uint32_t cp = 0;
        const int step = utf8_next_cp(s, &cp);
        if (cp < 0x80) {
            ascii[an++] = (char)cp;
            if (an == (int)sizeof(ascii) - 1) {
                ascii[an] = '\0';
                w += MeasureText(ascii, ascii_px);
                an = 0;
            }
            s += step;
            continue;
        }
        if (an > 0) {
            ascii[an] = '\0';
            w += MeasureText(ascii, ascii_px);
            an = 0;
        }
        char one[4] = { 0 };
        memcpy(one, s, (size_t)step);
        w += wq_cjk_width_scaled(one, scale);
        s += step;
    }
    if (an > 0) {
        ascii[an] = '\0';
        w += MeasureText(ascii, ascii_px);
    }
    return w;
}

/* 全局关卡号 -> 章节下标。关卡表按章节连续存放。 */
static uint8_t chapter_of_level(uint16_t level)
{
    for (uint8_t ci = 0; ci < (uint8_t)WQ_CHAPTER_COUNT; ci++) {
        const wq_chapter_t *ch = &wq_chapters[ci];
        if (level >= ch->first && level < (uint16_t)(ch->first + ch->count)) return ci;
    }
    return 0;
}

/* ---- BRIEF 页版式 ----
 * 题面按 2 倍字号排版（字模 16px -> 32px）：字距 18*2=36px，正文可用宽
 * 480-2*16=448px，所以每行 12 个码点；行高 20*2=40px。最长的题面 10 行
 * = 400px，仍会超出一屏，因此题面区支持上下拖动（见 game_set_pointer
 * 的 BRIEF 手势），其余 149 关一屏放得下、不需要滚。 */
#define WQ_BRIEF_SCALE          2
#define WQ_BRIEF_CHARS_PER_LINE 12
#define WQ_BRIEF_LINE_H         (20 * WQ_BRIEF_SCALE)
#define WQ_BRIEF_TITLE_Y        10
#define WQ_BRIEF_DIVIDER_Y      46
#define WQ_BRIEF_TEXT_Y         52
#define WQ_BRIEF_BOTTOM         (GAME_HEIGHT - 132) /* 给底部按钮留位 */

/* 每行字数不能超出可用宽度：字距 = 18 * WQ_BRIEF_SCALE（18 见 wq_cjk_font.c）。 */
_Static_assert(WQ_BRIEF_CHARS_PER_LINE * 18 * WQ_BRIEF_SCALE <= GAME_WIDTH - 32,
               "BRIEF 每行字数超出正文可用宽度");

/* BRIEF 底部两个按钮。触摸判定与绘制共用同一份几何，避免两边各写一遍。 */
static inline wq_btn_t brief_start_btn(void)
{
    return (wq_btn_t){ GAME_WIDTH / 2 - 90, GAME_HEIGHT - 120, 180, 60 };
}

static inline wq_btn_t brief_back_btn(void)
{
    return (wq_btn_t){ 20, GAME_HEIGHT - 120, 140, 60 };
}

/* 中文按钮（字模居中）。 */
static void draw_btn_cjk(const wq_btn_t *b, const char *label, Color bg)
{
    DrawRectangle(b->x, b->y, b->w, b->h, bg);
    DrawRectangleLines(b->x, b->y, b->w, b->h, TEXT_DARK);
    const int tw = wq_cjk_width(label);
    wq_cjk_draw(label, b->x + (b->w - tw) / 2, b->y + (b->h - 16) / 2, TEXT_LIGHT);
}

static void advance_to_next(game_handle_t game)
{
    const wq_action_t act = wq_session_key(&game->session, WQ_KEY_OK);
    if (act == WQ_ACT_ENTERED) {
        (void)wq_session_key(&game->session, WQ_KEY_OK); /* BRIEF -> SOLVE */
    }
    /* WQ_ACT_LEFT 表示没有下一关：wq_session_key 已把stage 置为 WORLD（回章节地图），
     * 这里不要 goto_level(0) —— 那会把玩家直接扔进第 1 关的棋盘，跳过地图。 */
}

/* 切到下一档音量并落盘（关→20→…→100→关）。有声音时播一个短反馈音。 */
static void cycle_volume(game_handle_t game)
{
    game->volume = wq_volume_next(game->volume);
    wq_sfx_set_volume(game->volume);
    (void)wq_store_save_volume(game->volume);
    if (game->volume != WQ_VOLUME_OFF) wq_sfx_play(WQ_TONE_MOVE);
}

/* 动作后的统一收尾：过关落盘 + 音效反馈。 */
static void after_action(game_handle_t game)
{
    const int act = game->last_action;
    if (act == WQ_ACT_PASSED) {
        const uint16_t lv = game->session.level;
        wq_progress_record_result(&game->progress, lv,
                                  wq_session_result_stars(&game->session));
        game->progress.last_level = lv;
        (void)wq_store_save(&game->progress);
        wq_sfx_play(WQ_TONE_CORRECT);
        return;
    }
    switch (act) {
        case WQ_ACT_PLACED:  wq_sfx_play(WQ_TONE_PLACE);   break;
        case WQ_ACT_MOVED:   wq_sfx_play(WQ_TONE_MOVE);    break;
        case WQ_ACT_ENTERED: wq_sfx_play(WQ_TONE_ENTER);   break;
        case WQ_ACT_LEFT:    wq_sfx_play(WQ_TONE_BACK);    break;
        case WQ_ACT_WRONG:   wq_sfx_play(WQ_TONE_WRONG);   break;
        default:             break;
    }
}

bool game_create(const game_config_t *config, game_handle_t *ret_handle)
{
    if (!ret_handle) return false;
    *ret_handle = NULL;
    if (!config || !config->width || !config->height) return false;
    game_handle_t game = calloc(1, sizeof(*game));
    if (!game) return false;
    game->config = *config;
    game->state.tap_col = -1;
    game->state.tap_row = -1;
    game->last_action = WQ_ACT_NONE;
    wq_progress_reset(&game->progress);
    game->volume = WQ_VOLUME_DEFAULT;
    if (wq_store_init() == ESP_OK) {
        (void)wq_store_load(&game->progress);
        (void)wq_store_load_volume(&game->volume); // 音量独立于进度 blob 持久化
    }
    wq_session_init(&game->session);
    (void)wq_sfx_start(); /* 无喇叭/初始化失败则静音，不影响玩法 */
    wq_sfx_set_volume(game->volume); /* 开机把存档里的档位下发给 ES8311（host 为空操作） */
    /* 启动落在章节页（WORLD），光标定位到上次关卡所在章，便于「继续」。 */
    const uint16_t start = (game->progress.last_level < (uint16_t)WQ_LEVEL_COUNT)
                               ? game->progress.last_level : 0;
    game->session.chapter = chapter_of_level(start);
    const wq_chapter_t *start_ch = &wq_chapters[game->session.chapter];
    game->session.level_cursor = (uint16_t)(start - start_ch->first);
    game->last_stage = game->session.stage; /* 供阶段切换检测（进 BRIEF 复位滚动） */
    *ret_handle = game;
    return true;
}

void game_delete(game_handle_t game)
{
    free(game);
}

void game_reset(game_handle_t game)
{
    if (!game) return;
    game->state = (game_snapshot_t){0};
    game->state.tap_col = -1;
    game->state.tap_row = -1;
    game->last_action = WQ_ACT_NONE;
    game->anim_on = false;
    wq_progress_reset(&game->progress);
    (void)wq_store_load(&game->progress);
    wq_session_init(&game->session);
    game->session.chapter = 0;
    game->session.level_cursor = 0;
    game->brief_scroll = 0;
    game->brief_dragging = false;
    game->brief_layout_valid = false;
    game->last_stage = game->session.stage;
}

void game_set_paused(game_handle_t game, bool paused)
{
    if (game) game->state.paused = paused;
}

/* 实体键（BOOT）→「返回上一层」。语义与各页面已有的返回热区一致：
 *   LEVELS → WORLD（章节页）      BRIEF → 上层（关卡列表）
 *   SOLVE  → 放弃本关（交给状态机，与左上 BACK 热区同一条路径）
 *   RESULT → LEVELS（回关卡列表）  WORLD → 已是根页，不动作
 * WORLD 不动作是刻意的：那是首页，误触把玩家踢出应用没有意义。
 * 只在游戏任务上调用（设备端由按键回调投递事件、在 on_event 里转过来）。 */
void game_request_back(game_handle_t game)
{
    if (!game) return;
    switch (game->session.stage) {
        case WQ_STAGE_LEVELS:
            game->session.stage = WQ_STAGE_WORLD;
            game->last_action = WQ_ACT_LEFT;
            break;
        case WQ_STAGE_BRIEF:
            game->last_action = wq_session_key(&game->session, WQ_KEY_BACK);
            game->anim_on = false;
            break;
        case WQ_STAGE_SOLVE:
            game->last_action = wq_session_key(&game->session, WQ_KEY_BACK);
            break;
        case WQ_STAGE_RESULT:
            game->session.stage = WQ_STAGE_LEVELS;
            game->anim_on = false;
            game->last_action = WQ_ACT_LEFT;
            break;
        default:
            return;
    }
    after_action(game);
}

/* ---- 页面触摸导航 ---- */
static void on_tap_world(game_handle_t game, int x, int y)
{
    /* 右上角音量档位按钮优先于行选择。 */
    const wq_btn_t vb = volume_btn();
    if (btn_hit(&vb, x, y)) {
        cycle_volume(game);
        game->last_action = WQ_ACT_MOVED;
        return;
    }
    /* 与 render_world 同几何：y0=58，row_h=(480-60)/7。 */
    const int y0 = 58;
    const int row_h = (GAME_HEIGHT - 60) / (int)WQ_CHAPTER_COUNT;
    int ch = (y - y0) / row_h;
    if (ch < 0) ch = 0;
    if (ch >= (int)WQ_CHAPTER_COUNT) ch = (int)WQ_CHAPTER_COUNT - 1;
    game->session.chapter = (uint8_t)ch;
    game->session.level_cursor = 0;
    game->session.stage = WQ_STAGE_LEVELS;
    game->last_action = WQ_ACT_MOVED;
}

static void on_tap_levels(game_handle_t game, int x, int y)
{
    /* 左上返回按钮 -> 章节页。 */
    const wq_btn_t back = { 0, 0, 80, 54 };
    if (btn_hit(&back, x, y)) {
        game->session.stage = WQ_STAGE_WORLD;
        game->last_action = WQ_ACT_LEFT;
        return;
    }
    const wq_chapter_t *chapter = &wq_chapters[game->session.chapter];
    const int cols = 6;
    const int cell = GAME_WIDTH / cols;
    const int top = 70;
    const int rows = (chapter->count + cols - 1) / cols;
    int col = x / cell;
    int row = (y - top) / cell;
    const int idx = row * cols + col;
    if (idx < 0 || idx >= (int)chapter->count || row >= rows) return;
    const uint16_t level = (uint16_t)(chapter->first + idx);
    /* 不做任何解锁限制：154 关全部开放，想从哪关进就从哪关进。 */
    game->session.level_cursor = (uint16_t)idx;
    game->session.level = level;
    game->session.wrong_count = 0;
    game->session.scroll = 0;
    game->session.scroll_max = 0;
    game->session.stage = WQ_STAGE_BRIEF;
    game->last_action = WQ_ACT_ENTERED;
}

static void on_tap_brief(game_handle_t game, int x, int y)
{
    const wq_btn_t start = brief_start_btn();
    const wq_btn_t back = brief_back_btn();
    if (btn_hit(&start, x, y)) {
        game->last_action = wq_session_key(&game->session, WQ_KEY_OK);
        game->anim_on = false; /* 新一局进入 SOLVE，清掉上一局的落子动画 */
    } else if (btn_hit(&back, x, y)) {
        game->last_action = wq_session_key(&game->session, WQ_KEY_BACK);
    }
}

static void on_tap_result(game_handle_t game, int x, int y)
{
    const wq_btn_t next = { GAME_WIDTH / 2 - 90, GAME_HEIGHT - 130, 180, 60 };
    const wq_btn_t back = { 20, GAME_HEIGHT - 130, 160, 60 };
    if (btn_hit(&next, x, y)) {
        advance_to_next(game);
        game->anim_on = false;
        game->last_action = WQ_ACT_ENTERED;
    } else if (btn_hit(&back, x, y)) {
        /* 回当前章的关卡列表（可看本关星级 / 选下一关）。 */
        game->session.stage = WQ_STAGE_LEVELS;
        game->anim_on = false;
        game->last_action = WQ_ACT_LEFT;
    }
}

/* 重算 BRIEF 版式：折行 + 滚动上限。按需调用（同一关只算一次）。
 * 必须在 update 路径上就会被调到 —— 手势处理先于渲染，scroll_max 不能只在
 * 渲染里算，否则第一次拖动会读到 0（被夹成"不能滚"）。渲染与手势都只读
 * 这里存下的结果。 */
static void brief_relayout(game_handle_t game)
{
    if (game->brief_layout_valid && game->brief_layout_level == game->session.level) return;
    const wq_level_t *level = wq_session_level(&game->session);
    if (!level) {
        game->brief_layout_valid = false;
        return;
    }
    wq_wrap_utf8(level->instruction, WQ_BRIEF_CHARS_PER_LINE,
                 game->brief_wrapped, sizeof(game->brief_wrapped));
    int lines = 0;
    for (const char *p = game->brief_wrapped; *p; lines++) {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    const int view_h = WQ_BRIEF_BOTTOM - WQ_BRIEF_TEXT_Y;
    const int content_h = lines * WQ_BRIEF_LINE_H;
    const int overflow = content_h > view_h ? content_h - view_h : 0;
    /* 上限取到整行：这样首行与末行都能整行停住，不会卡在半行上
     * （半行既难看，又容易被当成渲染出错）。 */
    game->brief_scroll_max = overflow > 0
        ? ((overflow + WQ_BRIEF_LINE_H - 1) / WQ_BRIEF_LINE_H) * WQ_BRIEF_LINE_H
        : 0;
    game->brief_layout_level = game->session.level;
    game->brief_layout_valid = true;
    if (game->brief_scroll > game->brief_scroll_max) game->brief_scroll = game->brief_scroll_max;
    if (game->brief_scroll < 0) game->brief_scroll = 0;
}

/* 重算 SOLVE 版式：棋盘几何 + 题干行数 + 选项矩形。按需调用（同一关只算一次）。
 *
 * 与 brief_relayout 同理，这些几何是**手势命中**要用的，所以必须在 update 路径上
 * 就算好并缓存在实例里 —— 留给渲染去写，第一次触摸会读到初值。
 *
 * 为什么要缩棋盘：选择题要在同一屏里放下「题干 + 棋盘 + 选项」。题面页虽然已经让
 * 玩家读过题干，但选项本身是「3 / 4 / 5」这种不带语义的碎片，脱离题干就没法选，
 * 所以作答页必须把题干和选项一起显示出来。三样都要，棋盘只能让位。 */
static void solve_layout(game_handle_t game)
{
    const wq_level_t *level = wq_session_level(&game->session);
    const uint16_t idx = level ? (uint16_t)(level - wq_levels) : 0xFFFFu;
    if (game->solve_layout_valid && game->solve_layout_level == idx) return;

    game->solve_layout_level = idx;
    game->solve_layout_valid = true;
    game->solve_q_lines = 0;
    game->solve_opt_count = 0;
    game->solve_opt_vertical = false;
    game->solve_action_label = NULL;
    /* 换关后清掉上一关留下的触摸高亮，免得标在新棋盘上。 */
    game->state.tap_col = -1;
    game->state.tap_row = -1;

    /* 默认满屏棋盘：落子解谜与「移除死子」这类纯棋盘交互沿用历史版式。 */
    int cell = WQ_CELL;
    int origin_x = WQ_BOARD_ORIGIN;
    int origin_y = WQ_BOARD_ORIGIN;
    int band_h = 0;  /* 底部作答区（选项 / 终局确认按钮）高度 */
    int band_y = 0;

    if (level && level->kind == WQ_KIND_CHOICE && level->option_count > 0) {
        /* 题干：中文没有空格，必须自己折行（硬保证每行 <= WQ_SOLVE_Q_CHARS 字）。 */
        wq_wrap_utf8(level->question, WQ_SOLVE_Q_CHARS,
                     game->solve_q_wrapped, sizeof(game->solve_q_wrapped));
        int q_lines = 0;
        for (const char *p = game->solve_q_wrapped; *p; q_lines++) {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
        }
        if (q_lines > WQ_SOLVE_Q_MAX_LINES) q_lines = WQ_SOLVE_Q_MAX_LINES;
        game->solve_q_lines = q_lines;

        int count = level->option_count;
        if (count > WQ_OPTIONS_MAX) count = WQ_OPTIONS_MAX;
        game->solve_opt_count = (uint8_t)count;

        /* 先按横排算按钮宽；只要有一项文本放不下，整关改竖排。
         * 判据必须量出来（line_width_mixed），不能靠字数猜：同一串里
         * ASCII 与全角字符宽度差一倍。 */
        const int bar_w = (GAME_WIDTH - 2 * WQ_SOLVE_SIDE - (count - 1) * WQ_SOLVE_OPT_GAP) / count;
        const int inner = bar_w - WQ_SOLVE_LABEL_W - 8;
        bool horizontal = true;
        for (int i = 0; i < count; i++) {
            const char *text = level->options[i] ? level->options[i] : "";
            if (line_width_mixed(text, 1) > inner) horizontal = false;
        }
        game->solve_opt_vertical = !horizontal;
        band_h = horizontal ? WQ_SOLVE_OPT_H
                            : count * WQ_SOLVE_OPT_ROW_H + (count - 1) * WQ_SOLVE_OPT_GAP;
        band_y = GAME_HEIGHT - WQ_SOLVE_BOTTOM - band_h;
        for (int i = 0; i < count; i++) {
            if (horizontal) {
                game->solve_opt[i] = (wq_btn_t){ WQ_SOLVE_SIDE + i * (bar_w + WQ_SOLVE_OPT_GAP),
                                                 band_y, bar_w, WQ_SOLVE_OPT_H };
            } else {
                game->solve_opt[i] = (wq_btn_t){ WQ_SOLVE_SIDE,
                                                 band_y + i * (WQ_SOLVE_OPT_ROW_H + WQ_SOLVE_OPT_GAP),
                                                 GAME_WIDTH - 2 * WQ_SOLVE_SIDE,
                                                 WQ_SOLVE_OPT_ROW_H };
            }
        }
    } else if (level && (level->kind == WQ_KIND_END_PASS || level->kind == WQ_KIND_END_FINISH)) {
        /* 终局两态的题面写着「请点击『停一手』/『完成』」，所以底部得有个真的按钮，
         * 否则玩家会照着一个不存在的按钮去点。 */
        game->solve_action_label = (level->kind == WQ_KIND_END_PASS) ? "停一手" : "完成";
        game->solve_opt_count = 1;
        band_h = WQ_SOLVE_OPT_H;
        band_y = GAME_HEIGHT - WQ_SOLVE_BOTTOM - band_h;
        game->solve_opt[0] = (wq_btn_t){ (GAME_WIDTH - 200) / 2, band_y, 200, WQ_SOLVE_OPT_H };
    }

    /* 有底部作答区时，棋盘缩进上方剩余空间：取整数格距，再居中。
     * 注意要按「格距」之外再留出**一个棋子半径**（半径 = cell*20/48）：棋盘最后
     * 一条线上的棋子有一半长在棋盘外，只按 8 格算会让最外圈棋子被选项条切掉。
     * 于是约束是 8*cell + 2*半径 <= avail，即 cell <= avail * 48 / 424。
     * 实测最紧的一关（3 个长选项竖排 + 2 行题干）仍能拿到 27px 格距。 */
    if (band_h > 0) {
        const int q_h = game->solve_q_lines * WQ_SOLVE_Q_LINE_H;
        const int board_top = WQ_SOLVE_Q_TOP + (q_h > 0 ? q_h + WQ_SOLVE_GAP : 0);
        int avail = band_y - WQ_SOLVE_GAP - board_top;
        const int avail_max = (WQ_BOARD_LINES - 1) * WQ_CELL;
        if (avail > avail_max) avail = avail_max;
        cell = (avail * WQ_CELL) / ((WQ_BOARD_LINES - 1) * WQ_CELL + 2 * WQ_STONE_RADIUS);
        if (cell > WQ_CELL) cell = WQ_CELL;
        if (cell < WQ_SOLVE_CELL_MIN) cell = WQ_SOLVE_CELL_MIN;
        const int span = (WQ_BOARD_LINES - 1) * cell;
        origin_x = (GAME_WIDTH - span) / 2;
        origin_y = board_top + (avail - span) / 2;
        if (origin_y < board_top) origin_y = board_top;
    }
    game->solve_board_cell = cell;
    game->solve_board_x = origin_x;
    game->solve_board_y = origin_y;
    game->solve_stone_r = cell * WQ_STONE_RADIUS / WQ_CELL;
    if (game->solve_stone_r < 8) game->solve_stone_r = 8;
}

void game_set_pointer(game_handle_t game, int32_t x, int32_t y, bool pressed)
{
    if (!game) return;
    game->state.pointer_x = x < 0 ? 0 : (x >= game->config.width ? game->config.width - 1 : x);
    game->state.pointer_y = y < 0 ? 0 : (y >= game->config.height ? game->config.height - 1 : y);
    const bool was_down = game->state.pointer_down;
    game->state.pointer_down = pressed;

    /* 阶段切换：每次进入 BRIEF，题面都从头看（换关、返回重读都一样）。 */
    if (game->session.stage != game->last_stage) {
        if (game->session.stage == WQ_STAGE_BRIEF) {
            game->brief_scroll = 0;
            game->brief_dragging = false;
            game->brief_layout_valid = false; /* 换了关，折行结果要重算 */
        }
        game->last_stage = game->session.stage;
    }

    const int px = (int)game->state.pointer_x;
    const int py = (int)game->state.pointer_y;

    /* BRIEF 页多一层滚动手势：手指落在题面区时整段按下/移动/抬起都归滚动，
     * 落在底部按钮上仍沿用其它页面的「按下即触发」，这样按钮手感不变。 */
    if (game->session.stage == WQ_STAGE_BRIEF) {
        brief_relayout(game); /* 保证 scroll_max 已就绪（同一关只算一次） */
        const wq_btn_t start = brief_start_btn();
        const wq_btn_t back = brief_back_btn();
        if (pressed && !was_down) {
            if (btn_hit(&start, px, py) || btn_hit(&back, px, py)) {
                on_tap_brief(game, px, py);
                after_action(game);
            } else {
                game->brief_touch_y = py;
                game->brief_scroll_anchor = game->brief_scroll;
                game->brief_dragging = true;
            }
            return;
        }
        if (pressed && game->brief_dragging) {
            int scroll = game->brief_scroll_anchor - (py - game->brief_touch_y);
            if (scroll < 0) scroll = 0;
            if (scroll > game->brief_scroll_max) scroll = game->brief_scroll_max;
            game->brief_scroll = scroll;
            return;
        }
        if (!pressed) {
            if (game->brief_dragging) {
                /* 抬手吸附到整行，停下来的画面永远是整行对齐的。上限本身也是
                 * 整行（见 brief_relayout），所以两端同样对齐。 */
                int snapped = (game->brief_scroll + WQ_BRIEF_LINE_H / 2) / WQ_BRIEF_LINE_H;
                snapped *= WQ_BRIEF_LINE_H;
                if (snapped > game->brief_scroll_max) snapped = game->brief_scroll_max;
                game->brief_scroll = snapped;
            }
            game->brief_dragging = false;
        }
        return;
    }

    if (!pressed) return;

    switch (game->session.stage) {
        case WQ_STAGE_WORLD:
            on_tap_world(game, px, py);
            after_action(game);
            return;
        case WQ_STAGE_LEVELS:
            on_tap_levels(game, px, py);
            after_action(game);
            return;
        case WQ_STAGE_SOLVE: {
            /* 左上 BACK 热区。 */
            const wq_btn_t back = { 0, 0, 70, 44 };
            if (btn_hit(&back, px, py)) {
                game->last_action = wq_session_key(&game->session, WQ_KEY_BACK);
                after_action(game);
                return;
            }
            /* 作答页版式（棋盘原点/格距 + 选项矩形）在这里取：
             * update 已经算过并缓存，这里再调一次是兜底（同一关不会重算）。 */
            solve_layout(game);
            const wq_level_t *level = wq_session_level(&game->session);
            if (!level) break;

            /* 选择题：命中选项按钮即作答；按在别处不动作（不再误当棋盘点）。 */
            if (level->kind == WQ_KIND_CHOICE) {
                for (uint8_t i = 0; i < game->solve_opt_count; i++) {
                    if (btn_hit(&game->solve_opt[i], px, py)) {
                        game->last_action = wq_session_touch_choice(&game->session, i);
                        after_action(game);
                        return;
                    }
                }
                return;
            }

            /* 落到最近交叉点（几何取自实例版式，选择题缩过盘也照样准）。 */
            const int bc = game->solve_board_cell;
            int col = (int)((px - game->solve_board_x + bc / 2) / bc);
            int row = (int)((py - game->solve_board_y + bc / 2) / bc);
            if (col < 0) col = 0;
            if (col >= WQ_BOARD_LINES) col = WQ_BOARD_LINES - 1;
            if (row < 0) row = 0;
            if (row >= WQ_BOARD_LINES) row = WQ_BOARD_LINES - 1;
            game->state.tap_col = (int8_t)col;
            game->state.tap_row = (int8_t)row;
            const uint8_t point = (uint8_t)(row * WQ_BOARD + col);
            if (level->kind == WQ_KIND_END_PASS || level->kind == WQ_KIND_END_FINISH) {
                /* 底部按钮是提示出来的那一处，但点棋盘其它地方也接受（老手感不变）。 */
                game->last_action = wq_session_touch_confirm(&game->session);
            } else {
                game->last_action = wq_session_touch(&game->session, point);
            }
            /* 落子缩放动画：puzzle 真正落子后记录玩家刚下的这一格。
             * 走错复位则清掉动画（该格此时为空或回到初始棋面）。 */
            if (game->last_action == WQ_ACT_PLACED && level->kind == WQ_KIND_PUZZLE) {
                game->anim_on = true;
                game->anim_start_tick = game->state.tick;
                game->anim_col = (int8_t)col;
                game->anim_row = (int8_t)row;
            } else if (game->last_action == WQ_ACT_WRONG) {
                game->anim_on = false;
            }
            after_action(game);
            return;
        }
        case WQ_STAGE_RESULT:
            on_tap_result(game, px, py);
            after_action(game);
            return;
        default:
            return;
    }
}

void game_update(game_handle_t game)
{
    if (!game || game->state.paused) return;
    /* 版式都在这里算：手势处理（game_set_pointer）跑在渲染之前，
     * 若把 scroll_max / 棋盘几何留给渲染去写，第一次触摸会读到初值。 */
    if (game->session.stage == WQ_STAGE_BRIEF) brief_relayout(game);
    if (game->session.stage == WQ_STAGE_SOLVE) solve_layout(game);
    ++game->state.tick;
}

/* 落子动画：从 55% 缩放到满半径，时长（tick）。仅视觉，不影响逻辑。 */
#define WQ_ANIM_TICKS 12

/* 画一颗棋子（带描边，白子可见）。radius 可调，供落子缩放动画使用。 */
static void draw_stone_r(int cx, int cy, uint8_t color, float radius)
{
    if (color == WQ_COLOR_BLACK) {
        DrawCircle(cx, cy, radius, BLACK);
        DrawCircleLines(cx, cy, radius, GRAY);
    } else if (color == WQ_COLOR_WHITE) {
        DrawCircle(cx, cy, radius, WHITE);
        DrawCircleLines(cx, cy, radius, GRAY);
    }
}

/* 某格当前是否应按动画半径绘制。满期后返回 false，走正常绘制。 */
static bool stone_anim_radius(const game_handle_t game, int col, int row,
                              float *radius)
{
    if (!game->anim_on || col != game->anim_col || row != game->anim_row)
        return false;
    const int dt = game->state.tick - game->anim_start_tick;
    if (dt < 0 || dt >= WQ_ANIM_TICKS) return false;
    const float t = (float)dt / (float)WQ_ANIM_TICKS; /* 0..1 */
    /* 半径基准取当前版式的棋子半径：选择题棋盘会缩小，动画要跟着缩。 */
    *radius = (float)game->solve_stone_r * (0.55f + 0.45f * t);
    return true;
}

/* 画 k 颗星（0..3），亮 / 暗。 */
static void draw_stars(int cx, int cy, int count, int gap)
{
    for (int i = 0; i < 3; i++) {
        const Color c = (i < count) ? GOLD : DARKGRAY;
        DrawCircle(cx + i * gap, cy, 8, c);
    }
}

/* ---- WORLD：章节列表 ---- */
static void render_world(game_handle_t game)
{
    ClearBackground(BOARD_BG);
    wq_cjk_draw("章节选择", 20, 14, TEXT_DARK);
    /* 右上角音量档位按钮：显示当前档，触摸循环切档。 */
    {
        const wq_btn_t vb = volume_btn();
        DrawRectangle(vb.x, vb.y, vb.w, vb.h, DARKGRAY);
        DrawRectangleLines(vb.x, vb.y, vb.w, vb.h, TEXT_DARK);
        wq_cjk_draw("音量", vb.x + 10, vb.y + 11, TEXT_LIGHT);
        char vbuf[8];
        wq_volume_text(game->volume, vbuf, sizeof(vbuf));
        DrawText(vbuf, vb.x + 60, vb.y + 10, 18, TEXT_LIGHT);
    }
    const int row_h = (GAME_HEIGHT - 60) / (int)WQ_CHAPTER_COUNT;
    const int y0 = 58;
    for (uint8_t ch = 0; ch < (uint8_t)WQ_CHAPTER_COUNT; ch++) {
        const wq_chapter_t *chapter = &wq_chapters[ch];
        const int y = y0 + ch * row_h;
        const bool sel = (ch == game->session.chapter);
        DrawRectangle(16, y + 4, GAME_WIDTH - 32, row_h - 8,
                      sel ? BAR_ACTIVE : PANEL_BG);
        DrawRectangleLines(16, y + 4, GAME_WIDTH - 32, row_h - 8, TEXT_DARK);
        /* 章节中文名（如「青铜 · 入门启蒙」）。 */
        wq_cjk_draw(chapter->title, 30, y + 16, TEXT_DARK);
        /* 章进度：通关数 / 总星（ASCII）。 */
        uint16_t cleared = 0;
        unsigned int stars = 0;
        for (uint16_t li = chapter->first;
             li < (uint16_t)(chapter->first + chapter->count); li++) {
            const uint8_t s = wq_progress_stars(&game->progress, li);
            if (s > 0) cleared++;
            stars += s;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%u/%u *%u", cleared, chapter->count, stars);
        DrawText(buf, 320, y + 20, 18, TEXT_DARK);
        /* 章节进度条：通关数 / 总数，细条贴在卡片底部。 */
        const int bar_x = 30, bar_w = GAME_WIDTH - 60;
        const int bar_y = y + row_h - 12, bar_h = 5;
        DrawRectangle(bar_x, bar_y, bar_w, bar_h, DARKGRAY);
        if (chapter->count > 0) {
            const int fill = bar_w * (int)cleared / (int)chapter->count;
            DrawRectangle(bar_x, bar_y, fill, bar_h, BAR_ACTIVE);
        }
    }
}

/* ---- LEVELS：关卡网格 ---- */
static void render_levels(game_handle_t game)
{
    ClearBackground(BOARD_BG);
    const wq_chapter_t *chapter = &wq_chapters[game->session.chapter];

    /* 左上返回按钮。 */
    DrawRectangle(0, 0, 80, 54, DARKGRAY);
    DrawRectangleLines(0, 0, 80, 54, TEXT_DARK);
    wq_cjk_draw("返回", 18, 18, TEXT_LIGHT);
    /* 章名标题（中文）。 */
    wq_cjk_draw(chapter->title, 92, 18, TEXT_DARK);

    const int cols = 6;
    const int cell = GAME_WIDTH / cols;
    const int top = 70;
    for (uint16_t i = 0; i < chapter->count; i++) {
        const int col = (int)(i % cols);
        const int row = (int)(i / cols);
        const int x = col * cell + 4;
        const int y = top + row * cell + 4;
        const uint16_t level = (uint16_t)(chapter->first + i);
        const uint8_t stars = wq_progress_stars(&game->progress, level);
        /* 全部开放：没有锁定态，只把「上次玩到的那一关」挑出来高亮。 */
        const bool current = (level == game->progress.last_level);
        const Color bg = current ? BAR_ACTIVE : PANEL_BG;
        DrawRectangle(x, y, cell - 8, cell - 8, bg);
        DrawRectangleLines(x, y, cell - 8, cell - 8, TEXT_DARK);
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", i + 1);
        DrawText(buf, x + 8, y + 6, 20, TEXT_DARK);
        /* 星级圆点（已通关才有星）。 */
        draw_stars(x + 6, y + cell - 16, stars, 16);
    }
}

/* ---- BRIEF：题面摘要（标题 + 折行题面正文） ---- */
static void render_brief(game_handle_t game)
{
    ClearBackground(BOARD_BG);
    const wq_level_t *level = wq_session_level(&game->session);
    if (level) {
        /* 标题行：关卡中文名 + 关号（同样按 2 倍字号）。 */
        draw_line_mixed_scaled(level->title, 16, WQ_BRIEF_TITLE_Y, TEXT_DARK,
                               WQ_BRIEF_SCALE);
        char buf[32];
        snprintf(buf, sizeof(buf), "#%u", (unsigned)(level - wq_levels) + 1);
        DrawText(buf, GAME_WIDTH - 78, WQ_BRIEF_TITLE_Y + 4, 24, TEXT_DARK);
        DrawLine(16, WQ_BRIEF_DIVIDER_Y, GAME_WIDTH - 16, WQ_BRIEF_DIVIDER_Y, GRID_LINE);

        /* 题面正文：折行结果与滚动上限由 brief_relayout 算好（与手势共用）。
         * 正文裁到 [TEXT_Y, BOTTOM) 这块矩形里 —— 否则滚动后最上面那行会
         * 压到标题与分隔线上（半行露在视口外）。 */
        brief_relayout(game);
        BeginScissorMode(0, WQ_BRIEF_TEXT_Y, GAME_WIDTH, WQ_BRIEF_BOTTOM - WQ_BRIEF_TEXT_Y);

        int y = WQ_BRIEF_TEXT_Y - game->brief_scroll;
        for (const char *line = game->brief_wrapped; *line; ) {
            /* 找到这一行的结尾（换行或 NUL）。 */
            const char *nl = line;
            while (*nl && *nl != '\n') nl++;
            char one_line[WQ_BRIEF_CHARS_PER_LINE * 3 + 1];
            const size_t n = (size_t)(nl - line);
            if (n < sizeof(one_line)) {
                memcpy(one_line, line, n);
                one_line[n] = '\0';
                /* 完全在视口外的行直接跳过（半行交给裁剪区处理）。 */
                if (y > WQ_BRIEF_TEXT_Y - WQ_BRIEF_LINE_H && y < WQ_BRIEF_BOTTOM) {
                    draw_line_mixed_scaled(one_line, 16, y, TEXT_DARK, WQ_BRIEF_SCALE);
                }
                y += WQ_BRIEF_LINE_H;
            }
            line = nl;
            if (*line == '\n') line++;
        }
        EndScissorMode();

        /* 内容超一屏时在右侧画一个滚动位置指示（纯图形，不引入新字模）。 */
        if (game->brief_scroll_max > 0) {
            const int view_h = WQ_BRIEF_BOTTOM - WQ_BRIEF_TEXT_Y;
            const int content_h = view_h + game->brief_scroll_max;
            const int track_x = GAME_WIDTH - 10;
            const int thumb_h = view_h * view_h / content_h;
            const int thumb_y = WQ_BRIEF_TEXT_Y +
                                (view_h - thumb_h) * game->brief_scroll / game->brief_scroll_max;
            DrawRectangle(track_x, WQ_BRIEF_TEXT_Y, 5, view_h, DARKGRAY);
            DrawRectangle(track_x, thumb_y, 5, thumb_h, TEXT_DARK);
        }
    }
    const wq_btn_t start = brief_start_btn();
    const wq_btn_t back = brief_back_btn();
    draw_btn_cjk(&start, "开始", BUTTON_BG);
    draw_btn_cjk(&back, "返回", DARKGRAY);
}

/* ---- RESULT：结算 ---- */
static void render_result(game_handle_t game)
{
    ClearBackground(BOARD_BG);
    const uint8_t stars = wq_session_result_stars(&game->session);
    const char *label = (stars >= 3) ? "PERFECT" : (stars == 2) ? "GOOD" : "CLEAR";
    DrawText(label, 40, 60, 40, TEXT_DARK);
    /* 「过关」跟在标签**实际渲染宽度**之后：PERFECT 比 CLEAR 宽得多，
     * 写死 x 会让三种标签里至少一种叠字（40px 的 ASCII 比同字数中文宽）。 */
    wq_cjk_draw("过关", 40 + MeasureText(label, 40) + 16, 70, TEXT_DARK);
    draw_stars(140, 180, stars, 70);
    char buf[48];
    snprintf(buf, sizeof(buf), "wrong:%u", (unsigned)game->session.wrong_count);
    DrawText(buf, 40, 240, 22, TEXT_DARK);
    const wq_btn_t next = { GAME_WIDTH / 2 - 90, GAME_HEIGHT - 130, 180, 60 };
    const wq_btn_t back = { 20, GAME_HEIGHT - 130, 160, 60 };
    draw_btn_cjk(&next, "下一关", BUTTON_BG);
    draw_btn_cjk(&back, "回列表", DARKGRAY);
}

/* 选择题的选项按钮：左侧字母栏 + 右侧选项正文（正文居中，按实测宽度）。
 * 选项正文必须画出来 —— 题库里 30/32 关的选项就是「3 / 4 / 5」这种裸数字，
 * 只画 A/B/C 等于把答案藏了。 */
static void draw_choice_btn(const wq_btn_t *b, char label, const char *text, bool active)
{
    DrawRectangle(b->x, b->y, b->w, b->h, active ? BAR_ACTIVE : DARKGRAY);
    DrawRectangleLines(b->x, b->y, b->w, b->h, TEXT_DARK);

    char lab[2] = { label, '\0' };
    DrawText(lab, b->x + (WQ_SOLVE_LABEL_W - MeasureText(lab, WQ_TEXT_PX)) / 2,
             b->y + (b->h - WQ_TEXT_PX) / 2, WQ_TEXT_PX, TEXT_LIGHT);
    DrawLine(b->x + WQ_SOLVE_LABEL_W, b->y + 6,
             b->x + WQ_SOLVE_LABEL_W, b->y + b->h - 6, TEXT_DARK);

    const int tw = line_width_mixed(text, 1);
    int tx = b->x + WQ_SOLVE_LABEL_W + (b->w - WQ_SOLVE_LABEL_W - tw) / 2;
    const int tx_min = b->x + WQ_SOLVE_LABEL_W + 3;
    if (tx < tx_min) tx = tx_min;
    draw_line_mixed_scaled(text, tx, b->y + (b->h - WQ_TEXT_PX) / 2, TEXT_LIGHT, 1);
}

/* 终局态的确认按钮（「停一手」/「完成」）：题面写着请点击它，就得真的有。 */
static void draw_solve_action_btn(const wq_btn_t *b, const char *label, bool active)
{
    DrawRectangle(b->x, b->y, b->w, b->h, active ? BAR_ACTIVE : BUTTON_BG);
    DrawRectangleLines(b->x, b->y, b->w, b->h, TEXT_DARK);
    const int tw = line_width_mixed(label, 1);
    draw_line_mixed_scaled(label, b->x + (b->w - tw) / 2,
                           b->y + (b->h - WQ_TEXT_PX) / 2, TEXT_LIGHT, 1);
}

/* ---- SOLVE：作答 ---- */
static void render_solve(game_handle_t game)
{
    ClearBackground(BOARD_BG);
    const wq_level_t *level = wq_session_level(&game->session);
    /* 版式（棋盘几何 + 题干行数 + 选项矩形）由 solve_layout 算好，渲染只读；
     * 选择题的棋盘会比满屏小，所以几何一律取实例字段，不用常量。 */
    solve_layout(game);
    const int bx = game->solve_board_x;
    const int by = game->solve_board_y;
    const int bc = game->solve_board_cell;
    const int br = game->solve_stone_r;
    const int span = (WQ_BOARD_LINES - 1) * bc;

    /* 左上 BACK 热区。 */
    DrawRectangle(0, 0, 70, 44, DARKGRAY);
    DrawText("<", 24, 8, 28, TEXT_LIGHT);

    /* 网格 + 星位。 */
    for (int i = 0; i < WQ_BOARD_LINES; i++) {
        DrawLine(bx, by + i * bc, bx + span, by + i * bc, GRID_LINE);
        DrawLine(bx + i * bc, by, bx + i * bc, by + span, GRID_LINE);
    }
    for (int s = 0; s < 5; s++) {
        DrawCircle(bx + star_cols[s] * bc, by + star_rows[s] * bc,
                   (bc >= 40) ? 4 : 3, STAR_POINT);
    }

    /* 真实盘面。 */
    for (int r = 0; r < WQ_BOARD_LINES; r++) {
        for (int c = 0; c < WQ_BOARD_LINES; c++) {
            const uint8_t color = game->session.board.cells[r * WQ_BOARD + c];
            if (color == WQ_COLOR_EMPTY) continue;
            const int cx = bx + c * bc;
            const int cy = by + r * bc;
            float ar = (float)br;
            if (stone_anim_radius(game, c, r, &ar)) {
                draw_stone_r(cx, cy, color, ar);
            } else {
                draw_stone_r(cx, cy, color, (float)br);
            }
        }
    }

    /* 标记字形（A/B/C... ASCII）。居中于交叉点：写死的偏移在棋盘缩放后必偏。
     * 按实际长度拷贝而非固定 2 字节：若内容里混入多字节 UTF-8，
     * 固定 char g[2] 会腰斩字符并越界读。 */
    if (level) {
        for (uint8_t mi = 0; mi < level->mark_count && mi < WQ_MARKS_MAX; mi++) {
            const uint8_t p = level->mark_points[mi];
            const char *glyph = level->mark_glyphs[mi];
            if (glyph && glyph[0]) {
                char g[8];
                size_t glen = 0;
                while (glyph[glen] != '\0' && glen < sizeof(g) - 1) {
                    g[glen] = glyph[glen];
                    glen++;
                }
                g[glen] = '\0';
                DrawText(g, bx + (p % WQ_BOARD) * bc - MeasureText(g, 16) / 2,
                         by + (p / WQ_BOARD) * bc - 8, 16, TEXT_LIGHT);
            }
        }
    }

    /* 触摸高亮（选择题不做落子，不画）。 */
    if (game->state.tap_col >= 0 && game->state.tap_row >= 0 &&
        level && level->kind != WQ_KIND_CHOICE) {
        DrawCircleLines(bx + game->state.tap_col * bc, by + game->state.tap_row * bc,
                        br + 4, HIGHLIGHT);
    }

    /* 选择题题干：选项是「3 / 4 / 5」这种脱离题干就没有意义的碎片，
     * 所以作答页把题干一起显示（题面页读过一遍也会忘）。 */
    if (level && level->kind == WQ_KIND_CHOICE && game->solve_q_lines > 0) {
        int qy = WQ_SOLVE_Q_TOP;
        const char *line = game->solve_q_wrapped;
        for (int i = 0; i < game->solve_q_lines && *line; i++) {
            const char *nl = line;
            while (*nl && *nl != '\n') nl++;
            char one_line[WQ_SOLVE_Q_CHARS * 3 + 1];
            const size_t n = (size_t)(nl - line);
            if (n < sizeof(one_line)) {
                memcpy(one_line, line, n);
                one_line[n] = '\0';
                draw_line_mixed_scaled(one_line, 16, qy, TEXT_DARK, 1);
            }
            qy += WQ_SOLVE_Q_LINE_H;
            line = nl;
            if (*line == '\n') line++;
        }
        DrawLine(16, qy + 2, GAME_WIDTH - 16, qy + 2, GRID_LINE);
    }

    /* 底部作答区：选项按钮 / 终局确认按钮。 */
    for (uint8_t i = 0; i < game->solve_opt_count; i++) {
        const wq_btn_t *b = &game->solve_opt[i];
        if (game->solve_action_label) {
            const bool down = game->state.pointer_down &&
                              btn_hit(b, (int)game->state.pointer_x, (int)game->state.pointer_y);
            draw_solve_action_btn(b, game->solve_action_label, down);
        } else {
            const char *text = (level && i < level->option_count) ? level->options[i] : "";
            /* 高亮跟随**按下**，不跟随 session.option：那是键盘光标，触摸输入下
             * 恒为 0，会让人误以为"A 已经选中了"。 */
            const bool down = game->state.pointer_down &&
                              btn_hit(b, (int)game->state.pointer_x, (int)game->state.pointer_y);
            draw_choice_btn(b, (char)('A' + i), text ? text : "", down);
        }
    }

    /* 顶部信息。 */
    char buf[48];
    if (level) {
        const uint16_t n = (uint16_t)(level - wq_levels);
        snprintf(buf, sizeof(buf), "L%u err:%u", n + 1, (unsigned)game->session.wrong_count);
    } else {
        snprintf(buf, sizeof(buf), "err:%u", (unsigned)game->session.wrong_count);
    }
    DrawText(buf, 90, 8, 20, TEXT_DARK);
}

bool game_render(game_handle_t game)
{
    if (!game) return false;
    BeginDrawing();
    if (!MosaicoFastFrameAvailable()) {
        EndDrawing();
        return false;
    }

    switch (game->session.stage) {
        case WQ_STAGE_WORLD:
            render_world(game);
            break;
        case WQ_STAGE_LEVELS:
            render_levels(game);
            break;
        case WQ_STAGE_BRIEF:
            render_brief(game);
            break;
        case WQ_STAGE_SOLVE:
            render_solve(game);
            break;
        case WQ_STAGE_RESULT:
            render_result(game);
            break;
        default:
            ClearBackground(BOARD_BG);
            break;
    }

    EndDrawing();
    return true;
}

game_snapshot_t game_read(game_handle_t game)
{
    return game ? game->state : (game_snapshot_t){0};
}
