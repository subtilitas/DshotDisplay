/**
 * @file ui_lcars.cpp
 * @brief LCARS (Star Trek) themed drawing helpers.
 *
 * Provides the distinctive pill-shaped buttons, elbow frame, and colour
 * palette inspired by the LCARS computer interface from Star Trek: TNG.
 */

#include "config.h"
#if UI_THEME == 1

#include "ui_lcars.h"
#include "gfx.h"
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// LCARS frame constants
// ---------------------------------------------------------------------------

/** @brief Width of the left sidebar elbow. */
#define ELBOW_W      36
/** @brief Height of the top header bar. */
#define HEADER_H     26
/** @brief Corner radius of the elbow. */
#define ELBOW_R      14
/** @brief Gap between frame and content. */
#define FRAME_GAP    2

void lcarsDrawFrame() {
	// Top-left elbow: a thick rounded corner that sweeps from vertical to
	// horizontal. We approximate it with a filled round-rect that we clip
	// visually by drawing the BG over the interior.

	// Top header bar
	gfxRoundRect(0, 0, GFX_W, HEADER_H, ELBOW_R, LC_TAN);
	// Left sidebar (connects to header)
	gfxRect(0, 0, ELBOW_W, GFX_H, LC_TAN);
	// Inner cutout — creates the elbow shape
	gfxRoundRect(ELBOW_W, HEADER_H, GFX_W - ELBOW_W, GFX_H - HEADER_H, ELBOW_R, LC_BG);
	// Clean up inside the elbow curve
	gfxRect(ELBOW_W + FRAME_GAP, HEADER_H + FRAME_GAP,
	        GFX_W - ELBOW_W - FRAME_GAP * 2, GFX_H - HEADER_H - FRAME_GAP * 2, LC_BG);

	// Decorative sidebar segments with gaps — the hallmark LCARS look
	int segY = HEADER_H + ELBOW_R + 4;
	const uint16_t segCols[] = {LC_PURPLE, LC_BLUE, LC_TAN, LC_ORANGE, LC_PURPLE};
	const int segH[] = {40, 30, 50, 45, 0}; // 0 = fill remainder
	for (int i = 0; i < 5; i++) {
		int h = segH[i];
		if (h == 0) h = GFX_H - segY - 4;
		if (segY + h > GFX_H - 4) h = GFX_H - 4 - segY;
		if (h <= 0) break;
		gfxRoundRect(2, segY, ELBOW_W - 4, h, 8, segCols[i]);
		segY += h + 3;
	}

	// Bottom accent bar
	gfxRoundRect(ELBOW_W + 4, GFX_H - 8, GFX_W - ELBOW_W - 8, 6, 3, LC_PURPLE);
}

void lcarsBtn(int x, int y, int w, int h, const char *label,
              uint16_t fill, uint16_t fg, int scale) {
	// Pill shape — radius = half height
	int r = h / 2;
	gfxRoundRect(x, y, w, h, r, fill);
	int tw = gfxTextW(label, scale);
	gfxText(x + (w - tw) / 2, y + (h - 7 * scale) / 2, label, fg, scale);
}

void lcarsLabelled(int x, int y, int w, int h, const char *label,
                   const char *value, uint16_t vcol) {
	// Small coloured header sweep across the top of the tile
	gfxRoundRect(x, y, w, 10, 4, LC_PURPLE);
	gfxRect(x, y + 6, w, h - 6, LC_DKBLUE);
	gfxText(x + 4, y + 2, label, LC_BG, 1);
	gfxText(x + 6, y + 14, value, vcol, 2);
}

void lcarsDrawSplash() {
	gfxFill(LC_BG);

	// Draw a simplified LCARS frame for the splash
	gfxRoundRect(10, 40, 220, 8, 4, LC_TAN);
	gfxRoundRect(10, 270, 220, 8, 4, LC_PURPLE);

	gfxTextCenter(90, "LCARS", LC_TAN, 4);
	gfxTextCenter(130, "DSHOT", LC_ORANGE, 4);
	gfxTextCenter(170, "DISPLAY", LC_BLUE, 3);
	gfxTextCenter(210, "BIDIRECTIONAL ESC TESTER", LC_DIM, 1);
	gfxTextCenter(240, "JuWi made", LC_CYAN, 2);
}

#endif // UI_THEME == 1
