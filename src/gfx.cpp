/**
 * @file gfx.cpp
 * @brief Framebuffer, dirty-band bookkeeping, primitives and fonts.
 */

#include "gfx.h"
#include <string.h>

/** @brief The framebuffer. 150 KB of the RP2350's 520 KB of SRAM. */
static uint16_t s_fb[GFX_W * GFX_H];

/**
 * @brief Maximum disjoint dirty bands tracked before they get collapsed.
 *
 * Bounded on purpose: an unbounded list would let a pathological frame issue
 * hundreds of tiny DMA transfers, which costs more in setup overhead than one
 * larger transfer does in bandwidth.
 */
#define MAX_BANDS 8

static int s_bandY0[MAX_BANDS]; /**< First row of each pending band. */
static int s_bandY1[MAX_BANDS]; /**< Last row of each pending band, inclusive. */
static int s_bands = 0;         /**< Number of pending bands. */

uint16_t *gfxBuffer() { return s_fb; }

void gfxInit() {
	memset(s_fb, 0, sizeof(s_fb));
	gfxMarkAllDirty();
}

// --------------------------------------------------------------------------
// dirty bands
// --------------------------------------------------------------------------
void gfxMarkDirty(int y0, int y1) {
	if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
	if (y1 < 0 || y0 >= GFX_H) return;
	if (y0 < 0) y0 = 0;
	if (y1 >= GFX_H) y1 = GFX_H - 1;

	// merge into any band we touch or sit adjacent to
	for (int i = 0; i < s_bands; i++) {
		if (y0 <= s_bandY1[i] + 1 && y1 >= s_bandY0[i] - 1) {
			if (y0 < s_bandY0[i]) s_bandY0[i] = y0;
			if (y1 > s_bandY1[i]) s_bandY1[i] = y1;
			// merging may have made this band overlap its neighbours
			for (int j = 0; j < s_bands; ) {
				if (j != i && y0 <= s_bandY1[j] + 1 && y1 >= s_bandY0[j] - 1) {
					if (s_bandY0[j] < s_bandY0[i]) s_bandY0[i] = s_bandY0[j];
					if (s_bandY1[j] > s_bandY1[i]) s_bandY1[i] = s_bandY1[j];
					for (int k = j; k < s_bands - 1; k++) {
						s_bandY0[k] = s_bandY0[k + 1];
						s_bandY1[k] = s_bandY1[k + 1];
					}
					s_bands--;
					if (j < i) i--;
				} else {
					j++;
				}
			}
			return;
		}
	}

	if (s_bands < MAX_BANDS) {
		s_bandY0[s_bands] = y0;
		s_bandY1[s_bands] = y1;
		s_bands++;
	} else {
		// out of slots: collapse everything into one span
		int lo = y0, hi = y1;
		for (int i = 0; i < s_bands; i++) {
			if (s_bandY0[i] < lo) lo = s_bandY0[i];
			if (s_bandY1[i] > hi) hi = s_bandY1[i];
		}
		s_bands = 1;
		s_bandY0[0] = lo;
		s_bandY1[0] = hi;
	}
}

void gfxMarkAllDirty() {
	s_bands = 1;
	s_bandY0[0] = 0;
	s_bandY1[0] = GFX_H - 1;
}

int gfxDirtyCount() { return s_bands; }

void gfxDirtyBand(int i, int *y0, int *y1) {
	*y0 = s_bandY0[i];
	*y1 = s_bandY1[i];
}

void gfxClearDirty() { s_bands = 0; }

// --------------------------------------------------------------------------
// clip box
// --------------------------------------------------------------------------
/**
 * @name Clip bounds
 * Half-open: x0/y0 inclusive, x1/y1 exclusive, always within the panel. The
 * default is the whole panel, so every primitive can test against these alone
 * rather than against these *and* the panel.
 * @{
 */
static int s_clipX0 = 0, s_clipY0 = 0, s_clipX1 = GFX_W, s_clipY1 = GFX_H;
/** @} */

