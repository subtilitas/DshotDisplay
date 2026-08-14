/**
 * @file board.h
 * @brief Which piece of hardware this firmware is built for.
 *
 * DshotDisplay targets two Waveshare RP2350 boards. They are not variants of
 * one design — the panel hangs off a different SPI instance, the touch
 * controller is a different chip with a different register width, and the I2C
 * bus is a different peripheral. None of that can be probed safely at runtime
 * (probing an I2C bus means first knowing which pins it is on), so the choice
 * is a compile-time constant.
 *
 * Two ways to set it:
 *
 * - Edit the `#define BOARD` line below. This is the only option the Arduino
 *   IDE gives you, since it has no way to pass `-D` flags, and it is what
 *   `test/select_board.sh` rewrites for CI.
 * - Define `BOARD` on the compiler command line. `test/Makefile` and PlatformIO
 *   do this; the `#ifndef` below gets out of the way when they do.
 *
 * @ref board_pins.h turns the choice into an actual pin map.
 */

#pragma once

/**
 * @warning **RP2350-E9.** A pad configured as an input with the internal
 *          pull-down engaged can latch at roughly 2.1 V instead of being pulled
 *          to ground, and reads high from then on. Do not use internal
 *          pull-downs for level detection anywhere in this firmware — drive the
 *          pad and read it back instead. A diagnostic in tools/sdtest did use
 *          one, and duly reported six perfectly good SDIO lines as "held high".
 */

/**
 * @defgroup board_ids Board identifiers
 * @brief Legal values for @ref BOARD.
 * @{
 */

/**
 * @brief Waveshare RP2350-Touch-LCD-2 — 2.0" panel.
 *
 * ST7789T3 on SPI0, CST816D touch on I2C0 sharing the LCD reset line, camera
 * bus broken out on two 14-pin 2.54 mm headers.
 */
#define BOARD_RP2350_TOUCH_LCD_2    2

/**
 * @brief Waveshare RP2350-Touch-LCD-2.8 — 2.8" panel.
 *
 * ST7789T3 on SPI1, CST328 touch on I2C1 with its own reset line, plus an RTC,
 * an audio codec and an SD slot that between them consume nearly every GPIO.
 * Only GP28 and GP29 are free.
 */
#define BOARD_RP2350_TOUCH_LCD_2_8  29

/**
 * @brief One image for both boards, with the choice made on the board itself.
 *
 * Compiles both pin maps, both touch drivers and both SD back ends, and picks
 * between them at run time from a stored setting. Costs a few kilobytes of
 * flash against 4 MB, and removes the entire class of "wrong image on the wrong
 * board" -- which matters more than it sounds, because the 2.8" image asserts a
 * power latch the 2.0" does not and the symptom of getting it wrong is a screen
 * that never lights.
 *
 * The single-board values remain, and CI still builds them. They are smaller,
 * and they are what keeps the preprocessor path from rotting.
 *
 * @see board_desc.h
 */
#define BOARD_UNIFIED               0

/** @} */

/**
 * @brief The board this build targets. One of @ref board_ids.
 *
 * @note This default must match the one in `CMakeLists.txt`. The two disagreed
 *       for a while -- CMake said the 2.0", this file said the 2.8" -- which
 *       was invisible through CMake, since it always passes `-DBOARD`, and
 *       wrong for every other consumer including the generated documentation.
 */
#ifndef BOARD
#define BOARD BOARD_RP2350_TOUCH_LCD_2
#endif
