/**
 * @file board_desc.cpp
 * @brief Which descriptor is active, and which ones this image holds.
 *
 * The selection list is built by the preprocessor from @ref BOARD, so a
 * single-board build carries exactly one descriptor and a unified build carries
 * both. That keeps the single-board images available -- they are smaller, and
 * they are what proves the preprocessor path has not rotted -- while the
 * unified one is a build option rather than a fork.
 */

#include "board_desc.h"
#include "board.h"

#include <stddef.h>

/** @brief One entry per board this image can drive. */
struct BoardEntry {
	uint8_t id;              /**< @see board_desc_ids */
	const BoardDesc *desc;   /**< Its descriptor. */
};

static const BoardEntry BOARDS[] = {
#if BOARD == BOARD_RP2350_TOUCH_LCD_2 || BOARD == BOARD_UNIFIED
	{ BOARD_ID_LCD_2,   &BOARD_DESC_LCD_2 },
#endif
#if BOARD == BOARD_RP2350_TOUCH_LCD_2_8 || BOARD == BOARD_UNIFIED
	{ BOARD_ID_LCD_2_8, &BOARD_DESC_LCD_2_8 },
#endif
};

static const int BOARD_N = (int)(sizeof(BOARDS) / sizeof(BOARDS[0]));
static_assert(sizeof(BOARDS) / sizeof(BOARDS[0]) >= 1,
              "a build must be able to drive at least one board");

// Defaults to the first entry, so a single-board build never needs to select
// and a unified one has something coherent to draw with before the user picks.
const BoardDesc *g_board = BOARDS[0].desc;
static uint8_t s_boardId = BOARDS[0].id;

bool boardSelect(uint8_t id) {
	for (int i = 0; i < BOARD_N; i++) {
		if (BOARDS[i].id == id) {
			g_board = BOARDS[i].desc;
			s_boardId = id;
			return true;
		}
	}
	return false;
}

uint8_t boardId() { return s_boardId; }
int boardCount() { return BOARD_N; }

const BoardDesc *boardAt(int i) {
	return (i >= 0 && i < BOARD_N) ? BOARDS[i].desc : NULL;
}

uint8_t boardIdAt(int i) {
	return (i >= 0 && i < BOARD_N) ? BOARDS[i].id : BOARD_ID_UNSET;
}