void gfxSetClip(int x, int y, int w, int h) {
	s_clipX0 = x < 0 ? 0 : x;
	s_clipY0 = y < 0 ? 0 : y;
	s_clipX1 = x + w > GFX_W ? GFX_W : x + w;
	s_clipY1 = y + h > GFX_H ? GFX_H : y + h;
	// A box with no area clips everything away rather than inverting, which is
	// what an unchecked x1 < x0 would do to the loops below.
	if (s_clipX1 < s_clipX0) s_clipX1 = s_clipX0;
	if (s_clipY1 < s_clipY0) s_clipY1 = s_clipY0;
}

void gfxClearClip() {
	s_clipX0 = 0;
	s_clipY0 = 0;
	s_clipX1 = GFX_W;
	s_clipY1 = GFX_H;
}

// --------------------------------------------------------------------------
// primitives
// --------------------------------------------------------------------------
void gfxFill(uint16_t c) {
	// Deliberately ignores the clip box. Its whole job is "start again from
	// nothing", every caller uses it on a screen change, and a clip left set
	// would turn that into a partial wipe with the old screen showing round it.
	for (int i = 0; i < GFX_W * GFX_H; i++) s_fb[i] = c;
	gfxMarkAllDirty();
}

void gfxRect(int x, int y, int w, int h, uint16_t c) {
	if (w <= 0 || h <= 0) return;
	int x0 = x < s_clipX0 ? s_clipX0 : x;
	int y0 = y < s_clipY0 ? s_clipY0 : y;
	int x1 = x + w; if (x1 > s_clipX1) x1 = s_clipX1;
	int y1 = y + h; if (y1 > s_clipY1) y1 = s_clipY1;
	if (x0 >= x1 || y0 >= y1) return;
	for (int yy = y0; yy < y1; yy++) {
		uint16_t *p = &s_fb[yy * GFX_W + x0];
		for (int xx = x0; xx < x1; xx++) *p++ = c;
	}
	gfxMarkDirty(y0, y1 - 1);
}

void gfxHLine(int x, int y, int w, uint16_t c) { gfxRect(x, y, w, 1, c); }
void gfxVLine(int x, int y, int h, uint16_t c) { gfxRect(x, y, 1, h, c); }

void gfxFrame(int x, int y, int w, int h, uint16_t c) {
	// Thickness grows inward, never outward: the layout static_asserts in ui.cpp
	// and ui_am32.cpp bound every region by its outer edge, and a frame that
	// grew outward in high contrast would silently overrun them.
	int t = themeStroke();
	if (t > w / 2) t = w / 2;
	if (t > h / 2) t = h / 2;
	if (t < 1) t = 1;
	gfxRect(x, y, w, t, c);
	gfxRect(x, y + h - t, w, t, c);
	gfxRect(x, y, t, h, c);
	gfxRect(x + w - t, y, t, h, c);
}

/**
 * @brief Filled rounded rectangle: a square body with the corners chamfered
 *        against an integer circle of radius @p r.
 */
void gfxRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
	if (r <= 0) { gfxRect(x, y, w, h, c); return; }
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	gfxRect(x + r, y, w - 2 * r, h, c);
	gfxRect(x, y + r, r, h - 2 * r, c);
	gfxRect(x + w - r, y + r, r, h - 2 * r, c);
	for (int dy = 0; dy < r; dy++) {
		// integer circle: how far in this row starts
		int dx = r - 1;
		while (dx > 0) {
			int a = r - 1 - dx, b = r - 1 - dy;
			if (a * a + b * b <= (r - 1) * (r - 1)) break;
			dx--;
		}
		int inset = r - 1 - dx;
		gfxRect(x + inset, y + dy, r - inset, 1, c);
		gfxRect(x + w - r, y + dy, r - inset, 1, c);
		gfxRect(x + inset, y + h - 1 - dy, r - inset, 1, c);
		gfxRect(x + w - r, y + h - 1 - dy, r - inset, 1, c);
	}
}

