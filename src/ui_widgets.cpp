/**
 * @file ui_widgets.cpp
 * @brief The shared widgets. @see ui_widgets.h for why they are shared.
 */

#include "ui_widgets.h"
#include "ui_input.h"
#include "gfx.h"
#include "theme.h"

// The BACK position is written in terms of a literal 240 in the header, because
// it has to be usable in a static_assert there. Check it against the real panel
// width here, where gfx.h is in scope.
static_assert(UI_BACK_X == GFX_W - UI_BACK_W - 6,
              "UI_BACK_X was written against a different panel width");
static_assert(UI_BACK_X + UI_BACK_W < GFX_W, "BACK runs off the panel");

bool uiPressing(const UiRect &r, const TouchState *t) {
	return inputPressing(t, r.x, r.y, r.w, r.h);
}

bool uiTapped(const UiRect &r, const TouchState *t) {
	return inputTapped(t, r.x, r.y, r.w, r.h);
}

void uiButton(const UiRect &r, const char *label, uint16_t fill, uint16_t fg,
              int scale, bool pressed) {
	gfxRoundRect(r.x, r.y, r.w, r.h, UI_RADIUS, fill);
	gfxRoundFrame(r.x, r.y, r.w, r.h, UI_RADIUS, pressed ? C_INK : C_GRID);
	// The inner ring is what makes a press read at arm's length; it needs a
	// button big enough to hold two concentric frames and still show fill.
	if (pressed && r.w > 6 && r.h > 6)
		gfxRoundFrame(r.x + 2, r.y + 2, r.w - 4, r.h - 4, UI_RADIUS - 2, C_INK);
	int d = pressed ? 1 : 0;
	gfxText(r.x + d + (r.w - gfxTextW(label, scale)) / 2,
	        r.y + d + (r.h - 7 * scale) / 2, label, fg, scale);
}

UiRect uiBackRect() {
	return UiRect{UI_BACK_X, UI_BACK_Y, UI_BACK_W, UI_BACK_H};
}

bool uiBackTapped(const TouchState *t) {
	return uiTapped(uiBackRect(), t);
}

void uiHeader(const char *title, const TouchState *t) {
	gfxRect(0, 0, GFX_W, UI_HDR_H, C_PANEL);
	// Scale 2 is 14 px tall, centred in the band rather than at a hand-picked y,
	// so the title sits on the same baseline on every screen.
	gfxText(UI_MARGIN, (UI_HDR_H - 14) / 2, title, C_TEXT, 2);
	uiButton(uiBackRect(), "BACK", C_PANEL, C_TEXT, 1, uiPressing(uiBackRect(), t));
}

void uiStripClear() {
	gfxRect(0, UI_STRIP_Y, GFX_W, UI_STRIP_H, C_BG);
}

void uiStripText(const char *text, uint16_t col) {
	// Scale 1 is 7 px tall; centring it in the strip lines it up with a chip
	// sitting at the other end of the same row.
	gfxText(UI_MARGIN, UI_STRIP_Y + (UI_STRIP_H - 7) / 2, text, col, 1);
}

void uiChip(const char *text, uint16_t fill) {
	int w = gfxTextW(text, 1) + 12;
	int x = GFX_W - UI_MARGIN - w;
	gfxRoundRect(x, UI_STRIP_Y, w, UI_STRIP_H, 4, fill);
	gfxText(x + 6, UI_STRIP_Y + (UI_STRIP_H - 7) / 2, text, C_ONACCENT, 1);
}

void uiRow(int y, int h, const char *label, const char *value, uint16_t vcol,
           int scale) {
	gfxRect(0, y, GFX_W, h, C_BG);
	gfxText(UI_MARGIN, y + (h - 7) / 2, label, C_DIM, 1);
	gfxText(GFX_W - UI_MARGIN - gfxTextW(value, scale), y + (h - 7 * scale) / 2,
	        value, vcol, scale);
}
