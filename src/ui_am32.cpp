/**
 * @file ui_am32.cpp
 * @brief AM32 config screen: connect, browse settings, edit, write back.
 *
 * The screen is a state machine. Nothing touches the ESC until the pin handover
 * completes, and nothing writes until the user has held the WRITE button down
 * for a full second — a stray tap must never reprogram a motor controller.
 */

#include "ui_am32.h"
#include "am32_bl.h"
#include "am32_eeprom.h"
#include "esc_task.h"
#include "cst816.h"
#include "gfx.h"
#include "config.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#define ROW_H        26      /**< Height of one settings row. */
#define LIST_Y0      54      /**< First pixel row of the list viewport. */
#define LIST_Y1      213     /**< Last pixel row of the list viewport. */
#define LIST_ROWS    ((LIST_Y1 - LIST_Y0 + 1) / ROW_H)
#define WRITE_HOLD_MS 1000   /**< Hold this long to commit a write. */

/**
 * @defgroup am32_edit Editor bar
 * @brief Big -/+ targets for the selected row.
 *
 * The controls live at the bottom rather than inline in the row: a 26 px row is
 * far smaller than a fingertip, and putting the buttons in a fixed place means
 * they do not move as the list scrolls.
 * @{
 */
#define EDIT_Y       218
#define EDIT_H       44
#define EDIT_BTN_W   58
#define BTN_MINUS_X  6
#define BTN_PLUS_X   (GFX_W - EDIT_BTN_W - 6)
/** @} */

#define FOOT_Y       268     /**< Top of the write/revert/hex row. */
#define FOOT_H       46

/** @brief Screen states. */
enum Am32Screen : uint8_t {
	S_HANDOVER,  /**< Waiting for core1 to release the DShot pin. */
	S_CONNECT,   /**< Prompting for ESC power, retrying the handshake. */
	S_LIST,      /**< Browsing and editing settings. */
	S_WRITING,   /**< Write in progress or just finished. */
};

static Am32Screen s_state = S_HANDOVER;
static uint8_t    s_eeprom[AM32_EEPROM_SIZE];
static uint8_t    s_original[AM32_EEPROM_SIZE];
static bool       s_hexView = false;
static int        s_scroll = 0;
static int        s_selected = -1;
static char       s_status[40] = "";
static uint32_t   s_writeHoldStart = 0;
static bool       s_writeHolding = false;
static uint8_t    s_layoutRev = 0;
static bool       s_leaving = false;
static bool       s_redraw = true;

/** @brief Indices into AM32_FIELDS that apply to this EEPROM's layout. */
static uint16_t s_visible[64];
static uint16_t s_visibleCount = 0;

/** @brief The AM32 bootloader link is fixed at 19200 8N1. */
#define AM32_LINK_BAUD 19200

#if AM32_FORCE_LOW_JUMP
static bool     s_jumped = false;  /**< Optional low-hold jump already tried. */
#endif
static int      s_attempt = 0;     /**< Init strings sent since entering. */

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/** @brief Rebuild the visible-field list for the layout revision just read. */
static void buildVisible() {
	s_layoutRev = s_eeprom[AM32_OFF_LAYOUT_REVISION];
	s_visibleCount = 0;
	for (uint16_t i = 0; i < AM32_FIELD_COUNT && s_visibleCount < 64; i++) {
		if (am32FieldApplies(&AM32_FIELDS[i], s_layoutRev)) {
			s_visible[s_visibleCount++] = i;
		}
	}
}

/** @brief True if any settings byte differs from what we read. */
static bool anyDirty() {
	return memcmp(s_eeprom, s_original, AM32_SETTINGS_SIZE) != 0;
}