/** @brief One-pixel rounded outline. @see gfxRoundFrame() for the themed one. */
static void roundFrame1px(int x, int y, int w, int h, int r, uint16_t c) {
	if (w <= 0 || h <= 0) return;
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r < 0) r = 0;
	gfxRect(x + r, y, w - 2 * r, 1, c);
	gfxRect(x + r, y + h - 1, w - 2 * r, 1, c);
	gfxRect(x, y + r, 1, h - 2 * r, c);
	gfxRect(x + w - 1, y + r, 1, h - 2 * r, c);
	for (int dy = 0; dy < r; dy++) {
		int dx = r - 1;
		while (dx > 0) {
			int a = r - 1 - dx, b = r - 1 - dy;
			if (a * a + b * b <= (r - 1) * (r - 1)) break;
			dx--;
		}
		int inset = r - 1 - dx;
		gfxRect(x + inset, y + dy, 1, 1, c);
		gfxRect(x + w - 1 - inset, y + dy, 1, 1, c);
		gfxRect(x + inset, y + h - 1 - dy, 1, 1, c);
		gfxRect(x + w - 1 - inset, y + h - 1 - dy, 1, 1, c);
	}
}

void gfxRoundFrame(int x, int y, int w, int h, int r, uint16_t c) {
	// Concentric one-pixel rings rather than a thicker stroke primitive: the
	// corner solver above is written for a single pixel, and nesting it is both
	// less code and geometrically exact at every radius.
	int t = themeStroke();
	for (int i = 0; i < t; i++) {
		if (w - 2 * i < 2 || h - 2 * i < 2) break;
		int ri = r - i;
		if (ri < 0) ri = 0;
		roundFrame1px(x + i, y + i, w - 2 * i, h - 2 * i, ri, c);
	}
}

/**
 * @brief 5x7 glyph data for ASCII 0x20..0x60.
 *
 * Column-major: each byte is one column, bit 0 is the top row. Index 0x40
 * (character 0x60, backtick) is repurposed as a degree sign.
 */
