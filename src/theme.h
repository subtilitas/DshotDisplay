/**
 * @file theme.h
 * @brief Runtime colour palette and stroke weight.
 *
 * The UI used to name colours as compile-time constants. It cannot any more:
 * a bench moves between a garage and direct sunlight, and the dark palette
 * that reads beautifully indoors is close to unreadable outdoors on a
 * transmissive IPS panel.
 *
 * So the `C_*` names still exist and every call site is unchanged, but they now
 * index a table that can be swapped at runtime. @ref themeSet() picks which.
 *
 * @section theme_semantics Naming the roles, not the colours
 *
 * Inverting a palette breaks any pairing that was written as a literal. Three
 * names exist purely to keep fill/foreground pairs correct in both themes, and
 * they are the ones to reach for when adding UI:
 *
 * | Name | Means | Dark | High contrast |
 * |---|---|---|---|
 * | `C_INK` | Strongest mark against `C_BG` | white | black |
 * | `C_PAPER` | Same as `C_BG`; text drawn *on* a bright fill | black | white |
 * | `C_ONACCENT` | Text drawn on a saturated fill (red, green, blue) | white | white |
 *
 * `C_PAPER` is what a button's label uses when the button's fill is `C_INK` or
 * `C_AMBER`, because those are the two fills that are bright in one theme and
 * dark in the other — and `C_PAPER` flips with them.
 *
 * @see gfx.h, which includes this and re-exports the names.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Pack 8-bit RGB into an RGB565 word.
 *
 * `constexpr` so the palettes below can be written as readable component
 * triples rather than as hex words nobody can check by eye.
 *
 * @param r Red, 0..255 (top 5 bits kept).
 * @param g Green, 0..255 (top 6 bits kept).
 * @param b Blue, 0..255 (top 5 bits kept).
 * @return Packed RGB565 colour.
 */
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/**
 * @brief Index into the active palette.
 *
 * Order is load-bearing: both palettes in theme.cpp are initialised
 * positionally, and a `static_assert` there checks each has one entry per
 * value below.
 */
enum ThemeColor : uint8_t {
	TC_BG,        /**< Screen background. */
	TC_PANEL,     /**< Card / tile fill. */
	TC_GRID,      /**< Separator and outline. */
	TC_DIM,       /**< Label text. */
	TC_TEXT,      /**< Primary text. */
	TC_INK,       /**< Strongest mark against the background. */
	TC_PAPER,     /**< Equals the background; for text on a bright fill. */
	TC_ONACCENT,  /**< Text on a saturated fill. */
	TC_GREEN,     /**< Muted green (SAFE badge). */
	TC_LIME,      /**< Live values, throttle fill. */
	TC_AMBER,     /**< Warning. */
	TC_RED,       /**< Error / ARMED. */
	TC_REDDARK,   /**< Dead-telemetry digit. Reads as "off but present". */
	TC_GHOST,     /**< Unused leading seven-segment digits. */
	TC_BLUE,      /**< HOLD active. */
	TC_CYAN,      /**< Informational accent. */
	TC_MAGENTA,   /**< ESC alert state. */
	TC_COUNT      /**< Number of entries. Not a colour. */
};

/** @brief Which palette is active. */
enum class Theme : uint8_t {
	Dark = 0,          /**< The indoor default. */
	HighContrast = 1,  /**< Black on white, heavier strokes. For sunlight. */
};

/**
 * @brief The active palette. Indexed by a @ref ThemeColor.
 *
 * Exposed rather than wrapped in a function because every `C_*` macro expands
 * to a subscript of it, and that happens hundreds of times per frame.
 */
extern const uint16_t *g_theme;

/**
 * @brief Select a palette.
 *
 * Does not repaint. Callers own the screen and must invalidate it themselves —
 * every region caches what it last drew, and a palette swap changes nothing
 * those caches can see.
 *
 * @param t Which theme.
 */
void themeSet(Theme t);

/** @brief The active theme. @return Current selection. */
Theme themeGet();

/**
 * @brief True when text should be drawn with a one-pixel vertical smear.
 *
 * Vertical rather than horizontal on purpose: the 5x7 font sits in a 6 px cell,
 * so a horizontal smear would close the one-pixel gap between glyphs and run
 * words together. A vertical smear thickens every stroke and changes no
 * advance width, so no layout moves.
 *
 * @return true in @ref Theme::HighContrast.
 */
bool themeBold();

/**
 * @brief Outline thickness for frames, in pixels.
 * @return 1 normally, 2 in @ref Theme::HighContrast.
 */
int themeStroke();

/**
 * @brief Seven-segment stroke thickness for a given nominal weight.
 *
 * Applied at the call site rather than inside gfxSegDigit() so a caller that
 * has laid out around a specific thickness can opt out.
 *
 * @param base Thickness the layout was designed for.
 * @return @p base, widened in @ref Theme::HighContrast.
 */
int themeSegStroke(int base);

/**
 * @brief Backlight level this theme wants, given the user's preferred level.
 *
 * High contrast forces full brightness: an inverted palette is most of the
 * outdoor legibility fix, but not all of it, and dimming a white screen throws
 * the rest away.
 *
 * @param preferred The level the user configured, 0..255.
 * @return The level to actually drive.
 */
uint8_t themeBacklight(uint8_t preferred);