static bool hit(int x, int y, int bx, int by, int bw, int bh) {
	return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

static void button(int x, int y, int w, int h, const char *label,
                   uint16_t fill, uint16_t fg) {
	gfxRoundRect(x, y, w, h, 5, fill);
	gfxRoundFrame(x, y, w, h, 5, C_GRID);
	gfxText(x + (w - gfxTextW(label, 1)) / 2, y + (h - 7) / 2, label, fg, 1);
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------
static void drawHeader() {
	gfxRect(0, 0, GFX_W, 50, C_PANEL);
	gfxText(6, 6, "AM32 CONFIG", C_TEXT, 2);

	char name[AM32_NAME_LEN + 1];
	if (s_state == S_LIST || s_state == S_WRITING) {
		am32DeviceName(s_eeprom, name);
		char buf[48];
		if (name[0]) {
			snprintf(buf, sizeof(buf), "%s  %s  FW %u.%u", name, am32BlEscTypeName(),
			         s_eeprom[AM32_OFF_MAIN_REVISION], s_eeprom[AM32_OFF_SUB_REVISION]);
		} else {
			snprintf(buf, sizeof(buf), "%s  FW %u.%u  LAYOUT %u", am32BlEscTypeName(),
			         s_eeprom[AM32_OFF_MAIN_REVISION], s_eeprom[AM32_OFF_SUB_REVISION],
			         s_layoutRev);
		}
		gfxText(6, 26, buf, C_DIM, 1);
	} else if (s_status[0]) {
		gfxText(6, 26, s_status, C_DIM, 1);
	}

	button(GFX_W - 54, 8, 48, 30, "BACK", C_PANEL, C_TEXT);
}

static void drawConnect() {
	gfxRect(0, 50, GFX_W, GFX_H - 50, C_BG);

	gfxTextCenter(84, "POWER-CYCLE THE ESC", C_LIME, 2);
	gfxTextCenter(116, "THE BOOTLOADER ONLY LISTENS", C_DIM, 1);
	gfxTextCenter(130, "BRIEFLY AT POWER-UP. UNPLUG", C_DIM, 1);
	gfxTextCenter(144, "AND REPLUG THE ESC BATTERY.", C_DIM, 1);

	gfxTextCenter(176, s_status[0] ? s_status : "SEARCHING...", C_AMBER, 1);

	// Only if the ESC answered with something we could not parse. Silence is
	// the normal state here -- it just means the power-up window has not come
	// round yet -- so saying anything about it would be noise.
	const uint8_t *rx;
	if (am32BlLastRx(&rx) > 0) {
		gfxText(10, 210, "REPLY NOT UNDERSTOOD:", C_AMBER, 1);
		gfxText(10, 224, s_status, C_AMBER, 1);
		gfxText(10, 244, "LINK WORKS BUT FRAMING IS OFF.", C_GRID, 1);
	}

	// A spinner beats a static screen: it shows we are still retrying.
	int phase = (int)((millis() / 150) % 12);
	for (int i = 0; i < 12; i++) {
		int ang = i * 30;
		int dx = (int)(40 * cosf(ang * 3.14159f / 180.0f));
		int dy = (int)(40 * sinf(ang * 3.14159f / 180.0f));
		uint16_t c = (i == phase) ? C_LIME : C_PANEL;
		gfxRect(GFX_W / 2 + dx - 2, 240 + dy - 2, 5, 5, c);
	}
}

static void drawRow(int slot, uint16_t fieldIdx) {
	const Am32Field *f = &AM32_FIELDS[fieldIdx];
	int y = LIST_Y0 + slot * ROW_H;
	bool sel = (s_selected == (int)fieldIdx);
	bool changed = s_eeprom[f->offset] != s_original[f->offset];

	gfxRect(0, y, GFX_W, ROW_H - 1, sel ? C_PANEL : C_BG);
	if (changed) gfxRect(0, y, 3, ROW_H - 1, C_AMBER);

	gfxText(8, y + 3, f->group, C_GRID, 1);
	gfxText(8, y + 13, f->name, sel ? C_TEXT : C_DIM, 1);

	char val[24];
	am32FormatValue(f, s_eeprom, val, sizeof(val));

	gfxText(GFX_W - 8 - gfxTextW(val, 2), y + 6, val,
	        changed ? C_AMBER : (sel ? C_LIME : C_TEXT), 2);
	gfxHLine(0, y + ROW_H - 1, GFX_W, C_PANEL);
}

static void drawHex() {
	gfxRect(0, LIST_Y0, GFX_W, LIST_Y1 - LIST_Y0 + 1, C_BG);
	// 8 bytes per line keeps each row inside 240 px at scale 1.
	int rows = (LIST_Y1 - LIST_Y0) / 11;
	int start = s_scroll * 8;
	for (int r = 0; r < rows; r++) {
		int base = start + r * 8;
		if (base >= AM32_SETTINGS_SIZE) break;
		char line[64];
		int n = snprintf(line, sizeof(line), "%02X ", base);
		for (int i = 0; i < 8 && base + i < AM32_SETTINGS_SIZE; i++) {
			n += snprintf(line + n, sizeof(line) - n, "%02X ", s_eeprom[base + i]);
		}
		gfxText(6, LIST_Y0 + r * 11, line, C_DIM, 1);
	}
}

static void drawList() {
	if (s_hexView) {
		drawHex();
	} else {
		gfxRect(0, LIST_Y0, GFX_W, LIST_Y1 - LIST_Y0 + 1, C_BG);
		for (int i = 0; i < LIST_ROWS; i++) {
			int idx = s_scroll + i;
			if (idx >= (int)s_visibleCount) break;
			drawRow(i, s_visible[idx]);
		}
	}

	gfxRect(0, LIST_Y1 + 1, GFX_W, GFX_H - LIST_Y1 - 1, C_BG);

	// ---- editor bar: big -/+ acting on the selected row ----
	const Am32Field *sel = (s_selected >= 0 && s_selected < (int)AM32_FIELD_COUNT)
	                           ? &AM32_FIELDS[s_selected] : nullptr;

	if (sel && !s_hexView) {
		button(BTN_MINUS_X, EDIT_Y, EDIT_BTN_W, EDIT_H, "-", C_GRID, C_WHITE);
		button(BTN_PLUS_X,  EDIT_Y, EDIT_BTN_W, EDIT_H, "+", C_GRID, C_WHITE);
		// The label is worth the space: the buttons are far from the row they
		// act on, so the bar has to say what it is editing.
		int cx = BTN_MINUS_X + EDIT_BTN_W;
		int cw = BTN_PLUS_X - cx;
		gfxText(cx + (cw - gfxTextW(sel->name, 1)) / 2, EDIT_Y + 2, sel->name, C_DIM, 1);
		char val[24];
		am32FormatValue(sel, s_eeprom, val, sizeof(val));
		bool ch = s_eeprom[sel->offset] != s_original[sel->offset];
		gfxText(cx + (cw - gfxTextW(val, 2)) / 2, EDIT_Y + 14, val,
		        ch ? C_AMBER : C_LIME, 2);
		const char *hint = "SWIPE ROW = COARSE";
		gfxText(cx + (cw - gfxTextW(hint, 1)) / 2, EDIT_Y + 33, hint, C_GRID, 1);
	} else if (!s_hexView) {
		gfxTextCenter(EDIT_Y + 12, "TAP A SETTING TO EDIT", C_GRID, 1);
		gfxTextCenter(EDIT_Y + 26, "SWIPE IT SIDEWAYS FOR COARSE", C_GRID, 1);
	}

	// ---- footer ----
	bool dirty = anyDirty();
	uint16_t writeFill = C_PANEL;
	if (s_writeHolding) {
		uint32_t held = millis() - s_writeHoldStart;
		int pct = (int)(held * 100 / WRITE_HOLD_MS);
		if (pct > 100) pct = 100;
		writeFill = C_REDDARK;
		gfxRect(6, FOOT_Y - 4, 110 * pct / 100, 3, C_LIME);
	}
	button(6, FOOT_Y, 110, FOOT_H,
	       dirty ? "HOLD TO WRITE" : "NO CHANGES",
	       dirty ? writeFill : C_PANEL, dirty ? C_WHITE : C_GRID);
	button(122, FOOT_Y, 52, FOOT_H, "REVERT", C_PANEL, dirty ? C_AMBER : C_GRID);
	button(180, FOOT_Y, 54, FOOT_H, s_hexView ? "FIELDS" : "HEX", C_PANEL, C_CYAN);
}

static void drawWriting() {
	gfxRect(0, 50, GFX_W, GFX_H - 50, C_BG);
	gfxTextCenter(140, s_status, C_TEXT, 2);
	button(70, 200, 100, 36, "OK", C_PANEL, C_TEXT);
}

// ---------------------------------------------------------------------------
// state machine
// ---------------------------------------------------------------------------
void uiAm32Enter() {
	s_state = S_HANDOVER;
	s_hexView = false;
	s_scroll = 0;
	s_selected = -1;
	s_leaving = false;
	s_redraw = true;
	s_writeHolding = false;
	s_visibleCount = 0;
#if AM32_FORCE_LOW_JUMP
	s_jumped = false;
#endif
	s_attempt = 0;
	am32BlSetBaud(AM32_LINK_BAUD);
	snprintf(s_status, sizeof(s_status), "RELEASING DSHOT PIN");
	memset(s_eeprom, 0, sizeof(s_eeprom));
	memset(s_original, 0, sizeof(s_original));
	escTaskSuspend();
}

void uiAm32Exit() {
	am32BlEnd();
	escTaskResume();
	s_state = S_HANDOVER;
}

bool uiAm32Active() { return !s_leaving; }

/** @brief Attempt one handshake plus settings read. */
static void tryConnect() {
	uint8_t info[9];

	// The bootloader opens its listening window at power-up, so the way in is
	// to repeat the init string as fast as possible and be transmitting when
	// that window appears. Holding the line low, in contrast, makes us deaf for
	// its duration and loses more windows than it forces.
#if AM32_FORCE_LOW_JUMP
	if (!s_jumped) {
		am32BlJumpToBootloader();
		s_jumped = true;
	}
#endif

	s_attempt++;
	Am32Result r = am32BlHandshake(info);
	if (r != AM32_OK) {
		const uint8_t *rx;
		uint8_t n = am32BlLastRx(&rx);

		if (n) {
			// Bytes arrived: the link is alive and this is a framing or rate
			// problem, which is a different fix from silence entirely.
			snprintf(s_status, sizeof(s_status), "%luB: %02X %02X %02X @%lu",
			         (unsigned long)n, rx[0], n > 1 ? rx[1] : 0, n > 2 ? rx[2] : 0,
			         (unsigned long)am32BlBaud());
			return;
		}

		snprintf(s_status, sizeof(s_status), "WAITING... (TRY %d)", s_attempt);
		return;
	}

	r = am32BlRead(am32BlEepromAddr(), s_eeprom, AM32_SETTINGS_SIZE);
	if (r != AM32_OK) {
		snprintf(s_status, sizeof(s_status), "READ FAILED (%s)", am32ResultText(r));
		return;
	}

	if (!am32Plausible(s_eeprom)) {
		snprintf(s_status, sizeof(s_status), "NO VALID SETTINGS");
		return;
	}

	memcpy(s_original, s_eeprom, sizeof(s_eeprom));
	buildVisible();
	s_scroll = 0;
	s_selected = -1;
	s_state = S_LIST;
	s_redraw = true;
	snprintf(s_status, sizeof(s_status), "READ OK");
}

/** @brief Commit the settings block. */
static void doWrite() {
	Am32Result r = am32BlWrite(am32BlEepromAddr(), s_eeprom, AM32_SETTINGS_SIZE);
	if (r != AM32_OK) {
		snprintf(s_status, sizeof(s_status), "WRITE FAILED: %s", am32ResultText(r));
		return;
	}
	// Read back rather than trusting the ACK: a write that reports success but
	// lands wrong is worse than one that fails loudly.
	//
	// Give the ESC time first. It is erasing and reprogramming a flash page and
	// will not answer sensibly until that finishes -- reading too soon is what
	// makes a perfectly good write look like a failure.
	uint8_t check[AM32_SETTINGS_SIZE];
	r = AM32_ERR_TIMEOUT;
	for (int attempt = 0; attempt < 3 && r != AM32_OK; attempt++) {
		delay(AM32_WRITE_SETTLE_MS);
		r = am32BlRead(am32BlEepromAddr(), check, AM32_SETTINGS_SIZE);
	}
	if (r != AM32_OK) {
		snprintf(s_status, sizeof(s_status), "VERIFY READ: %s", am32ResultText(r));
		return;
	}
	if (memcmp(check, s_eeprom, AM32_SETTINGS_SIZE) != 0) {
		// Name the first offending byte. "Mismatch" alone cannot distinguish a
		// write that did not take from a read that came back skewed.
		int i = 0;
		while (i < AM32_SETTINGS_SIZE && check[i] == s_eeprom[i]) i++;
		snprintf(s_status, sizeof(s_status), "DIFF @%02X: GOT %02X WANT %02X",
		         i, check[i], s_eeprom[i]);
		return;
	}
	memcpy(s_original, s_eeprom, AM32_SETTINGS_SIZE);
	snprintf(s_status, sizeof(s_status), "WRITE VERIFIED");
}

/** @brief Held-button repeat state for the editor bar. */
static int      s_repeatDir = 0;
static uint32_t s_repeatStart = 0;
static uint32_t s_repeatLast = 0;

/**
 * @brief Repeat interval for a button held @p heldMs, in milliseconds.
 *
 * Accelerates, because the useful ranges are wide: PWM frequency spans 8..144
 * and motor KV 1..255, which is far too many presses at one step per tap.
 */
static uint32_t repeatInterval(uint32_t heldMs) {
	if (heldMs > 2500) return 25;
	if (heldMs > 1200) return 60;
	return 120;
}

static void handleListTouch(const TouchState *t) {
	int x = t->x, y = t->y;

	// --- write button: needs every frame, including finger-off, to time the
	//     hold and to cancel it the moment the finger moves away ---
	bool dirty = anyDirty();
	if (dirty && t->down && hit(x, y, 6, FOOT_Y, 110, FOOT_H)) {
		if (!s_writeHolding) { s_writeHolding = true; s_writeHoldStart = millis(); }
		if (millis() - s_writeHoldStart >= WRITE_HOLD_MS) {
			s_writeHolding = false;
			s_state = S_WRITING;
			snprintf(s_status, sizeof(s_status), "WRITING...");
			s_redraw = true;
			return;
		}
		s_redraw = true;
	} else {
		if (s_writeHolding) s_redraw = true;
		s_writeHolding = false;
	}

	// --- editor bar: also needs every frame, to drive hold-to-repeat ---
	const Am32Field *sel = (!s_hexView && s_selected >= 0 &&
	                        s_selected < (int)AM32_FIELD_COUNT)
	                           ? &AM32_FIELDS[s_selected] : nullptr;
	bool onMinus = hit(x, y, BTN_MINUS_X, EDIT_Y, EDIT_BTN_W, EDIT_H);
	bool onPlus  = hit(x, y, BTN_PLUS_X,  EDIT_Y, EDIT_BTN_W, EDIT_H);

	if (t->down && sel && (onMinus || onPlus)) {
		int dir = onMinus ? -1 : +1;
		uint32_t now = millis();
		if (t->pressed || s_repeatDir != dir) {
			am32Adjust(sel, s_eeprom, dir);
			s_repeatDir = dir;
			s_repeatStart = now;
			s_repeatLast = now;
			s_redraw = true;
		} else if (now - s_repeatStart > 400 &&
		           now - s_repeatLast >= repeatInterval(now - s_repeatStart)) {
			am32Adjust(sel, s_eeprom, dir);
			s_repeatLast = now;
			s_redraw = true;
		}
		return;
	}
	if (!t->down) s_repeatDir = 0;

	if (!t->pressed) return;

	// --- discrete taps, dispatched strictly by band ---
	//
	// Each band returns. A press in the editor bar must never fall through to
	// the row-selection code beneath it: that is what made the buttons select
	// whichever list entry happened to sit behind them.
	if (y >= FOOT_Y) {
		if (hit(x, y, 122, FOOT_Y, 52, FOOT_H)) {
			memcpy(s_eeprom, s_original, sizeof(s_eeprom));
			s_redraw = true;
		} else if (hit(x, y, 180, FOOT_Y, 54, FOOT_H)) {
			s_hexView = !s_hexView;
			s_scroll = 0;
			s_redraw = true;
		}
		return;
	}
	if (y >= EDIT_Y) return;                       // editor bar, handled above
	if (s_hexView || y < LIST_Y0 || y > LIST_Y1) return;

	int slot = (y - LIST_Y0) / ROW_H;
	int idx = s_scroll + slot;
	if (idx >= (int)s_visibleCount) return;

	// Tapping a row only selects it; the value changes from the editor bar.
	s_selected = (int)s_visible[idx];
	s_redraw = true;
}

/**
 * @brief List gestures: vertical drag scrolls, horizontal drag adjusts.
 *
 * The two axes are locked exclusively on first movement, so a swipe is either
 * a scroll or an edit and never both. Horizontal gives coarse control -- one
 * full-width swipe covers a field's entire range -- while the editor-bar
 * buttons stay one step per press for fine work.
 */
static void handleListGesture(const TouchState *t) {
	/** @brief Travel before an axis is committed to, in pixels. */
	static const int AXIS_LOCK_PX = 10;
	/** @brief Horizontal travel that spans a field's whole range. */
	static const int SWIPE_FULL_PX = 180;

	enum GMode : uint8_t { G_NONE, G_VERT, G_HORIZ };
	static GMode   mode = G_NONE;
	static int16_t startX = 0, startY = 0;
	static int16_t anchorX = 0, lastY = 0;
	static uint8_t anchorRaw = 0;

	const Am32Field *sel = (!s_hexView && s_selected >= 0 &&
	                        s_selected < (int)AM32_FIELD_COUNT)
	                           ? &AM32_FIELDS[s_selected] : nullptr;

	if (t->pressed) {
		mode = G_NONE;
		startX = anchorX = t->x;
		startY = lastY = t->y;
		if (sel) anchorRaw = s_eeprom[sel->offset];
		return;
	}
	if (!t->down) { mode = G_NONE; return; }

	// Only gestures that began inside the list count, so a drag that strays
	// out of the editor bar cannot scroll or edit.
	if (startY < LIST_Y0 || startY > LIST_Y1) return;

	int dx = t->x - startX, dy = t->y - startY;
	int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;

	if (mode == G_NONE) {
		if (adx > AXIS_LOCK_PX && adx >= ady)      mode = G_HORIZ;
		else if (ady > AXIS_LOCK_PX)               mode = G_VERT;
		else return;
		// Re-base at the moment of commitment so neither the value nor the
		// scroll position jumps by the distance already travelled.
		anchorX = t->x;
		lastY = t->y;
		if (sel) anchorRaw = s_eeprom[sel->offset];
	}

	if (mode == G_VERT) {
		int maxScroll = s_hexView ? (AM32_SETTINGS_SIZE / 8) - 1
		                          : (int)s_visibleCount - LIST_ROWS;
		if (maxScroll < 0) maxScroll = 0;
		int d = t->y - lastY;
		if (d > ROW_H || d < -ROW_H) {
			s_scroll -= d / ROW_H;
			if (s_scroll < 0) s_scroll = 0;
			if (s_scroll > maxScroll) s_scroll = maxScroll;
			lastY = t->y;
			s_selected = -1;
			s_redraw = true;
		}
		return;
	}

	// --- horizontal: coarse adjustment of the selected field ---
	if (!sel) return;
	int32_t range = (int32_t)sel->rawMax - (int32_t)sel->rawMin;
	if (range <= 0) return;

	int32_t v = (int32_t)anchorRaw +
	            (int32_t)(t->x - anchorX) * range / SWIPE_FULL_PX;

	// Re-anchor at the rails, so reversing direction responds at once rather
	// than having to unwind however far past the end the finger went.
	if (v <= sel->rawMin) {
		v = sel->rawMin;
		anchorX = t->x;
		anchorRaw = sel->rawMin;
	} else if (v >= sel->rawMax) {
		v = sel->rawMax;
		anchorX = t->x;
		anchorRaw = sel->rawMax;
	}

	// Snap to the field's own step so stepped fields stay on legal values --
	// motor poles, for instance, must remain even.
	if (sel->step > 1) {
		v = sel->rawMin + ((v - sel->rawMin) / sel->step) * sel->step;
	}

	if ((uint8_t)v != s_eeprom[sel->offset]) {
		s_eeprom[sel->offset] = (uint8_t)v;
		s_redraw = true;
	}
}

bool uiAm32Tick(const TouchState *t) {
	// BACK is drawn by drawHeader() in every state, so it has to be handled in
	// every state. Doing it here rather than per-state stops it being a button
	// that is visible but inert on whichever screen forgot to check for it.
	if (t->pressed && hit(t->x, t->y, GFX_W - 54, 8, 48, 30)) {
		s_leaving = true;
		uiAm32Exit();
		return false;
	}

	switch (s_state) {
		case S_HANDOVER:
			if (escTaskSuspended()) {
				am32BlBegin(DSHOT_PIN);
				s_state = S_CONNECT;
				snprintf(s_status, sizeof(s_status), "SEARCHING...");
			}
			s_redraw = true;
			break;

		case S_CONNECT:
			// Retry as fast as the frame loop allows. A failed attempt costs
			// about 40 ms, and the power-up window is narrow, so retry rate is
			// what determines whether we catch it.
			tryConnect();
			s_redraw = true;
			break;

		case S_LIST:
			// Selection first: the press selects the row under the finger, and
			// the gesture handler then anchors on that field's current value,
			// so press-and-swipe adjusts the row you actually touched.
			handleListTouch(t);
			handleListGesture(t);
			break;

		case S_WRITING:
			if (strcmp(s_status, "WRITING...") == 0) {
				drawHeader();
				drawWriting();
				doWrite();
				s_redraw = true;
			} else if (t->pressed && hit(t->x, t->y, 70, 200, 100, 36)) {
				s_state = S_LIST;
				s_redraw = true;
			}
			break;
	}

	if (s_redraw) {
		s_redraw = false;
		drawHeader();
		switch (s_state) {
			case S_HANDOVER:
			case S_CONNECT:  drawConnect(); break;
			case S_LIST:     drawList();    break;
			case S_WRITING:  drawWriting(); break;
		}
	}

	if (s_leaving) {
		uiAm32Exit();
		return false;
	}
	return true;
}