static const uint8_t FONT5X7[][5] = {
	{0x00,0x00,0x00,0x00,0x00}, // 0x20 space
	{0x00,0x00,0x5F,0x00,0x00}, // !
	{0x00,0x07,0x00,0x07,0x00}, // "
	{0x14,0x7F,0x14,0x7F,0x14}, // #
	{0x24,0x2A,0x7F,0x2A,0x12}, // $
	{0x23,0x13,0x08,0x64,0x62}, // %
	{0x36,0x49,0x55,0x22,0x50}, // &
	{0x00,0x05,0x03,0x00,0x00}, // '
	{0x00,0x1C,0x22,0x41,0x00}, // (
	{0x00,0x41,0x22,0x1C,0x00}, // )
	{0x14,0x08,0x3E,0x08,0x14}, // *
	{0x08,0x08,0x3E,0x08,0x08}, // +
	{0x00,0x50,0x30,0x00,0x00}, // ,
	{0x08,0x08,0x08,0x08,0x08}, // -
	{0x00,0x60,0x60,0x00,0x00}, // .
	{0x20,0x10,0x08,0x04,0x02}, // /
	{0x3E,0x51,0x49,0x45,0x3E}, // 0
	{0x00,0x42,0x7F,0x40,0x00}, // 1
	{0x42,0x61,0x51,0x49,0x46}, // 2
	{0x21,0x41,0x45,0x4B,0x31}, // 3
	{0x18,0x14,0x12,0x7F,0x10}, // 4
	{0x27,0x45,0x45,0x45,0x39}, // 5
	{0x3C,0x4A,0x49,0x49,0x30}, // 6
	{0x01,0x71,0x09,0x05,0x03}, // 7
	{0x36,0x49,0x49,0x49,0x36}, // 8
	{0x06,0x49,0x49,0x29,0x1E}, // 9
	{0x00,0x36,0x36,0x00,0x00}, // :
	{0x00,0x56,0x36,0x00,0x00}, // ;
	{0x08,0x14,0x22,0x41,0x00}, // <
	{0x14,0x14,0x14,0x14,0x14}, // =
	{0x00,0x41,0x22,0x14,0x08}, // >
	{0x02,0x01,0x51,0x09,0x06}, // ?
	{0x32,0x49,0x79,0x41,0x3E}, // @
	{0x7E,0x11,0x11,0x11,0x7E}, // A
	{0x7F,0x49,0x49,0x49,0x36}, // B
	{0x3E,0x41,0x41,0x41,0x22}, // C
	{0x7F,0x41,0x41,0x22,0x1C}, // D
	{0x7F,0x49,0x49,0x49,0x41}, // E
	{0x7F,0x09,0x09,0x09,0x01}, // F
	{0x3E,0x41,0x49,0x49,0x7A}, // G
	{0x7F,0x08,0x08,0x08,0x7F}, // H
	{0x00,0x41,0x7F,0x41,0x00}, // I
	{0x20,0x40,0x41,0x3F,0x01}, // J
	{0x7F,0x08,0x14,0x22,0x41}, // K
	{0x7F,0x40,0x40,0x40,0x40}, // L
	{0x7F,0x02,0x0C,0x02,0x7F}, // M
	{0x7F,0x04,0x08,0x10,0x7F}, // N
	{0x3E,0x41,0x41,0x41,0x3E}, // O
	{0x7F,0x09,0x09,0x09,0x06}, // P
	{0x3E,0x41,0x51,0x21,0x5E}, // Q
	{0x7F,0x09,0x19,0x29,0x46}, // R
	{0x46,0x49,0x49,0x49,0x31}, // S
	{0x01,0x01,0x7F,0x01,0x01}, // T
	{0x3F,0x40,0x40,0x40,0x3F}, // U
	{0x1F,0x20,0x40,0x20,0x1F}, // V
	{0x3F,0x40,0x38,0x40,0x3F}, // W
	{0x63,0x14,0x08,0x14,0x63}, // X
	{0x07,0x08,0x70,0x08,0x07}, // Y
	{0x61,0x51,0x49,0x45,0x43}, // Z
	{0x00,0x7F,0x41,0x41,0x00}, // [
	{0x02,0x04,0x08,0x10,0x20}, // backslash
	{0x00,0x41,0x41,0x7F,0x00}, // ]
	{0x04,0x02,0x01,0x02,0x04}, // ^
	{0x40,0x40,0x40,0x40,0x40}, // _
	{0x00,0x07,0x05,0x07,0x00}, // 0x60 -> degree sign
};

static void gfxChar(int x, int y, char ch, uint16_t fg, int scale) {
	if (ch >= 'a' && ch <= 'z') ch -= 32;
	if (ch < 0x20 || ch > 0x60) ch = 0x20;
	const uint8_t *g = FONT5X7[(uint8_t)ch - 0x20];

	// One extra pixel row per lit pixel in high contrast. Vertical only: the
	// glyph cell is 6 px wide for a 5 px glyph, so smearing sideways would close
	// the inter-character gap and run words together. Downward costs no advance
	// width, so not one label moves.
	int extra = themeBold() ? 1 : 0;

	// Reject the whole glyph against the clip box first, so a line of text
	// entirely outside it costs one comparison per character rather than a pass
	// over 35 glyph bits. Everything past this point clips per scaled pixel.
	if (x + 5 * scale <= s_clipX0 || x >= s_clipX1 ||
	    y + 7 * scale + extra <= s_clipY0 || y >= s_clipY1) return;

	// Tracked rather than derived from y and the glyph height, so the dirty band
	// covers exactly the rows that were written -- which after clipping is not
	// the same thing.
	int dirtyTop = GFX_H, dirtyBot = -1;

	for (int col = 0; col < 5; col++) {
		uint8_t bits = g[col];
		if (!bits) continue;
		int px = x + col * scale;
		// Horizontal clip, per column: a glyph straddling the edge keeps the
		// columns that are inside and drops the ones that are not.
		int sx0 = px < s_clipX0 ? s_clipX0 - px : 0;
		int sx1 = px + scale > s_clipX1 ? s_clipX1 - px : scale;
		if (sx0 >= sx1) continue;
		for (int row = 0; row < 7; row++) {
			if (!(bits & (1 << row))) continue;
			for (int sy = 0; sy < scale + extra; sy++) {
				int yy = y + row * scale + sy;
				if (yy < s_clipY0) continue;
				if (yy >= s_clipY1) break;
				uint16_t *p = &s_fb[yy * GFX_W + px];
				for (int sx = sx0; sx < sx1; sx++) p[sx] = fg;
				if (yy < dirtyTop) dirtyTop = yy;
				if (yy > dirtyBot) dirtyBot = yy;
			}
		}
	}
	if (dirtyBot < 0) return;   // nothing visible was drawn
	gfxMarkDirty(dirtyTop, dirtyBot);
}

