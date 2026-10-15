// SPDX-License-Identifier: Apache-2.0
#pragma once

#define GAME_NAME "weiqi_quest"
enum { GAME_WIDTH = 480, GAME_HEIGHT = 480, GAME_TICK_HZ = 30 };
enum { GAME_ACTION_PAUSE = 3, GAME_ACTION_RESET = 4 };

/* Board geometry: 9x9 intersections, cell 48px, board 384px centered in 480px. */
enum { WQ_BOARD_LINES = 9 };
enum { WQ_CELL = 48 };
enum { WQ_BOARD_ORIGIN = (GAME_WIDTH - (WQ_BOARD_LINES - 1) * WQ_CELL) / 2 }; /* 48 */
enum { WQ_STONE_RADIUS = 20 };
