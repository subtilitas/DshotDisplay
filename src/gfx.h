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
#include "theme.h"

#if (LCD_ROTATION == 1) || (LCD_ROTATION == 3)
#define GFX_W 320 /**< Framebuffer width in pixels, after @ref LCD_ROTATION. */
#define GFX_H 240 /**< Framebuffer height in pixels, after @ref LCD_ROTATION. */
#else
#define GFX_W 240 /**< Framebuffer width in pixels, after @ref LCD_ROTATION. */
#define GFX_H 320 /**< Framebuffer height in pixels, after @ref LCD_ROTATION. */
#endif

/**
 * @defgroup gfx_palette Palette
 * @brief Names for the active theme's colours. @see theme.h
 *
 * Each expands to a subscript of @ref g_theme, so a call site written against
 * these follows a runtime theme change with no edit. `rgb()` lives in theme.h.
 *
 * `C_INK`, `C_PAPER` and `C_ONACCENT` are the ones to reach for when
 * adding UI: they name the *role* a colour plays in a fill/foreground pair,
 * which is the part a palette inversion would otherwise break. See the table in
 * theme.h.
 * @{
 */
#define C_BG       (g_theme[TC_BG])       /**< Screen background. */
#define C_PANEL    (g_theme[TC_PANEL])    /**< Card / tile fill. */
#define C_GRID     (g_theme[TC_GRID])     /**< Separator and outline. */
#define C_DIM      (g_theme[TC_DIM])      /**< Label text. */
#define C_TEXT     (g_theme[TC_TEXT])     /**< Primary text. */
#define C_INK      (g_theme[TC_INK])      /**< Strongest mark against the background. */
#define C_PAPER    (g_theme[TC_PAPER])    /**< Text drawn on a bright fill. */
#define C_ONACCENT (g_theme[TC_ONACCENT]) /**< Text drawn on a saturated fill. */
#define C_GREEN    (g_theme[TC_GREEN])    /**< Muted green (SAFE badge). */
#define C_LIME     (g_theme[TC_LIME])     /**< Live values, throttle fill. */
#define C_AMBER    (g_theme[TC_AMBER])    /**< Warning. */
#define C_RED      (g_theme[TC_RED])      /**< Error / ARMED. */
#define C_REDDARK  (g_theme[TC_REDDARK])  /**< Dead-telemetry digit. */
#define C_GHOST    (g_theme[TC_GHOST])    /**< Unlit leading seven-segment digits. */
#define C_BLUE     (g_theme[TC_BLUE])     /**< HOLD button active. */
#define C_CYAN     (g_theme[TC_CYAN])     /**< Informational accent. */
#define C_MAGENTA  (g_theme[TC_MAGENTA])  /**< ESC alert state. */
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
 * @defgroup gfx_clip Clip rectangle
 * @brief A second bound, inside the panel, that every primitive respects.
 *
 * The panel edge has always been clipped against. This adds a caller-set box on
 * top of it, and it exists for one reason: a list that scrolls by the pixel has
 * partial rows at both ends, and a partial row drawn in full paints over
 * whatever is above and below the viewport — on the AM32 screen, the header and
 * the editor bar.
 *
 * The alternative is for every caller to work out how much of each row is
 * visible and draw only that, per primitive, per row, and get it right for text
 * as well as fills. That arithmetic belongs here once rather than at each call
 * site, where it would be wrong in a different way each time.
 *
 * Set it, draw, clear it. It is deliberately not saved and restored: nesting it
 * would invite leaving it set, and a stray clip is a screen that silently stops
 * painting parts of itself.
 * @{
 */

/**
 * @brief Restrict drawing to the given rectangle until gfxClearClip().
 * @param x,y Top-left corner. Clamped to the panel.
 * @param w,h Size. A degenerate box clips everything away.
 */
void gfxSetClip(int x, int y, int w, int h);

/** @brief Drop the clip box, restoring the whole panel. */
void gfxClearClip();

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
 * @param x,y Top-left corner.
 * @param w,h Size.
 * @param r   Corner radius, clamped to half the smaller dimension.
 * @param c   Fill colour.
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
 * degree sign, so a string containing 46, a backtick, then C prints as 46°C.
 * Unknown characters render as spaces. One character cell is `6 * scale`
 * pixels wide and `7 * scale` tall.
 * @{
 */

/**
 * @brief Draw a string with its top-left corner at (@p x, @p y).
 * @param x,y   Top-left corner.
 * @param s     NUL-terminated string.
 * @param fg    Text colour.
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
 * @param y      Top edge.
 * @param w,h    Size of one digit cell.
 * @param t      Stroke thickness.
 * @param gap    Horizontal spacing between digit cells.
 * @param value  Value to render; not clamped, so clamp before calling.
 * @param digits Number of digit positions to draw.
 * @param on     Colour for lit segments.
 * @param ghost  Colour for the unused leading digits.
 */
void gfxSegNumber(int xRight, int y, int w, int h, int t, int gap, uint32_t value,
                  int digits, uint16_t on, uint16_t ghost);

/** @} */