void gfxText(int x, int y, const char *s, uint16_t fg, int scale) {
	int cx = x;
	for (const char *p = s; *p; p++) {
		gfxChar(cx, y, *p, fg, scale);
		cx += 6 * scale;
	}
}

void gfxTextCenter(int y, const char *s, uint16_t fg, int scale) {
	int x = (GFX_W - gfxTextW(s, scale)) / 2;
	if (x < 0) x = 0;
	gfxText(x, y, s, fg, scale);
}

int gfxTextW(const char *s, int scale) {
	int n = 0;
	for (const char *p = s; *p; p++) n++;
	return n ? (n * 6 - 1) * scale : 0;
}

/**
 * @brief Segment bitmasks per digit, bit 0 = segment a through bit 6 = g.
 *
 * Segment layout:
 * @verbatim
 *    aaaa
 *   f    b
 *   f    b
 *    gggg
 *   e    c
 *   e    c
 *    dddd
 * @endverbatim
 */
static const uint8_t SEG_MAP[10] = {
	//        gfedcba
	0b0111111, // 0
	0b0000110, // 1
	0b1011011, // 2
	0b1001111, // 3
	0b1100110, // 4
	0b1101101, // 5
	0b1111101, // 6
	0b0000111, // 7
	0b1111111, // 8
	0b1101111, // 9
};

void gfxSegDigit(int x, int y, int w, int h, int t, int digit, uint16_t on, uint16_t off) {
	uint8_t m = (digit >= 0 && digit <= 9) ? SEG_MAP[digit] : 0;
	int half = (h - t) / 2;
	// horizontal segments are inset by t so corners meet cleanly
	int hx = x + t, hw = w - 2 * t;

	gfxRect(hx,         y,              hw, t,        (m & 0x01) ? on : off); // a
	gfxRect(x + w - t,  y + t,          t,  half - t, (m & 0x02) ? on : off); // b
	gfxRect(x + w - t,  y + half + t,   t,  half - t, (m & 0x04) ? on : off); // c
	gfxRect(hx,         y + h - t,      hw, t,        (m & 0x08) ? on : off); // d
	gfxRect(x,          y + half + t,   t,  half - t, (m & 0x10) ? on : off); // e
	gfxRect(x,          y + t,          t,  half - t, (m & 0x20) ? on : off); // f
	gfxRect(hx,         y + half,       hw, t,        (m & 0x40) ? on : off); // g
}

void gfxSegNumber(int xRight, int y, int w, int h, int t, int gap, uint32_t value,
                  int digits, uint16_t on, uint16_t ghost) {
	int x = xRight - w;
	uint32_t v = value;
	for (int i = 0; i < digits; i++) {
		int d = (int)(v % 10);
		bool show = (i == 0) || (v != 0);
		if (show) {
			// unlit segments of a live digit stay background-coloured so the
			// number reads cleanly; only unused leading digits get ghosted
			gfxSegDigit(x, y, w, h, t, d, on, C_BG);
		} else {
			gfxSegDigit(x, y, w, h, t, 8, ghost, ghost);
		}
		v /= 10;
		x -= (w + gap);
	}
}
