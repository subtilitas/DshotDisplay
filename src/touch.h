/**
 * @file touch.h
 * @brief Capacitive touch interface, independent of which controller is fitted.
 *
 * Both implementations always compile, and the board descriptor names which one
 * to call. They used to be mutually exclusive `#if`s, each exporting exactly
 * `touchInit` and `touchPoll` -- which is a duplicate-symbol error the moment
 * an image has to be able to drive either board.
 *
 * | Board | Controller | Driver | Address |
 * |---|---|---|---|
 * | RP2350-Touch-LCD-2 | CST816D | `cst816.cpp` | 0x15, 8-bit registers |
 * | RP2350-Touch-LCD-2.8 | CST328 | `cst328.cpp` | 0x1A, 16-bit registers |
 *
 * Both panels report coordinates in a native 240x320 portrait frame. Both
 * drivers rotate them to match @ref LCD_ROTATION, so UI code only ever sees
 * framebuffer coordinates and never learns which chip it is talking to.
 */

#pragma once

#include <stdint.h>

#include "config.h"
#include "board_desc.h"

/** @brief Native panel width, in portrait, independent of @ref LCD_ROTATION. */
#define TOUCH_NATIVE_W 240
/** @brief Native panel height, in portrait, independent of @ref LCD_ROTATION. */
#define TOUCH_NATIVE_H 320

/**
 * @brief Map native portrait coordinates into the active framebuffer frame.
 *
 * Lives here rather than in either driver because both panels report in the
 * same native frame, and a rotation that is correct on one board and stale on
 * the other is a bug nobody would find without owning both.
 *
 * @param      nx,ny Native panel coordinates.
 * @param[out] ox,oy Framebuffer coordinates for the current @ref LCD_ROTATION.
 */
static inline void touchMapCoords(int16_t nx, int16_t ny, int16_t *ox, int16_t *oy) {
#if LCD_ROTATION == 1
	*ox = ny;
	*oy = (int16_t)(TOUCH_NATIVE_W - 1 - nx);
#elif LCD_ROTATION == 2
	*ox = (int16_t)(TOUCH_NATIVE_W - 1 - nx);
	*oy = (int16_t)(TOUCH_NATIVE_H - 1 - ny);
#elif LCD_ROTATION == 3
	*ox = (int16_t)(TOUCH_NATIVE_H - 1 - ny);
	*oy = nx;
#else
	*ox = nx;
	*oy = ny;
#endif
}

/**
 * @brief Snapshot of the touch panel for one poll.
 *
 * Edge flags are computed against the previous poll, so a caller that polls at
 * a fixed rate sees exactly one `pressed` and one `released` per contact.
 */
struct TouchState {
	bool     down;      /**< Finger currently on the glass. */
	bool     pressed;   /**< Rising edge: contact started this poll. */
	bool     released;  /**< Falling edge: contact ended this poll. */
	int16_t  x;         /**< Current X, framebuffer coordinates. */
	int16_t  y;         /**< Current Y, framebuffer coordinates. */
	int16_t  downX;     /**< X where the current press started. */
	int16_t  downY;     /**< Y where the current press started. */
	uint8_t  gesture;   /**< Controller gesture id, 0 if it reports none. */
};

/**
 * @brief Start the touch bus and configure the controller.
 *
 * @pre st7789Init() must already have run. On the 2.0" board that is a hard
 *      requirement — it owns the reset line both chips share. On the 2.8" board
 *      the reset lines are separate and this driver pulses its own, but the
 *      ordering is kept identical so callers need not care which board it is.
 *
 * @return true if the controller answered an identifying read.
 */
bool touchInit();

/**
 * @brief The driver the active board names, or nullptr before one is selected.
 * @return Its TouchDriver.
 */
const TouchDriver *touchActiveDriver();

/**
 * @brief Poll the controller and update @p t.
 *
 * Safe to call at any rate; ~60 Hz is plenty. On a failed I2C transaction the
 * position is left unchanged and the finger is reported as up.
 *
 * @param[in,out] t State to update. Must persist between calls.
 */
void touchPoll(TouchState *t);
