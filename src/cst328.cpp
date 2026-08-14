/**
 * @file cst328.cpp
 * @brief CST328 register access and edge detection (RP2350-Touch-LCD-2.8).
 *
 * Compiles to nothing unless the selected board fits a CST328. The Arduino
 * builder compiles every `.cpp` under `src/` regardless of board, so the guard
 * is the whole file rather than a build-system exclusion.
 *
 * The CST328 is not a bigger CST816. Registers are **16 bits wide**, the
 * address is 0x1A rather than 0x15, mode changes are address-only writes with
 * no data byte, and coordinates are packed 12-bit rather than split across
 * nibble-masked bytes. Nothing about the CST816D sequence transfers.
 *
 * The chip reports five contacts. Only the first is read: the UI is
 * single-touch throughout, so the other four would be I2C traffic and dead
 * code. Reading one finger is a 5-byte transfer instead of 27.
 *
 * @see touch.h for the interface both touch drivers implement.
 */

#include "touch.h"
#include "board_desc.h"
#include "config.h"
#include "gfx.h"

#include "plat.h"
#include <hardware/i2c.h>
#include <hardware/gpio.h>

/** @brief Per-transfer I2C timeout. Long enough for a 400 kHz byte, short
 *         enough that a wedged bus does not stall the UI. */
#define I2C_TIMEOUT_US 2000

/** @brief I2C instance the touch controller sits on. @see board_desc.h */
#define TOUCH_I2C ((i2c_inst_t *)g_board->i2c)
/** @brief 7-bit address, from the descriptor. */
#define TOUCH_ADDR (g_board->tpAddr)

/**
 * @defgroup cst328_regs CST328 register map
 * @brief Only the subset this driver needs. All addresses are 16-bit.
 * @{
 */

/**
 * @brief Enter debug-info mode.
 *
 * The identifying registers read back as zero in normal reporting mode, so the
 * probe below has to switch modes first and switch back afterwards.
 */
#define REG_MODE_DEBUG_INFO 0xD101

/** @brief Leave debug mode and start reporting touches. */
#define REG_MODE_NORMAL     0xD109

/**
 * @brief Firmware info word. Top half reads back a fixed 0xCACA.
 *
 * That constant is the only reliable presence check the part offers — there is
 * no chip-ID register in the CST816 sense.
 */
#define REG_INFO_FW         0xD1FC

/**
 * @brief First contact: id and state byte, then XH, YH, XL/YL, pressure.
 *
 * Byte 0 low nibble is 6 while the finger is down. Bytes 1 and 2 carry the top
 * eight bits of X and Y; byte 3 packs the low four bits of each, X in the high
 * nibble. Byte 4 is contact pressure, unused here.
 */
#define REG_FINGER_1        0xD000

/** @} */

/** @brief Low nibble of the state byte while a finger is present. */
#define FINGER_STATE_DOWN 0x06

static bool s_prevDown = false;        /**< Contact state at the previous poll. */
static int16_t s_downX = 0, s_downY = 0; /**< Where the current press started. */

/**
 * @brief Write a bare 16-bit register address, with no data byte.
 *
 * How the CST328 takes mode changes: the address *is* the command.
 *
 * @param reg Register address.
 * @return true if the device acknowledged.
 */
static bool wrCmd(uint16_t reg) {
	// 16-bit register address, big-endian, and the address alone is the command.
	uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
	return i2c_write_timeout_us(TOUCH_I2C, TOUCH_ADDR, a, 2, false,
	                            I2C_TIMEOUT_US) == 2;
}

/**
 * @brief Read @p n consecutive bytes starting at 16-bit register @p reg.
 *
 * @param reg Register address.
 * @param buf Destination, at least @p n bytes.
 * @param n   Byte count.
 * @return true if the full read completed.
 */
static bool rdRegs(uint16_t reg, uint8_t *buf, uint8_t n) {
	uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
	// nostop=true leaves a repeated start; a stop here would make the
	// controller forget the register pointer.
	if (i2c_write_timeout_us(TOUCH_I2C, TOUCH_ADDR, a, 2, true,
	                         I2C_TIMEOUT_US) != 2) return false;
	return i2c_read_timeout_us(TOUCH_I2C, TOUCH_ADDR, buf, n, false,
	                           I2C_TIMEOUT_US) == n;
}

/** @brief Bring the CST328 up. @return true if it answered its firmware word. */
static bool cst328Init() {
	gpio_init(g_board->tpInt);
	gpio_set_dir(g_board->tpInt, GPIO_IN);
	gpio_pull_up(g_board->tpInt);

	i2c_init(TOUCH_I2C, 400 * 1000);
	gpio_set_function(g_board->sda, GPIO_FUNC_I2C);
	gpio_set_function(g_board->scl, GPIO_FUNC_I2C);
	gpio_pull_up(g_board->sda);
	gpio_pull_up(g_board->scl);

	// A runtime test now, not #if: whether TP_RST is its own net is a property
	// of the board, and a unified image does not know which one it is on until
	// it is told.
	if (!g_board->tpRstSharedWithLcd) {
		// Nothing has pulsed it yet. The datasheet wants 200 ms before the part
		// answers; 120 ms is what every working driver actually uses, and the
		// probe below retries anyway.
		gpio_init(g_board->tpRst);
		gpio_set_dir(g_board->tpRst, GPIO_OUT);
		gpio_put(g_board->tpRst, 1);
		delay(10);
		gpio_put(g_board->tpRst, 0);
		delay(10);
		gpio_put(g_board->tpRst, 1);
	}
	delay(120);

	// Presence check: in debug mode the firmware word reads back with 0xCACA in
	// its top half. Retried because the first transaction after reset lands
	// while the part is still counting out its own boot timer.
	bool ok = false;
	for (int attempt = 0; attempt < 3 && !ok; attempt++) {
		uint8_t b[4];
		if (wrCmd(REG_MODE_DEBUG_INFO) && rdRegs(REG_INFO_FW, b, 4)) {
			ok = (b[2] == 0xCA && b[3] == 0xCA);
		}
		if (!ok) delay(20);
	}

	// Report touches regardless of how the probe went — a controller that
	// answers coordinates but not its own ID is still a usable touchscreen, and
	// refusing to poll would turn a cosmetic problem into a dead panel.
	wrCmd(REG_MODE_NORMAL);

	return ok;
}

/** @brief Sample the CST328. @param[in,out] t State to update. */
static void cst328Poll(TouchState *t) {
	uint8_t b[5];
	bool down = false;
	int16_t x = t->x, y = t->y;

	if (rdRegs(REG_FINGER_1, b, 5)) {
		if ((b[0] & 0x0F) == FINGER_STATE_DOWN) {
			int16_t nx = (int16_t)(((uint16_t)b[1] << 4) | (b[3] >> 4));
			int16_t ny = (int16_t)(((uint16_t)b[2] << 4) | (b[3] & 0x0F));
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
	t->gesture = 0;   // the CST328 has no gesture engine; the UI ignores this

	if (t->pressed) { s_downX = x; s_downY = y; }
	t->downX = s_downX;
	t->downY = s_downY;

	s_prevDown = down;
}

/** @brief The CST328 driver, as named by the 2.8" board's descriptor. */
extern const TouchDriver TOUCH_DRIVER_CST328;
extern const TouchDriver TOUCH_DRIVER_CST328 = { "CST328", cst328Init, cst328Poll };
