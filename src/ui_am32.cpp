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
#include "touch.h"
#include "ui_input.h"
#include "ui_widgets.h"
#include "gfx.h"
#include "config.h"

#include "plat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ROW_H        26      /**< Height of one settings row. */
#define HEX_ROW_H    11      /**< Height of one hex-dump line. */
#define LIST_Y0      UI_BODY_Y  /**< First pixel row of the list viewport. */
#define LIST_Y1      215     /**< Last pixel row of the list viewport. */
#define LIST_H       (LIST_Y1 - LIST_Y0 + 1)
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

/** @brief The three footer targets. */
static const UiRect BTN_WRITE  = { 6, FOOT_Y, 110, FOOT_H };
static const UiRect BTN_REVERT = { 122, FOOT_Y, 52, FOOT_H };
static const UiRect BTN_HEX    = { 180, FOOT_Y, 54, FOOT_H };
/** @brief The editor bar's two steppers. */
static const UiRect BTN_MINUS  = { BTN_MINUS_X, EDIT_Y, EDIT_BTN_W, EDIT_H };
static const UiRect BTN_PLUS   = { BTN_PLUS_X,  EDIT_Y, EDIT_BTN_W, EDIT_H };
/** @brief The OK button on the write-result screen. */
static const UiRect BTN_OK     = { 70, 200, 100, 36 };

// Layout invariants, asserted rather than eyeballed. A caption that runs under
// a button is invisible in code review and obvious only in a screenshot.
static_assert(LIST_Y0 >= UI_BODY_Y, "the list overlaps the header strip");
static_assert(LIST_Y1 < EDIT_Y, "list overlaps the editor bar");
static_assert(EDIT_Y + EDIT_H <= FOOT_Y, "editor bar overlaps the footer");
static_assert(FOOT_Y + FOOT_H <= GFX_H, "footer runs off the panel");
static_assert(BTN_MINUS_X + EDIT_BTN_W < BTN_PLUS_X, "editor buttons overlap");
static_assert(EDIT_H >= UI_TAP_MIN && FOOT_H >= UI_TAP_MIN,
              "an AM32 button is smaller than a fingertip");

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
/**
 * @brief How far the list is scrolled, in pixels rather than in rows.
 *
 * Rows were the wrong unit and it showed in the one way that matters on glass:
 * with the axis lock at 10 px and a step of a whole 26 px row, the list did not
 * move at all until the finger had travelled 36 px, so a scroll felt like it
 * arrived late and then jumped. Pixels let the content track the finger from
 * the moment the gesture is recognised. @see handleListGesture()
 */
static int        s_scrollPx = 0;
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

/**
 * @brief Held-button repeat state for the editor bar.
 *
 * The rule itself lives in ui_input.h now, shared with the settings screen,
 * which had no repeat at all until it was factored out -- getting the throttle
 * ceiling from 20 % to 100 % was eight separate taps.
 */
static Repeat s_editRepeat;

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

// hit(), button() and pressingBtn() used to live here. They were a third copy
// of the same three functions and are uiPressing() and uiButton() now.
// @see ui_widgets.h

/** @brief Touch state for the frame being drawn. Set at the top of uiAm32Tick(). */
static const TouchState *s_frameTouch = nullptr;

/** @brief Total height of the list content, in pixels, for the current view. */
static int contentHeight() {
	return s_hexView ? ((AM32_SETTINGS_SIZE + 7) / 8) * HEX_ROW_H
	                 : (int)s_visibleCount * ROW_H;
}

/** @brief Largest legal @ref s_scrollPx, which is 0 when everything fits. */
static int maxScrollPx() {
	int over = contentHeight() - LIST_H;
	return over > 0 ? over : 0;
}

/** @brief Clamp @ref s_scrollPx into range. Call after any change to either. */
static void clampScroll() {
	int max = maxScrollPx();
	if (s_scrollPx > max) s_scrollPx = max;
	if (s_scrollPx < 0)   s_scrollPx = 0;
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------
static void drawHeader() {
	uiHeader("AM32 CONFIG", s_frameTouch);

	// The device name qualifies the title rather than being part of it, so it
	// goes on the shared strip -- the same row SETTINGS puts its EDT chip on.
	// It used to be a second line inside a 50 px header this screen had to
	// itself, which is why BACK ended up 8 px lower here than on SETUP.
	uiStripClear();
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
		uiStripText(buf, C_DIM);
	} else if (s_status[0]) {
		uiStripText(s_status, C_DIM);
	}
}

