/**
 * @file theme.cpp
 * @brief The two palettes and the stroke-weight rules that go with them.
 */

#include "theme.h"

/**
 * @brief The indoor palette. These are the values the UI was designed against.
 *
 * Kept byte-for-byte identical to what the screenshots in docs/ were rendered
 * with, so switching to a table changes nothing visible in the default theme.
 */
static const uint16_t THEME_DARK[TC_COUNT] = {
	0x0000, /* TC_BG       */
	0x18E3, /* TC_PANEL    */
	0x39E7, /* TC_GRID     */
	0x8410, /* TC_DIM      */
	0xE71C, /* TC_TEXT     */
	0xFFFF, /* TC_INK      */
	0x0000, /* TC_PAPER    */
	0xFFFF, /* TC_ONACCENT */
	0x2665, /* TC_GREEN    */
	0x07E0, /* TC_LIME     */
	0xFD20, /* TC_AMBER    */
	0xF9E7, /* TC_RED      */
	0x6000, /* TC_REDDARK  */
	0x1082, /* TC_GHOST    */
	0x2D7F, /* TC_BLUE     */
	0x07FF, /* TC_CYAN     */
	0xF81F, /* TC_MAGENTA  */
};

/**
 * @brief The sunlight palette: black on white, with every accent darkened.
 *
 * Two rules produced these values, and both matter more than taste:
 *
 * - **Every accent must take white text.** `C_ONACCENT` is white in both
 *   themes, so red, green and blue here are dark enough to carry it. A bright
 *   lime that reads well on black is invisible on white *and* will not take
 *   white text, so it becomes a dark green rather than being left alone.
 * - **The greys collapse.** `C_DIM` and `C_GRID` are nearly the same dark grey.
 *   Three distinguishable greys is an indoor luxury; in sunlight a mid-grey
 *   label is simply gone, so the label tier gives up its distinctness and keeps
 *   its legibility.
 *
 * `TC_REDDARK` inverts direction rather than value. Its job is "lit, but this
 * reading is dead" — a *dark* red on black, so a *pale* red on white.
 */
static const uint16_t THEME_HIGH_CONTRAST[TC_COUNT] = {
	rgb(255, 255, 255), /* TC_BG       paper white                */
	rgb(206, 212, 220), /* TC_PANEL    tile fill, clearly not BG   */
	rgb( 56,  62,  72), /* TC_GRID     outlines                    */
	rgb( 68,  74,  84), /* TC_DIM      labels, still black-ish     */
	rgb(  0,   0,   0), /* TC_TEXT                                 */
	rgb(  0,   0,   0), /* TC_INK                                  */
	rgb(255, 255, 255), /* TC_PAPER    == TC_BG                    */
	rgb(255, 255, 255), /* TC_ONACCENT                             */
	rgb(  0, 110,  52), /* TC_GREEN    SAFE badge, takes white     */
	rgb(  0, 122,  40), /* TC_LIME     live values, throttle fill  */
	rgb(174,  88,   0), /* TC_AMBER    warning, takes white        */
	rgb(190,   0,  24), /* TC_RED      ARMED, takes white          */
	rgb(244, 190, 190), /* TC_REDDARK  dead digits: pale, not dark */
	rgb(238, 241, 245), /* TC_GHOST    unlit ghost 8s: barely there  */
	rgb(  0,  58, 168), /* TC_BLUE     HOLD active, takes white    */
	rgb(  0,  88, 122), /* TC_CYAN     informational               */
	rgb(150,   0, 140), /* TC_MAGENTA  alert                       */
};

static_assert(sizeof(THEME_DARK) / sizeof(THEME_DARK[0]) == TC_COUNT,
              "dark palette must have one entry per ThemeColor");
static_assert(sizeof(THEME_HIGH_CONTRAST) / sizeof(THEME_HIGH_CONTRAST[0]) == TC_COUNT,
              "high-contrast palette must have one entry per ThemeColor");

const uint16_t *g_theme = THEME_DARK;

static Theme s_theme = Theme::Dark;

void themeSet(Theme t) {
	s_theme = t;
	g_theme = (t == Theme::HighContrast) ? THEME_HIGH_CONTRAST : THEME_DARK;
}

Theme themeGet() { return s_theme; }

bool themeBold() { return s_theme == Theme::HighContrast; }

int themeStroke() { return s_theme == Theme::HighContrast ? 2 : 1; }

int themeSegStroke(int base) {
	// +2 rather than a multiplier: the digit cell is a fixed 36x62 and the
	// segment geometry stops meeting cleanly once the stroke approaches a
	// quarter of the height.
	return s_theme == Theme::HighContrast ? base + 2 : base;
}

uint8_t themeBacklight(uint8_t preferred) {
	return s_theme == Theme::HighContrast ? 255 : preferred;
}
