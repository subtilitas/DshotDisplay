/**
 * @file cst816.h
 * @brief CST816D capacitive touch controller driver (I2C0, address 0x15).
 *
 * The panel reports coordinates in its native 240x320 portrait frame. This
 * driver rotates them to match @ref LCD_ROTATION, so UI code only ever sees
 * framebuffer coordinates.
 */

#pragma once

#include <stdint.h>

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
	uint8_t  gesture;   /**< Raw CST816 gesture id, passed through unfiltered. */
};

/**
 * @brief Start I2C0 and configure the controller.
 *
 * Disables the chip's auto-sleep so it can be polled, and enables touch-detect
 * and change interrupts on @ref PIN_TP_INT.
 *
 * @pre st7789Init() must already have run — it owns the shared reset line.
 * @return true if the controller acknowledged a chip-ID read.
 */
bool touchInit();

/**
 * @brief Poll the controller and update @p t.
 *
 * Safe to call at any rate; ~60 Hz is plenty. On a failed I2C transaction the
 * position is left unchanged and the finger is reported as up.
 *
 * @param[in,out] t State to update. Must persist between calls.
 */
void touchPoll(TouchState *t);
