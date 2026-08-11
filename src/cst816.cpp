/**
 * @file cst816.cpp
 * @brief CST816D register access and edge detection (RP2350-Touch-LCD-2).
 *
 * Compiles to nothing unless the selected board fits a CST816D. The Arduino
 * builder compiles every `.cpp` under `src/` regardless of board, so the guard
 * is the whole file rather than a build-system exclusion.
 *
 * @see touch.h for the interface both touch drivers implement.
 */

#include "board_pins.h"

#ifdef BOARD_TOUCH_CST816D

#include "touch.h"
#include "config.h"
#include "gfx.h"

#include <Arduino.h>
#include <Wire.h>

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
	BOARD_I2C.beginTransmission(CST816_I2C_ADDR);
	BOARD_I2C.write(reg);
	BOARD_I2C.write(val);
	return BOARD_I2C.endTransmission() == 0;
}

/**
 * @brief Read @p n consecutive registers starting at @p reg.
 * @return true if the full read completed.
 */
static bool rdRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
	BOARD_I2C.beginTransmission(CST816_I2C_ADDR);
	BOARD_I2C.write(reg);
	if (BOARD_I2C.endTransmission(false) != 0) return false;
	if (BOARD_I2C.requestFrom((uint8_t)CST816_I2C_ADDR, n) != n) return false;
	for (uint8_t i = 0; i < n; i++) buf[i] = BOARD_I2C.read();
	return true;
}

bool touchInit() {
	pinMode(PIN_TP_INT, INPUT_PULLUP);

	BOARD_I2C.setSDA(PIN_I2C_SDA);
	BOARD_I2C.setSCL(PIN_I2C_SCL);
	BOARD_I2C.begin();
	BOARD_I2C.setClock(400000);

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
			if (nx < TOUCH_NATIVE_W && ny < TOUCH_NATIVE_H) {
				touchMapCoords(nx, ny, &x, &y);
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

#endif /* BOARD_TOUCH_CST816D */
