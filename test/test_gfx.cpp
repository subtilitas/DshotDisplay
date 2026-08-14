/**
 * @file test_gfx.cpp
 * @brief Dirty-band bookkeeping and glyph rendering tests.
 *
 * The dirty bands decide how much SPI traffic a frame costs, and their merge
 * and saturation paths are pure logic -- exactly the kind of code that is easy
 * to break silently and cheap to pin down here.
 */

#include "check.h"
#include "gfx.h"

#include <string.h>

/**
 * @brief A fixed foreground colour for the renderer tests.
 *
 * Deliberately a literal rather than a palette name. These checks assert the
 * exact word written into specific framebuffer cells, so they must not move
 * when a theme does -- the renderer is what is under test here, not the
 * palette. @see test_settings.cpp for the theme-dependent side.
 */
static const uint16_t TEST_FG = 0xFFFF;

/** @brief Reset the framebuffer and band list to a clean, undirty slate. */
static void resetGfx() {
	gfxInit();
	gfxClearDirty();
}

static void testBands() {
	section("Dirty bands");

	resetGfx();
	checkInt("clean slate has no bands", gfxDirtyCount(), 0);

	gfxMarkDirty(10, 20);
	checkInt("one band", gfxDirtyCount(), 1);

	int y0, y1;
	gfxDirtyBand(0, &y0, &y1);
	checkInt("band starts where marked", y0, 10);
	checkInt("band ends where marked", y1, 20);

	// A disjoint band must stay disjoint.
	gfxMarkDirty(100, 110);
	checkInt("disjoint band stays separate", gfxDirtyCount(), 2);

	// Overlapping bands merge into one.
	resetGfx();
	gfxMarkDirty(10, 20);
	gfxMarkDirty(15, 25);
	checkInt("overlapping bands merge", gfxDirtyCount(), 1);
	gfxDirtyBand(0, &y0, &y1);
	checkInt("merge keeps the lower edge", y0, 10);
	checkInt("merge keeps the upper edge", y1, 25);

	// Adjacent bands merge too: 21 is exactly one row past 20.
	resetGfx();
	gfxMarkDirty(10, 20);
	gfxMarkDirty(21, 30);
	checkInt("adjacent bands merge", gfxDirtyCount(), 1);
	gfxDirtyBand(0, &y0, &y1);
	checkInt("adjacent merge spans both", y1, 30);

	// A band bridging two existing bands absorbs both.
	resetGfx();
	gfxMarkDirty(10, 15);
	gfxMarkDirty(40, 45);
	gfxMarkDirty(14, 41);
	checkInt("bridging band absorbs both", gfxDirtyCount(), 1);
	gfxDirtyBand(0, &y0, &y1);
	checkInt("bridge starts at the low band", y0, 10);
	checkInt("bridge ends at the high band", y1, 45);

	// Inverted arguments are normalised, and off-screen rows clipped.
	resetGfx();
	gfxMarkDirty(50, 40);
	gfxDirtyBand(0, &y0, &y1);
	checkInt("swapped bounds normalised low", y0, 40);
	checkInt("swapped bounds normalised high", y1, 50);

	resetGfx();
	gfxMarkDirty(-100, 5);
	gfxDirtyBand(0, &y0, &y1);
	checkInt("negative rows clip to 0", y0, 0);
	checkInt("clip keeps the visible end", y1, 5);

	resetGfx();
	gfxMarkDirty(GFX_H + 10, GFX_H + 20);
	checkInt("fully off-screen band is dropped", gfxDirtyCount(), 0);

	resetGfx();
	gfxMarkDirty(-300, -5);
	checkInt("fully negative band is dropped", gfxDirtyCount(), 0);

	// Fill every slot, then push one more: the whole set must collapse into a
	// single spanning band rather than overflowing the fixed arrays.
	resetGfx();
	for (int i = 0; i < 8; i++) gfxMarkDirty(i * 30, i * 30 + 5);
	checkInt("eight disjoint bands all kept", gfxDirtyCount(), 8);
	gfxMarkDirty(300, 319);
	checkInt("ninth band collapses the set", gfxDirtyCount(), 1);
	gfxDirtyBand(0, &y0, &y1);
	checkInt("collapse starts at the lowest band", y0, 0);
	checkInt("collapse ends at the new band", y1, 319);
}

