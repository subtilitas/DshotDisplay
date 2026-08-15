/**
 * @file board_pins.h
 * @brief Pin map for the selected board.
 *
 * This file picks a per-board header based on @ref BOARD and does nothing else.
 * The pin numbers themselves live in:
 *
 * - @ref board_rp2350_touch_lcd_2.h — Waveshare RP2350-Touch-LCD-2 (2.0")
 * - @ref board_rp2350_touch_lcd_2_8.h — Waveshare RP2350-Touch-LCD-2.8
 *
 * @section board_contract What a board header must define
 *
 * Every board header supplies the whole of this list. There is no fallback: a
 * missing macro is a compile error at the point of use, which is the intended
 * behaviour — silently defaulting a pin number is how you drive an ESC signal
 * out of a backlight transistor.
 *
 * | Macro | Meaning |
 * |---|---|
 * | `BOARD_LABEL` | Short board name, for the splash screen |
 * | `PIN_LCD_BL`, `PIN_LCD_DC`, `PIN_LCD_CS` | Panel control lines |
 * | `PIN_LCD_SCK`, `PIN_LCD_MOSI`, `PIN_LCD_RST` | Panel data and reset |
 * | `LCD_SPI_PORT` | Pico SDK SPI instance the panel hangs off |
 * | `BOARD_I2C` | Arduino `TwoWire` instance for the touch bus |
 * | `PIN_I2C_SDA`, `PIN_I2C_SCL` | Touch bus pins |
 * | `PIN_TP_INT`, `PIN_TP_RST` | Touch interrupt and reset |
 * | `TOUCH_RST_SHARED_WITH_LCD` | 1 if st7789Init() already pulsed TP_RST |
 * | `BOARD_TOUCH_CST816D` or `BOARD_TOUCH_CST328` | Which touch driver compiles |
 * | `PIN_BAT_ADC`, `BAT_ADC_CHAN`, `BAT_DIVIDER` | Battery sense |
 *
 * Anything else a board header defines — IMU, RTC, SD, audio — is documentation
 * of pins this firmware must not reuse, not an interface.
 */

#pragma once

#include "board.h"

#if BOARD == BOARD_RP2350_TOUCH_LCD_2
#include "board_rp2350_touch_lcd_2.h"
#elif BOARD == BOARD_RP2350_TOUCH_LCD_2_8
#include "board_rp2350_touch_lcd_2_8.h"
#elif BOARD == BOARD_UNIFIED
// Nothing. A unified build has no single pin map to include -- that is the
// point of it -- and everything that used to read these macros now reads
// @ref g_board instead. The header is kept, and the two board headers with it,
// because the single-board builds still use them and because they are where the
// schematic-derived pin tables and their reasoning live.
#else
#error "BOARD is not a known board id -- see board.h"
#endif

#if BOARD != BOARD_UNIFIED
#if defined(BOARD_TOUCH_CST816D) == defined(BOARD_TOUCH_CST328)
#error "a board header must select exactly one touch driver"
#endif
#endif
