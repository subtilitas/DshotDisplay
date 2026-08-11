/**
 * @file cst816.cpp
 * @brief CST816D register access, coordinate rotation and edge detection.
 */

#include "cst816.h"
#include "board_pins.h"
#include "config.h"
#include "gfx.h"

#include "plat.h"
#include <hardware/i2c.h>
#include <hardware/gpio.h>

/** @brief I2C instance the touch controller sits on. @see board_pins.h */
#define TOUCH_I2C i2c0

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

#define TP_NATIVE_W 240 /**< Panel native width, independent of LCD_ROTATION. */
#define TP_NATIVE_H 320 /**< Panel native height, independent of LCD_ROTATION. */

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
	int n = i2c_write_timeout_us(TOUCH_I2C, CST816_I2C_ADDR, b, 2, false,
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
	if (i2c_write_timeout_us(TOUCH_I2C, CST816_I2C_ADDR, &reg, 1, true,
	                         I2C_TIMEOUT_US) != 1) return false;
	return i2c_read_timeout_us(TOUCH_I2C, CST816_I2C_ADDR, buf, n, false,
	                           I2C_TIMEOUT_US) == n;
}

bool touchInit() {
	gpio_init(PIN_TP_INT);
	gpio_set_dir(PIN_TP_INT, GPIO_IN);
	gpio_pull_up(PIN_TP_INT);

	i2c_init(TOUCH_I2C, 400 * 1000);
	gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
	gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
	gpio_pull_up(PIN_I2C_SDA);
	gpio_pull_up(PIN_I2C_SCL);

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

/**
 * @brief Map native portrait coordinates into the active framebuffer frame.
 * @param      nx,ny Native panel coordinates.
 * @param[out] ox,oy Framebuffer coordinates for the current @ref LCD_ROTATION.
 */
static void mapCoords(int16_t nx, int16_t ny, int16_t *ox, int16_t *oy) {
#if LCD_ROTATION == 1
	*ox = ny;
	*oy = (int16_t)(TP_NATIVE_W - 1 - nx);
#elif LCD_ROTATION == 2
	*ox = (int16_t)(TP_NATIVE_W - 1 - nx);
	*oy = (int16_t)(TP_NATIVE_H - 1 - ny);
#elif LCD_ROTATION == 3
	*ox = (int16_t)(TP_NATIVE_H - 1 - ny);
	*oy = nx;
#else
	*ox = nx;
	*oy = ny;
#endif
}

void touchPoll(TouchState *t) {
	uint8_t b[6];
	bool down = false;
	int16_t x = t->x, y = t->y;
	uint8_t gesture = 0;

	if (rdRegs(REG_GESTURE, b, 6)) {
		gesture = b[0];
		uint8_t fingers = b[1] & 0x0F;
		if (fingers > 0) {
			int16_t nx = (int16_t)(((b[2] & 0x0F) << 8) | b[3]);
			int16_t ny = (int16_t)(((b[4] & 0x0F) << 8) | b[5]);
			if (nx < TP_NATIVE_W && ny < TP_NATIVE_H) {
				mapCoords(nx, ny, &x, &y);
				down = true;
			}
		}
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