static void testGlyphs() {
	section("Glyph rendering");

	// A fully off-screen character must not draw or dirty anything.
	resetGfx();
	gfxText(10, GFX_H + 20, "X", TEST_FG, 1);
	checkInt("off-screen text dirties nothing", gfxDirtyCount(), 0);
	gfxText(-6, 10, "X", TEST_FG, 1);   // entirely left of the framebuffer
	checkInt("off-left text dirties nothing", gfxDirtyCount(), 0);

	// ' ' at scale 1 is five empty columns: no pixels, no dirty band.
	resetGfx();
	gfxText(10, 10, " ", TEST_FG, 1);
	checkInt("space dirties nothing", gfxDirtyCount(), 0);

	// '0' at scale 1 lights rows 1..5 in every column, so its band starts one
	// row below the glyph top and ends on the glyph's last row.
	resetGfx();
	gfxText(10, 10, "0", TEST_FG, 1);
	checkInt("glyph creates one band", gfxDirtyCount(), 1);
	int y0, y1;
	gfxDirtyBand(0, &y0, &y1);
	checkInt("glyph band starts on its first lit row", y0, 11);
	checkInt("glyph band ends on its last row", y1, 16);

	// Partially visible at the bottom edge: clipped, not dropped.
	resetGfx();
	gfxText(10, GFX_H - 3, "0", TEST_FG, 1);
	checkInt("bottom-clipped glyph still draws", gfxDirtyCount(), 1);
	gfxDirtyBand(0, &y0, &y1);
	checkInt("clipped band stops at the edge", y1, GFX_H - 1);

	// A lit pixel actually lands in the framebuffer, an unlit one stays clear.
	resetGfx();
	gfxText(0, 0, "0", TEST_FG, 1);
	// '0' = {0x3E,0x51,0x49,0x45,0x3E}: column 0 rows 1..5 lit, row 0 clear.
	checkInt("lit pixel written", gfxBuffer()[1 * GFX_W + 0], TEST_FG);
	checkInt("unlit pixel untouched", gfxBuffer()[0 * GFX_W + 0], 0);

	// Scale 2: one glyph pixel is a 2x2 block.
	resetGfx();
	gfxText(0, 0, "0", TEST_FG, 2);
	checkInt("scaled pixel block, top-left", gfxBuffer()[2 * GFX_W + 0], TEST_FG);
	checkInt("scaled pixel block, bottom-right", gfxBuffer()[3 * GFX_W + 1], TEST_FG);
	gfxDirtyBand(0, &y0, &y1);
	checkInt("scaled band starts on its first lit row", y0, 2);
	checkInt("scaled band ends on its last row", y1, 13);

	// Left edge: '0' at x = -2 drops its first two columns, so screen
	// column 0 shows glyph column 2 (0x49: rows 0, 3 and 6 lit).
	resetGfx();
	gfxText(-2, 0, "0", TEST_FG, 1);
	checkInt("clipped-left glyph column, row 0", gfxBuffer()[0 * GFX_W + 0], TEST_FG);
	checkInt("clipped-left glyph column, row 6", gfxBuffer()[6 * GFX_W + 0], TEST_FG);
	checkInt("clipped-left glyph column, row 1", gfxBuffer()[1 * GFX_W + 0], 0);
	checkInt("clipped-left glyph still dirties", gfxDirtyCount(), 1);

	// Left clip at scale 2: '0' at x = -2 drops its first column, so screen
	// column 0 shows glyph column 1 (0x51: rows 0, 4 and 6 lit) as a 2x2 block.
	resetGfx();
	gfxText(-2, 0, "0", TEST_FG, 2);
	checkInt("scaled left-clip dirties", gfxDirtyCount(), 1);
	checkInt("scaled left-clip column, row 0", gfxBuffer()[0 * GFX_W + 0], TEST_FG);
	checkInt("scaled left-clip column, row 8", gfxBuffer()[8 * GFX_W + 0], TEST_FG);
	checkInt("scaled left-clip column, row 12", gfxBuffer()[12 * GFX_W + 0], TEST_FG);
	checkInt("scaled left-clip column, row 2", gfxBuffer()[2 * GFX_W + 0], 0);

	// Lowercase folds to uppercase, unknown characters render as space.
	resetGfx();
	gfxText(0, 0, "a", TEST_FG, 1);            // 'A' has lit pixels
	checkInt("lowercase draws as uppercase", gfxDirtyCount(), 1);
	resetGfx();
	gfxText(0, 0, "\x7F", TEST_FG, 1);          // DEL -> space
	checkInt("unknown char draws nothing", gfxDirtyCount(), 0);
}

void runGfxTests() {
	testBands();
	testGlyphs();
}
