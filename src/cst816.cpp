/**
 * @file cst816.cpp
 * @brief CST816D register access, coordinate rotation and edge detection.
 */

#include "touch.h"
#include "board_desc.h"
#include "config.h"
#include "gfx.h"

#include "plat.h"
#include <hardware/i2c.h>
#include <hardware/gpio.h>

/*
 * Compiled unconditionally now, and wired from @ref g_board rather than from
 * board_pins.h macros. Both drivers used to be #if'd out on the board they were
 * not for, which is what made a unified image impossible: each exported exactly
 * touchInit and touchPoll, so compiling both was a duplicate-symbol error.
 * They export a TouchDriver each instead, and the descriptor names which one.
 */

/** @brief I2C instance the touch controller sits on. @see board_desc.h */
#define TOUCH_I2C ((i2c_inst_t *)g_board->i2c)
/** @brief 7-bit address, from the descriptor. */
#define TOUCH_ADDR (g_board->tpAddr)

/** @brief Per-transfer I2C timeout. Long enough for a 400 kHz byte, short
 *         enough that a wedged bus does not stall the UI. */
#define I2C_TIMEOUT_US 2000

/**
 * @defgroup cst816_regs CST816 register map
 * @brief Only the subset this driver needs.
 * @{
 */
#define REG_GESTURE       0x01 /**< Gesture id. */
#define REG_FINGER_NUM    0x02 /**< Contact count, low nibble. */
#define REG_XPOS_H        0x03 /**< X high nibble; X low, Y high, Y low follow. */
#define REG_CHIP_ID       0xA7 /**< Chip identifier, read to probe presence. */
#define REG_IRQ_CTL       0xFA /**< Interrupt enable bits. */
#define REG_DIS_AUTOSLEEP 0xFE /**< Non-zero disables auto-sleep. */
/** @} */

static bool s_prevDown = false;        /**< Contact state at the previous poll. */
static int16_t s_downX = 0, s_downY = 0; /**< Where the current press started. */

/**
 * @brief Write one register.
 * @return true if the device acknowledged.
 */
static bool wrReg(uint8_t reg, uint8_t val) {
	uint8_t b[2] = { reg, val };
	// Timed out rather than blocking: this bus is shared with the IMU, and a
	// device holding SDA low would otherwise hang the UI thread forever.
	int n = i2c_write_timeout_us(TOUCH_I2C, TOUCH_ADDR, b, 2, false,
	                             I2C_TIMEOUT_US);
	return n == 2;
}

/**
 * @brief Read @p n consecutive registers starting at @p reg.
 * @return true if the full read completed.
 */
static bool rdRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
	// nostop=true leaves a repeated start between the write and the read, which
	// is what the controller expects; a stop in between makes it forget the
	// register pointer.
	if (i2c_write_timeout_us(TOUCH_I2C, TOUCH_ADDR, &reg, 1, true,
	                         I2C_TIMEOUT_US) != 1) return false;
	return i2c_read_timeout_us(TOUCH_I2C, TOUCH_ADDR, buf, n, false,
	                           I2C_TIMEOUT_US) == n;
}

/** @brief Bring the CST816D up. @return true if it answered an identifying read. */
static bool cst816Init() {
	gpio_init(g_board->tpInt);
	gpio_set_dir(g_board->tpInt, GPIO_IN);
	gpio_pull_up(g_board->tpInt);

	i2c_init(TOUCH_I2C, 400 * 1000);
	gpio_set_function(g_board->sda, GPIO_FUNC_I2C);
	gpio_set_function(g_board->scl, GPIO_FUNC_I2C);
	gpio_pull_up(g_board->sda);
	gpio_pull_up(g_board->scl);

	// The touch chip shares its RESET net with the LCD, so st7789Init() has
	// already pulsed it. Give the controller time to come up.
	delay(60);

	uint8_t id = 0;
	bool ok = rdRegs(REG_CHIP_ID, &id, 1);

	// Keep the controller awake so we can poll it, and enable both
	// touch-detect and change interrupts on TP_INT.
	wrReg(REG_DIS_AUTOSLEEP, 0xFF);
	wrReg(REG_IRQ_CTL, 0x41);

	return ok;
}

/** @brief Consecutive failed polls before a glitch counts as a release. */
#define POLL_FAIL_LIMIT 2

/** @brief How many polls in a row the controller has failed to answer. */
static uint8_t s_pollFails = 0;

/** @brief Sample the CST816D. @param[in,out] t State to update. */
static void cst816Poll(TouchState *t) {
	uint8_t b[6];
	bool down = false;
	int16_t x = t->x, y = t->y;
	uint8_t gesture = 0;

	if (rdRegs(REG_GESTURE, b, 6)) {
		s_pollFails = 0;
		gesture = b[0];
		uint8_t fingers = b[1] & 0x0F;
		if (fingers > 0) {
			int16_t nx = (int16_t)(((b[2] & 0x0F) << 8) | b[3]);
			int16_t ny = (int16_t)(((b[4] & 0x0F) << 8) | b[5]);
			if (nx < TOUCH_NATIVE_W && ny < TOUCH_NATIVE_H) {
				touchMapCoords(nx, ny, &x, &y);
				down = true;
			}
		}
	} else {
		// One failed read mid-press is a bus glitch, not a lift-off. Treating
		// it as a release used to *complete a tap* at wherever the finger was
		// resting -- under the fire-on-release rule that is a commit, not a
		// repeat -- so a single failure holds the previous state, and only
		// POLL_FAIL_LIMIT consecutive ones report the finger up.
		if (s_pollFails < POLL_FAIL_LIMIT) s_pollFails++;
		if (s_pollFails < POLL_FAIL_LIMIT) down = s_prevDown;
	}

	if (x < 0) x = 0; else if (x >= GFX_W) x = GFX_W - 1;
	if (y < 0) y = 0; else if (y >= GFX_H) y = GFX_H - 1;

	t->pressed  = down && !s_prevDown;
	t->released = !down && s_prevDown;
	t->down     = down;
	t->x = x;
	t->y = y;
	t->gesture = gesture;

	if (t->pressed) { s_downX = x; s_downY = y; }
	t->downX = s_downX;
	t->downY = s_downY;

	s_prevDown = down;
}

/**
 * @brief The CST816D driver, as named by the 2.0" board's descriptor.
 *
 * `extern` is load-bearing: a namespace-scope `const` has internal linkage in
 * C++, so without it this object is invisible to board_desc_lcd2.cpp and the
 * link fails on a symbol that is plainly right there in the file.
 */
extern const TouchDriver TOUCH_DRIVER_CST816D;
extern const TouchDriver TOUCH_DRIVER_CST816D = { "CST816D", cst816Init, cst816Poll };
