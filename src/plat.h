/**
 * @file plat.h
 * @brief The handful of Arduino conveniences this firmware still wants.
 *
 * The rest of the codebase talks to the Pico SDK directly — `gpio_put()`,
 * `spi_write_blocking()`, `pio_sm_*`, `critical_section_*`. What it also used
 * was `millis()`, `micros()` and `delay()`, in about sixty places, and those
 * are the only reason `Arduino.h` was ever included in most files.
 *
 * The names are kept deliberately. Renaming them to `platMillis()` would have
 * touched sixty lines across nine files for no behavioural gain, and every one
 * of those lines is a chance to introduce a bug in code that currently works.
 * These are ordinary SDK calls wearing familiar names, not a compatibility
 * layer with anything behind it.
 *
 * @note `millis()` wraps every 49.7 days and `micros()` every 71.6 minutes.
 *       Both are used only in `now - then` comparisons, which stay correct
 *       across a wrap provided the arithmetic is unsigned — the same property
 *       the Arduino versions had, and the same trap if a difference is ever
 *       assigned to a signed type before comparing.
 */

#pragma once

#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/watchdog.h"

/**
 * @brief Reboot the chip, now.
 *
 * A settings save that changes the stored *board* is applied by rebooting:
 * display, touch and the DShot pump are all built from the board descriptor
 * during setup(), and re-pointing them live has no non-hazardous
 * implementation — the old board's hardware is still the hardware.
 * The host suite fakes this and counts the calls.
 */
static inline void platReboot() {
	watchdog_reboot(0, 0, 0);
}

/**
 * @brief Milliseconds since boot.
 * @return Count, wrapping every 49.7 days.
 */
static inline uint32_t millis() {
	return (uint32_t)(time_us_64() / 1000u);
}

/**
 * @brief Microseconds since boot.
 *
 * Uses the 32-bit timer read rather than truncating the 64-bit one: it is a
 * single register read, and this is called from the DShot frame pump.
 *
 * @return Count, wrapping every 71.6 minutes.
 */
static inline uint32_t micros() {
	return time_us_32();
}

/**
 * @brief Block for a number of milliseconds.
 * @param ms Duration.
 */
static inline void delay(uint32_t ms) {
	sleep_ms(ms);
}

/**
 * @brief Block for a number of microseconds.
 * @param us Duration.
 */
static inline void delayMicroseconds(uint32_t us) {
	sleep_us(us);
}
