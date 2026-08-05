/**
 * @file gfx.h
 * @brief Minimal RGB565 framebuffer renderer with dirty-band tracking.
 *
 * The whole 240x320 frame lives in SRAM (150 KB of the RP2350's 520 KB). Draw
 * calls record the vertical span they touched; on flush, only those bands are
 * DMA'd to the panel. Bands are full-width, which keeps each transfer a single
 * contiguous DMA with no per-row restart cost.
 *
 * @see st7789FlushDirty()
 */

#pragma once

#include <stdint.h>
#include "config.h"

#if (LCD_ROTATION == 1) || (LCD_ROTATION == 3)
#define GFX_W 320 /**< Framebuffer width in pixels, after @ref LCD_ROTATION. */
#define GFX_H 240 /**< Framebuffer height in pixels, after @ref LCD_ROTATION. */
#else
#define GFX_W 240 /**< Framebuffer width in pixels, after @ref LCD_ROTATION. */
#define GFX_H 320 /**< Framebuffer height in pixels, after @ref LCD_ROTATION. */
#endif

/**
 * @brief Pack 8-bit RGB into an RGB565 word.
 * @param r Red, 0..255 (top 5 bits kept).
 * @param g Green, 0..255 (top 6 bits kept).
 * @param b Blue, 0..255 (top 5 bits kept).
 * @return Packed RGB565 colour.
 */
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/**
 * @defgroup gfx_palette Palette
 * @{
 */
#define C_BLACK   0x0000 /**< Pure black. */
#define C_WHITE   0xFFFF /**< Pure white. */
#define C_BG      0x0000 /**< Screen background. */
#define C_PANEL   0x18E3 /**< Dark grey card / tile fill. */
#define C_GRID    0x39E7 /**< Separator and outline grey. */
#define C_DIM     0x8410 /**< Label grey. */
#define C_TEXT    0xE71C /**< Primary text, near-white. */
#define C_GREEN   0x2665 /**< Muted green (SAFE badge). */
#define C_LIME    0x07E0 /**< Bright green (live values, throttle fill). */
#define C_AMBER   0xFD20 /**< Warning amber. */
#define C_RED     0xF9E7 /**< Error / ARMED red. */
#define C_REDDARK 0x6000 /**< Dead-telemetry digit colour. */
#define C_BLUE    0x2D7F /**< HOLD button active. */
#define C_CYAN    0x07FF /**< Informational accent. */
#define C_MAGENTA 0xF81F /**< ESC alert state. */
/** @} */

/** @brief Clear the framebuffer and mark the whole screen dirty. */
void gfxInit();

/**
 * @brief Direct access to the framebuffer.
 * @return Pointer to @ref GFX_W * @ref GFX_H RGB565 pixels, row-major.
 */
uint16_t *gfxBuffer();

/**
 * @defgroup gfx_dirty Dirty-band bookkeeping
 * @brief Tracks which horizontal bands changed so only those get transferred.
 * @{
 */

/**
 * @brief Mark rows @p y0 through @p y1 inclusive as needing a flush.
 *
 * Overlapping and adjacent bands are merged. If more than eight disjoint bands
 * accumulate they collapse into a single spanning band, trading transfer size
 * for bounded bookkeeping.
 *
 * @param y0 First row. Clipped to the framebuffer; may be greater than @p y1.
 * @param y1 Last row, inclusive.
 */
void gfxMarkDirty(int y0, int y1);

/** @brief Mark the entire screen dirty. */
void gfxMarkAllDirty();

/** @brief Number of pending dirty bands. @return Count, 0..8. */
int  gfxDirtyCount();

/**
 * @brief Read back band @p i.
 * @param      i  Band index, 0..gfxDirtyCount()-1.
 * @param[out] y0 First row of the band.
 * @param[out] y1 Last row of the band, inclusive.
 */
void gfxDirtyBand(int i, int *y0, int *y1);

/** @brief Discard all pending bands. Called after a successful flush. */
void gfxClearDirty();

/** @} */

/**
 * @defgroup gfx_prims Primitives
 * @brief All coordinates are clipped; off-screen draws are silently dropped.
 * @{
 */

/** @brief Fill the whole framebuffer with @p c. */
void gfxFill(uint16_t c);

/** @brief Filled rectangle at (@p x, @p y) of @p w by @p h in colour @p c. */
void gfxRect(int x, int y, int w, int h, uint16_t c);

/** @brief One-pixel outline of the rectangle at (@p x, @p y), @p w by @p h. */
void gfxFrame(int x, int y, int w, int h, uint16_t c);

/**
 * @brief Filled rectangle with rounded corners.
 * @param r Corner radius, clamped to half the smaller dimension.
 */
void gfxRoundRect(int x, int y, int w, int h, int r, uint16_t c);

/** @brief One-pixel outline version of gfxRoundRect(). */
void gfxRoundFrame(int x, int y, int w, int h, int r, uint16_t c);

/** @brief Horizontal line of length @p w. */
void gfxHLine(int x, int y, int w, uint16_t c);

/** @brief Vertical line of height @p h. */
void gfxVLine(int x, int y, int h, uint16_t c);

/** @} */

/**
 * @defgroup gfx_text Text
 * @brief 5x7 bitmap font covering ASCII 0x20..0x5F.
 *
 * Lowercase is folded to uppercase. Character 0x60 (backtick) renders as a
 * degree sign, so `"46`C"` prints as `46°C`. Unknown characters render as
 * spaces. One character cell is `6 * scale` pixels wide and `7 * scale` tall.
 * @{
 */

/**
 * @brief Draw a string with its top-left corner at (@p x, @p y).
 * @param scale Integer pixel multiplier, 1 or greater.
 */
void gfxText(int x, int y, const char *s, uint16_t fg, int scale);

/**
 * @brief Rendered width of @p s in pixels.
 * @return `(len * 6 - 1) * scale`, or 0 for an empty string.
 */
int  gfxTextW(const char *s, int scale);

/**
 * @brief Draw @p s horizontally centred on the display.
 *
 * Prefer this over eyeballing an x offset — at scale 3 a 13-character string is
 * already 231 px, which leaves only 9 px of margin on a 240 px panel.
 */
void gfxTextCenter(int y, const char *s, uint16_t fg, int scale);

/** @} */

/**
 * @defgroup gfx_seg Seven-segment digits
 * @brief Drawn from rectangles rather than glyph data, so they scale cleanly.
 * @{
 */

/**
 * @brief Draw one seven-segment digit.
 * @param x,y   Top-left corner.
 * @param w,h   Size of the digit cell.
 * @param t     Stroke thickness.
 * @param digit Value 0..9; anything else renders all segments off.
 * @param on    Colour for lit segments.
 * @param off   Colour for unlit segments.
 */
void gfxSegDigit(int x, int y, int w, int h, int t, int digit, uint16_t on, uint16_t off);

/**
 * @brief Draw a right-aligned seven-segment number.
 *
 * Unlit segments of a live digit are drawn in the background colour so the
 * number reads cleanly. Unused leading digits are drawn as dim "8" ghosts in
 * @p ghost, giving the look of an unlit LCD panel.
 *
 * @param xRight Right edge of the least-significant digit.
 * @param gap    Horizontal spacing between digit cells.
 * @param value  Value to render; not clamped, so clamp before calling.
 * @param digits Number of digit positions to draw.
 * @param on     Colour for lit segments.
 * @param ghost  Colour for the unused leading digits.
 */
void gfxSegNumber(int xRight, int y, int w, int h, int t, int gap, uint32_t value,
                  int digits, uint16_t on, uint16_t ghost);

/** @} */