static void drawConnect() {
	gfxRect(0, LIST_Y0, GFX_W, GFX_H - LIST_Y0, C_BG);

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

/**
 * @brief Draw one settings row with its top edge at @p y.
 *
 * Takes a pixel row rather than a slot index, because with a pixel scroll
 * offset the first and last rows drawn are usually partly outside the viewport.
 * The clip box set by drawList() is what keeps them there.
 *
 * @param y        Top of the row, which may be above LIST_Y0 or below LIST_Y1.
 * @param fieldIdx Index into AM32_FIELDS.
 */
static void drawRow(int y, uint16_t fieldIdx) {
	const Am32Field *f = &AM32_FIELDS[fieldIdx];
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

/** @brief The hex dump, scrolled by the same pixel offset as the field list. */
static void drawHex() {
	// 8 bytes per line keeps each row inside 240 px at scale 1.
	int first = s_scrollPx / HEX_ROW_H;
	int off   = s_scrollPx % HEX_ROW_H;
	for (int r = 0;; r++) {
		int y = LIST_Y0 - off + r * HEX_ROW_H;
		if (y > LIST_Y1) break;
		int base = (first + r) * 8;
		if (base >= AM32_SETTINGS_SIZE) break;
		char line[64];
		int n = snprintf(line, sizeof(line), "%02X ", base);
		for (int i = 0; i < 8 && base + i < AM32_SETTINGS_SIZE; i++) {
			n += snprintf(line + n, sizeof(line) - n, "%02X ", s_eeprom[base + i]);
		}
		gfxText(6, y, line, C_DIM, 1);
	}
}

/** @brief The field list, drawn from @ref s_scrollPx. */
static void drawFields() {
	int first = s_scrollPx / ROW_H;
	int off   = s_scrollPx % ROW_H;
	for (int r = 0;; r++) {
		int y = LIST_Y0 - off + r * ROW_H;
		if (y > LIST_Y1) break;
		int idx = first + r;
		if (idx >= (int)s_visibleCount) break;
		drawRow(y, s_visible[idx]);
	}
}

static void drawList() {
	// The clip is what makes a pixel scroll possible at all: the top and bottom
	// rows are usually partial, and without it the overhang lands on the header
	// strip above and the editor bar below. @see gfx_clip
	gfxSetClip(0, LIST_Y0, GFX_W, LIST_H);
	gfxRect(0, LIST_Y0, GFX_W, LIST_H, C_BG);
	if (s_hexView) drawHex();
	else           drawFields();
	gfxClearClip();

	gfxRect(0, LIST_Y1 + 1, GFX_W, GFX_H - LIST_Y1 - 1, C_BG);

	// ---- editor bar: big -/+ acting on the selected row ----
	const Am32Field *sel = (s_selected >= 0 && s_selected < (int)AM32_FIELD_COUNT)
	                           ? &AM32_FIELDS[s_selected] : nullptr;

	if (sel && !s_hexView) {
		uiButton(BTN_MINUS, "-", C_GRID, C_ONACCENT, 1,
		         uiPressing(BTN_MINUS, s_frameTouch));
		uiButton(BTN_PLUS,  "+", C_GRID, C_ONACCENT, 1,
		         uiPressing(BTN_PLUS, s_frameTouch));
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
		// Short enough to clear both buttons: the gap between them is only
		// (BTN_PLUS_X - cx) px, and a longer caption ran under them.
		const char *hint = "SWIPE = COARSE";
		gfxText(cx + (cw - gfxTextW(hint, 1)) / 2, EDIT_Y + 33, hint, C_GRID, 1);
	} else if (!s_hexView) {
		gfxTextCenter(EDIT_Y + 12, "TAP A SETTING TO EDIT", C_GRID, 1);
		gfxTextCenter(EDIT_Y + 26, "SWIPE IT SIDEWAYS FOR COARSE", C_GRID, 1);
	}

	// ---- footer ----
	bool dirty = anyDirty();
	// C_RED while holding rather than C_REDDARK: that colour's other job is the
	// dead-telemetry digit, which has to read as "off" and therefore inverts
	// direction between the two palettes. A fill that carries white text needs
	// to be dark in both, and C_RED is.
	uint16_t writeFill = s_writeHolding ? C_RED : C_PANEL;
	if (s_writeHolding) {
		uint32_t held = millis() - s_writeHoldStart;
		int pct = (int)(held * 100 / WRITE_HOLD_MS);
		if (pct > 100) pct = 100;
		gfxRect(6, FOOT_Y - 4, 110 * pct / 100, 3, C_LIME);
	}
	uiButton(BTN_WRITE, dirty ? "HOLD TO WRITE" : "NO CHANGES",
	         dirty ? writeFill : C_PANEL,
	         dirty ? (s_writeHolding ? C_ONACCENT : C_TEXT) : C_GRID, 1,
	         s_writeHolding);
	uiButton(BTN_REVERT, "REVERT", C_PANEL, dirty ? C_AMBER : C_GRID, 1,
	         uiPressing(BTN_REVERT, s_frameTouch));
	uiButton(BTN_HEX, s_hexView ? "FIELDS" : "HEX", C_PANEL, C_CYAN, 1,
	         uiPressing(BTN_HEX, s_frameTouch));
}

static void drawWriting() {
	gfxRect(0, LIST_Y0, GFX_W, GFX_H - LIST_Y0, C_BG);
	gfxTextCenter(140, s_status, C_TEXT, 2);
	uiButton(BTN_OK, "OK", C_PANEL, C_TEXT, 1, uiPressing(BTN_OK, s_frameTouch));
}

// ---------------------------------------------------------------------------
// state machine
// ---------------------------------------------------------------------------
void uiAm32Enter() {
	s_state = S_HANDOVER;
	s_hexView = false;
	s_scrollPx = 0;
	s_selected = -1;
	s_leaving = false;
	s_redraw = true;
	s_writeHolding = false;
	s_visibleCount = 0;
#if AM32_FORCE_LOW_JUMP
	s_jumped = false;
#endif
	s_attempt = 0;
	s_editRepeat = Repeat{};
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
	s_scrollPx = 0;
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


static void handleListTouch(const TouchState *t) {
	// --- write button: needs every frame, including finger-off, to time the
	//     hold and to cancel it the moment the finger moves away. Press-origin
	//     gated (uiPressing), so a drag that strays in from the list cannot
	//     start a hold it never announced -- the drawn pressed state already
	//     required the press to begin on the button, and firing has to agree
	//     with feedback ---
	bool dirty = anyDirty();
	if (dirty && uiPressing(BTN_WRITE, t)) {
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

	// --- editor bar: also needs every frame, to drive hold-to-repeat.
	//     Same press-origin rule: a swipe on the bottom list row that slid
	//     less than one row height used to keep the selection *and* step it
	//     the moment the finger crossed into the bar ---
	const Am32Field *sel = (!s_hexView && s_selected >= 0 &&
	                        s_selected < (int)AM32_FIELD_COUNT)
	                           ? &AM32_FIELDS[s_selected] : nullptr;
	bool onMinus = uiPressing(BTN_MINUS, t);
	bool onPlus  = uiPressing(BTN_PLUS, t);

	if (sel && (onMinus || onPlus)) {
		int dir = onMinus ? -1 : +1;
		if (repeatFires(&s_editRepeat, dir, millis())) {
			am32Adjust(sel, s_eeprom, dir);
			s_redraw = true;
		}
		return;
	}
	if (!t->down) repeatFires(&s_editRepeat, 0, millis());

	// Row selection is *not* here any more. It used to fire on touch-down, and
	// that is what made the list feel like it fought back: every scroll began
	// by selecting whatever the finger happened to land on, repainting the row,
	// and only then -- 36 px of travel later -- starting to move. The press now
	// records a candidate and commits nothing; handleListGesture() decides
	// whether the gesture turned out to be a tap or a drag.

	// --- destructive taps fire on release, inside, having started inside, so
	//     a mis-tap can slide off. REVERT throws away every unsaved edit; it
	//     used to do so on touch-down ---
	if (uiTapped(BTN_REVERT, t)) {
		memcpy(s_eeprom, s_original, sizeof(s_eeprom));
		s_redraw = true;
	} else if (uiTapped(BTN_HEX, t)) {
		s_hexView = !s_hexView;
		s_scrollPx = 0;
		s_redraw = true;
	}
}

/**
 * @brief List gestures: vertical drag scrolls, horizontal drag adjusts, a
 *        press that goes nowhere selects.
 *
 * The two axes are locked exclusively on first movement, so a swipe is either
 * a scroll or an edit and never both. Horizontal gives coarse control -- one
 * full-width swipe covers a field's entire range -- while the editor-bar
 * buttons stay one step per press for fine work.
 *
 * The important change here is what a press means. It used to select the row
 * under the finger immediately, and scrolling was something the gesture became
 * afterwards; the row lit up, and then 36 px later the list started moving in
 * whole-row jumps. That is backwards for a list, where dragging is the common
 * gesture and tapping the rare one. So a press now records a *candidate* row
 * and commits nothing:
 *
 * - it becomes a selection on release, if the finger never travelled;
 * - it becomes a selection at the moment a horizontal lock happens, so that
 *   press-and-swipe still adjusts the row actually touched;
 * - and a vertical drag never selects anything at all.
 *
 * Scrolling then follows the finger 1:1 in pixels from the press point, so no
 * travel is lost to the axis lock and nothing jumps a row at a time.
 */
static void handleListGesture(const TouchState *t) {
	/**
	 * @brief Travel before an axis is committed to, in pixels.
	 *
	 * Lower than it was. It used to be 10 px on top of a 26 px row step; now it
	 * is only the slop that separates a tap from a drag, and every pixel of it
	 * is a pixel the list has not moved yet.
	 */
	static const int AXIS_LOCK_PX = 6;
	/** @brief Horizontal travel that spans a field's whole range. */
	static const int SWIPE_FULL_PX = 180;

	enum GMode : uint8_t { G_NONE, G_VERT, G_HORIZ };
	static GMode   mode = G_NONE;
	static int16_t startX = 0, startY = 0;
	static int16_t anchorX = 0;
	static uint8_t anchorRaw = 0;
	static int     scrollAtPress = 0;
	/** @brief Field index under the finger when it landed; -1 for none. */
	static int     candidate = -1;

	const Am32Field *sel = (!s_hexView && s_selected >= 0 &&
	                        s_selected < (int)AM32_FIELD_COUNT)
	                           ? &AM32_FIELDS[s_selected] : nullptr;

	if (t->pressed) {
		mode = G_NONE;
		startX = anchorX = t->x;
		startY = t->y;
		scrollAtPress = s_scrollPx;
		candidate = -1;
		if (!s_hexView && t->y >= LIST_Y0 && t->y <= LIST_Y1) {
			int idx = (s_scrollPx + (t->y - LIST_Y0)) / ROW_H;
			if (idx >= 0 && idx < (int)s_visibleCount)
				candidate = (int)s_visible[idx];
		}
		if (sel) anchorRaw = s_eeprom[sel->offset];
		return;
	}

	if (t->released) {
		// A press that never became a drag is a tap, and a tap selects. Doing it
		// here rather than on touch-down is what makes the list scroll first and
		// ask questions later.
		if (mode == G_NONE && candidate >= 0 &&
		    t->y >= LIST_Y0 && t->y <= LIST_Y1) {
			s_selected = candidate;
			s_redraw = true;
		}
		mode = G_NONE;
		candidate = -1;
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
		if (mode == G_HORIZ) {
			// A coarse swipe adjusts the row the finger landed on, so the
			// selection it never made on touch-down is made now -- before the
			// anchor is taken, or the first swipe would edit the previous
			// selection's value into the newly selected field.
			if (candidate >= 0 && candidate != s_selected) {
				s_selected = candidate;
				s_redraw = true;
			}
			sel = (s_selected >= 0 && s_selected < (int)AM32_FIELD_COUNT)
			          ? &AM32_FIELDS[s_selected] : nullptr;
			// Re-base at the moment of commitment so the value does not jump by
			// the distance already travelled.
			anchorX = t->x;
			if (sel) anchorRaw = s_eeprom[sel->offset];
		}
	}

	if (mode == G_VERT) {
		// Measured from the press, not from the lock: the few pixels spent
		// deciding this was a scroll are pixels the user dragged, and swallowing
		// them is exactly the lag this rework is about. Dragging down moves the
		// content down, which means the offset goes the other way.
		int want = scrollAtPress - dy;
		if (want != s_scrollPx) {
			s_scrollPx = want;
			clampScroll();
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
	s_frameTouch = t;
	// A pressed button has to repaint while it is held -- and once more on the
	// release frame, or the pressed look outlives the finger -- and this
	// screen only repaints when something asks it to.
	if (t && (t->down || t->released)) s_redraw = true;

	// BACK is drawn by drawHeader() in every state, so it has to be handled in
	// every state. Doing it here rather than per-state stops it being a button
	// that is visible but inert on whichever screen forgot to check for it.
	// On release, like every navigation tap.
	if (uiBackTapped(t)) {
		s_leaving = true;
		uiAm32Exit();
		return false;
	}

	switch (s_state) {
		case S_HANDOVER:
			if (escTaskSuspended()) {
				// Whichever pin the pump was driving, not the compiled default:
				// the two part company the moment the user changes it on SETUP.
				am32BlBegin(escTaskDshotPin());
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
			} else if (uiTapped(BTN_OK, t)) {
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
