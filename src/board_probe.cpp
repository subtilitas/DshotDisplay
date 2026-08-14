/**
 * @file board_probe.cpp
 * @brief The RP2350 side of boardProbe(): two bus probes, one verdict.
 *
 * Device-only by design — the decision it feeds (probe result → board select →
 * defaults) is exercised on the host through settingsLoad() and the SETUP
 * screen; the four SDK calls here are not worth a stub apiece.
 *
 * The pin and address facts are duplicated from the board headers on purpose
 * rather than read from the descriptors: the descriptors describe the board
 * the image *has chosen*, and this file runs precisely when no choice exists.
 * Each constant cites its header so a schematic change has a trail to follow.
 */

#include "board_probe.h"
#include "board_desc.h"

#include <hardware/i2c.h>
#include <hardware/gpio.h>

/** @brief Probe bus speed. Conservative: every device here does 400 kHz. */
#define PROBE_BAUD 100000u

/** @brief Per-address ACK wait. A present device answers in microseconds. */
#define PROBE_TIMEOUT_US 2000u

/**
 * @brief True if any of @p addrs ACKs on an I2C bus brought up on @p sda/@p scl.
 *
 * The bus is initialised, probed, and torn back down to high-impedance inputs
 * before returning, so a probe of the wrong board leaves no trace: open-drain
 * pulses on pins that are either free headers or pulled-up idle lines.
 *
 * @param bus   i2c0 or i2c1 — must be the instance @p sda/@p scl mux to.
 * @param sda   SDA GPIO.
 * @param scl   SCL GPIO.
 * @param addrs 7-bit addresses to try.
 * @param n     How many.
 * @return true on the first ACK; false when every address stayed silent.
 */
static bool probeBus(i2c_inst_t *bus, uint8_t sda, uint8_t scl,
                     const uint8_t *addrs, int n) {
	i2c_init(bus, PROBE_BAUD);
	gpio_set_function(sda, GPIO_FUNC_I2C);
	gpio_set_function(scl, GPIO_FUNC_I2C);
	gpio_pull_up(sda);
	gpio_pull_up(scl);

	bool acked = false;
	for (int i = 0; i < n && !acked; i++) {
		uint8_t rx = 0;
		// A 1-byte read is the lightest transaction that still proves an ACK:
		// the return value is bytes transferred, negative on address NACK.
		acked = i2c_read_timeout_us(bus, addrs[i], &rx, 1, false,
		                            PROBE_TIMEOUT_US) == 1;
	}

	i2c_deinit(bus);
	gpio_set_function(sda, GPIO_FUNC_NULL);
	gpio_set_function(scl, GPIO_FUNC_NULL);
	gpio_disable_pulls(sda);
	gpio_disable_pulls(scl);
	return acked;
}

uint8_t boardProbe(void) {
	// RP2350-Touch-LCD-2.8: I2C1 on GP6/GP7 carries the QMI8658 IMU (0x6B),
	// the PCF85063 RTC (0x51) and the CST328 touch controller (0x1A) — see
	// board_rp2350_touch_lcd_2_8.h. On the 2.0" these pins are free camera
	// header lines (CAM_D6/D7), floating unless a camera module is fitted.
	static const uint8_t LCD_2_8_ADDRS[] = { 0x6B, 0x51, 0x1A };
	bool is28 = probeBus(i2c1, 6, 7, LCD_2_8_ADDRS,
	                     (int)sizeof(LCD_2_8_ADDRS));

	// RP2350-Touch-LCD-2: I2C0 on GP12/GP13 carries the QMI8658 IMU (0x6B)
	// and the CST816D touch controller (0x15) — see board_rp2350_touch_lcd_2.h.
	// The IMU is the witness that matters: a CST816D can autosleep into a
	// NACK. On the 2.8" these pins are LCD MISO and LCD CS, idle and pulled
	// up until the panel driver claims them.
	static const uint8_t LCD_2_ADDRS[] = { 0x6B, 0x15 };
	bool is2 = probeBus(i2c0, 12, 13, LCD_2_ADDRS, (int)sizeof(LCD_2_ADDRS));

	// Exactly one answer or no answer at all. "Both" means something is
	// back-driving a bus this image does not understand — a camera module,
	// a rework, hardware this table has never met — and a guess about
	// hardware is precisely what this function exists to avoid.
	if (is28 == is2) return BOARD_ID_UNSET;
	return is28 ? BOARD_ID_LCD_2_8 : BOARD_ID_LCD_2;
}
