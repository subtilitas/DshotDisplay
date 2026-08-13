/**
 * @file st7789.h
 * @brief ST7789T3 panel driver, 240x320, for both supported boards.
 *
 * Talks to the panel over the hardware SPI instance the board header names in
 * `LCD_SPI_PORT` (SPI0 on the 2.0" board, SPI1 on the 2.8"), with a
 * dedicated DMA channel for pixel pushes. Commands go out in 8-bit SPI frames;
 * pixels go out in 16-bit frames so RGB565 words land on the wire MSB-first
 * with no software byte swapping.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Bring up `LCD_SPI_PORT`, the DMA channel, and the panel itself.
 *
 * @warning This pulses `PIN_LCD_RST`. On the 2.0" board that is the same net
 *          as the touch controller's reset line, so touchInit() must be called
 *          after this, not before. The 2.8" board separates them, but the
 *          ordering rule is the same on both so callers need not branch.
 */
void st7789Init();

/**
 * @brief Set backlight brightness.
 * @param level 0 (off) to 255 (full), driven as 20 kHz PWM.
 */
void st7789SetBacklight(uint8_t level);

/**
 * @brief DMA a rectangle of pixels to the panel.
 *
 * Blocks until the transfer completes. Pixels are consumed row-major, so the
 * caller must supply `(x1 - x0 + 1) * (y1 - y0 + 1)` contiguous words — which
 * for a framebuffer slice means @p x0 and @p x1 must span the full width.
 *
 * @param x0,y0  Top-left corner, inclusive.
 * @param x1,y1  Bottom-right corner, inclusive.
 * @param pixels Source RGB565 data.
 */
void st7789Blit(int x0, int y0, int x1, int y1, const uint16_t *pixels);

/**
 * @brief Enter or leave panel sleep mode.
 * @param on true to sleep, false to wake.
 */
void st7789Sleep(bool on);

/**
 * @brief Push every dirty band recorded by gfx, then clear them.
 * @see gfxMarkDirty()
 */
void st7789FlushDirty();
