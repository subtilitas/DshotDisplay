/**
 * @file ui_setup.cpp
 * @brief SETUP screen: wiring, display, and the hold-to-save that persists it.
 *
 * Six rows and two buttons. The interesting parts are not the widgets:
 *
 * - **Only legal values can be selected.** The `-`/`+` buttons step through the
 *   board's free-GPIO mask, so there is no invalid pin to reject and no error
 *   state to design. @see settingsNextPin()
 * - **LINK is on this screen.** Changing the ESC pin is otherwise unverifiable
 *   without walking back to the main screen, and "the ESC is silent" is exactly
 *   the failure this screen exists to fix.
 * - **Changes apply live; only persistence needs the hold.** Turn the bitrate
 *   down and the pump rebuilds this frame. The one-second hold is about writing
 *   flash, which parks core1, not about the value being dangerous.
 */

#include "ui_setup.h"
#include "settings.h"
#include "esc_task.h"
#include "ui.h"
#include "gfx.h"
#include "st7789.h"
#include "touch.h"
#include "config.h"
#include "board_pins.h"

#include "plat.h"
#include <stdio.h>
#include <string.h>

/**
 * @defgroup setup_layout Layout (240x320 portrait)
 * @brief Asserted rather than eyeballed, as everywhere else in this UI.
 * @{
 */
#define HDR_H       30   /**< Header band height. */
#define BACK_X      (GFX_W - 54)
#define BACK_Y      5
#define BACK_W      48
#define BACK_H      22

#define SEC1_Y      34   /**< "WIRING" caption. */
#define ROW_H       26   /**< Height of a row's controls. */
#define ROW_PITCH   30   /**< Row top to row top. */
#define R_PIN       44   /**< ESC pin. */
#define R_SPEED     (R_PIN + ROW_PITCH)
#define R_KISS      (R_PIN + 2 * ROW_PITCH)
#define R_KISSPIN   (R_PIN + 3 * ROW_PITCH)

#define SEC2_Y      168  /**< "DISPLAY" caption. */
#define R_CONTRAST  178
#define R_BACKLIGHT (R_CONTRAST + ROW_PITCH)

#define LINK_Y      240  /**< Live link readout. */
#define NOTE_Y      252  /**< One line saying what state the settings are in. */

#define FOOT_Y      266
#define FOOT_H      40
#define SAVE_X      8
#define SAVE_W      150
#define DEF_X       (SAVE_X + SAVE_W + 4)
#define DEF_W       (GFX_W - 8 - DEF_X)

/** Value column: right-aligned text ends here, buttons start after it. */
#define VAL_R       140
#define BTN_M_X     148
#define BTN_P_X     192
#define BTN_W       40
#define TOGGLE_W    (BTN_P_X + BTN_W - BTN_M_X)
/** @} */

static_assert(SEC1_Y >= HDR_H, "WIRING caption overlaps the header");
static_assert(R_PIN >= SEC1_Y + 8, "first row overlaps the WIRING caption");
static_assert(SEC2_Y >= R_KISSPIN + ROW_H, "DISPLAY caption overlaps the KISS row");
static_assert(R_CONTRAST >= SEC2_Y + 8, "contrast row overlaps the DISPLAY caption");
static_assert(LINK_Y >= R_BACKLIGHT + ROW_H, "LINK overlaps the backlight row");
static_assert(NOTE_Y >= LINK_Y + 7, "note overlaps LINK");
static_assert(FOOT_Y >= NOTE_Y + 7, "footer overlaps the note");
static_assert(FOOT_Y + FOOT_H <= GFX_H, "footer runs off the panel");
static_assert(BTN_M_X + BTN_W < BTN_P_X, "row buttons overlap");
static_assert(BTN_P_X + BTN_W <= GFX_W - 8, "row buttons run off the panel");
static_assert(DEF_X + DEF_W <= GFX_W - 8 + 1, "DEFAULTS runs off the panel");

/** @brief Hold this long to commit the settings to flash. */
#define SAVE_HOLD_MS 1000

/** @brief Backlight adjustment step. 8 levels across the usable range. */
#define BACKLIGHT_STEP 32

static bool     s_leaving = false;
static bool     s_redraw = true;
static bool     s_saveHolding = false;
static uint32_t s_saveHoldStart = 0;
/**
 * @brief Set once a hold has fired; cleared only when the finger lifts.
 *
 * Without it, keeping the button pressed past one second immediately starts a
 * fresh hold and saves again a second later, for as long as the finger stays
 * down. Each of those is a flash erase.
 */
static bool     s_saveLatched = false;

/** @brief What the last save attempt did. Drives the note line. */
enum class SaveOutcome : uint8_t { None, Ok, Failed };
static SaveOutcome s_saveOutcome = SaveOutcome::None;

/** @brief True when validation last had to repair something the user asked for. */
static bool s_repaired = false;

/** @brief Cached LINK values, so the live row repaints only when it changes. */
static int s_shownRate = -1, s_shownErr = -1;

static bool hit(int x, int y, int bx, int by, int bw, int bh) {
	return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

static void button(int x, int y, int w, int h, const char *label,
                   uint16_t fill, uint16_t fg, int scale) {
	gfxRoundRect(x, y, w, h, 5, fill);
	gfxRoundFrame(x, y, w, h, 5, C_GRID);
	gfxText(x + (w - gfxTextW(label, scale)) / 2, y + (h - 7 * scale) / 2,
	        label, fg, scale);
}

/**
 * @brief Draw one label / value row with a `-` and `+` pair.
 *
 * @param y     Row top.
 * @param label Left-hand caption.
 * @param value Right-aligned value.
 * @param vcol  Value colour.
 */
static void drawStepRow(int y, const char *label, const char *value, uint16_t vcol) {
	gfxRect(0, y, GFX_W, ROW_H, C_BG);
	gfxText(8, y + 9, label, C_DIM, 1);
	gfxText(VAL_R - gfxTextW(value, 2), y + 6, value, vcol, 2);
	button(BTN_M_X, y, BTN_W, ROW_H, "-", C_PANEL, C_TEXT, 2);
	button(BTN_P_X, y, BTN_W, ROW_H, "+", C_PANEL, C_TEXT, 2);
}

/**
 * @brief Draw one label / value row whose control is a single wide toggle.
 *
 * @param y     Row top.
 * @param label Left-hand caption.
 * @param value Current state, also the toggle's label.
 * @param on    Whether the state is the "active" one.
 */
static void drawToggleRow(int y, const char *label, const char *value, bool on) {
	gfxRect(0, y, GFX_W, ROW_H, C_BG);
	gfxText(8, y + 9, label, C_DIM, 1);
	button(BTN_M_X, y, TOGGLE_W, ROW_H, value,
	       on ? C_BLUE : C_PANEL, on ? C_ONACCENT : C_DIM, 1);
}

/**
 * @brief The note line: the single most useful thing to say right now.
 *
 * Ordered by urgency rather than by category. A failed save outranks
 * everything; a setting the firmware had to repair outranks the ordinary
 * saved/unsaved distinction, because the user asked for something and did not
 * get it and nothing else on the screen says so.
 *
 * @param[out] col Colour to draw it in.
 * @return The text.
 */
static const char *noteText(uint16_t *col) {
	if (s_saveOutcome == SaveOutcome::Failed) {
		*col = C_RED;
		return "SAVE FAILED - FLASH REFUSED THE WRITE";
	}
	if (s_repaired) {
		*col = C_AMBER;
		return "KISS OFF: NEEDS ITS OWN UART RX PIN";
	}
	if (settingsDirty()) {
		*col = C_AMBER;
		return "UNSAVED - HOLD SAVE TO KEEP";
	}
	if (s_saveOutcome == SaveOutcome::Ok) {
		*col = C_LIME;
		return "SAVED";
	}
	*col = C_DIM;
	return settingsStored() ? "SAVED SETTINGS IN USE"
	                        : "COMPILED DEFAULTS IN USE";
}

/** @brief Repaint the live LINK row if its numbers moved. */
static void drawLink(bool force) {
	EscTelemetry t;
	escSnapshot(&t);
	if (!force && s_shownRate == (int)t.packetRate && s_shownErr == (int)t.errPercent)
		return;
	s_shownRate = t.packetRate;
	s_shownErr  = t.errPercent;

	gfxRect(0, LINK_Y, GFX_W, 8, C_BG);
	gfxText(8, LINK_Y, "LINK", C_DIM, 1);

	char buf[32];
	snprintf(buf, sizeof(buf), "%d/S  %d%% ERR", (int)t.packetRate, (int)t.errPercent);
	// Green as soon as anything is arriving. That is the whole assertion this
	// row makes: frames are coming back, so the pin is right.
	uint16_t col = t.packetRate == 0 ? C_DIM
	             : t.errPercent > 5  ? C_AMBER
	                                 : C_LIME;
	gfxText(GFX_W - 8 - gfxTextW(buf, 1), LINK_Y, buf, col, 1);
}

static void drawAll() {
	const Settings *s = settings();
	char buf[24];

	gfxFill(C_BG);
	gfxRect(0, 0, GFX_W, HDR_H, C_PANEL);
	gfxText(6, 8, "SETUP", C_TEXT, 2);
	button(BACK_X, BACK_Y, BACK_W, BACK_H, "BACK", C_PANEL, C_TEXT, 1);

	gfxText(8, SEC1_Y, "WIRING", C_GRID, 1);

	snprintf(buf, sizeof(buf), "GP%u", (unsigned)s->dshotPin);
	drawStepRow(R_PIN, "ESC PIN", buf, C_LIME);

	snprintf(buf, sizeof(buf), "%u", (unsigned)s->dshotKbaud);
	drawStepRow(R_SPEED, "DSHOT KBAUD", buf, C_TEXT);

	drawToggleRow(R_KISS, "KISS TELEM", s->kissEnable ? "ON" : "OFF", s->kissEnable);

	if (s->kissEnable) snprintf(buf, sizeof(buf), "GP%u", (unsigned)s->kissPin);
	else               snprintf(buf, sizeof(buf), "--");
	drawStepRow(R_KISSPIN, "KISS PIN", buf,
	            s->kissEnable ? C_CYAN : C_DIM);

	gfxText(8, SEC2_Y, "DISPLAY", C_GRID, 1);

	drawToggleRow(R_CONTRAST, "CONTRAST",
	              s->highContrast ? "HIGH" : "NORMAL", s->highContrast != 0);

	snprintf(buf, sizeof(buf), "%u", (unsigned)s->backlight);
	drawStepRow(R_BACKLIGHT, "BACKLIGHT", buf, C_TEXT);

	drawLink(true);

	uint16_t noteCol;
	const char *note = noteText(&noteCol);
	gfxRect(0, NOTE_Y, GFX_W, 8, C_BG);
	gfxText(8, NOTE_Y, note, noteCol, 1);

	// Progress fills across the button itself rather than being a sliver
	// somewhere else, so the thing you are pressing is the thing that reports.
	uint16_t saveFill = C_PANEL;
	if (s_saveHolding) {
		uint32_t held = millis() - s_saveHoldStart;
		int pct = (int)(held * 100 / SAVE_HOLD_MS);
		if (pct > 100) pct = 100;
		saveFill = C_BLUE;
		gfxRect(SAVE_X, FOOT_Y - 4, SAVE_W * pct / 100, 3, C_LIME);
	}
	button(SAVE_X, FOOT_Y, SAVE_W, FOOT_H, "HOLD TO SAVE", saveFill,
	       s_saveHolding ? C_ONACCENT : C_TEXT, 1);
	button(DEF_X, FOOT_Y, DEF_W, FOOT_H, "RESET", C_PANEL, C_AMBER, 1);
}

/**
 * @brief Push the current wiring to core1 and re-apply the display settings.
 *
 * Called after every edit. Each of the three is a no-op when nothing changed,
 * so this can be unconditional rather than tracking which field moved — which
 * is the kind of bookkeeping that goes wrong exactly once and then behaves like
 * a hardware fault.
 */
static void applyLive() {
	Settings *s = settings();
	s_repaired = !settingsValidate(s);

	escTaskConfigure(s->dshotPin, s->dshotKbaud, s->kissEnable != 0, s->kissPin);
	themeSet(s->highContrast ? Theme::HighContrast : Theme::Dark);
	st7789SetBacklight(themeBacklight(s->backlight));
}

void uiSetupEnter() {
	s_leaving = false;
	s_redraw = true;
	s_saveHolding = false;
	s_saveLatched = false;
	s_saveOutcome = SaveOutcome::None;
	s_shownRate = -1;
	s_shownErr = -1;
	applyLive();
}

bool uiSetupTick(const TouchState *t) {
	Settings *s = settings();

	// --- hold to save ---
	//
	// Evaluated every frame including finger-off, so moving away from the button
	// cancels rather than commits. Deliberately not an early return: the hold's
	// progress bar is drawn by the block at the bottom of this function, so
	// returning here would make the one control that needs continuous feedback
	// the only one that never repaints.
	bool onSave = t->down && hit(t->x, t->y, SAVE_X, FOOT_Y, SAVE_W, FOOT_H);
	if (!t->down) s_saveLatched = false;
	if (onSave && !s_saveLatched) {
		if (!s_saveHolding) { s_saveHolding = true; s_saveHoldStart = millis(); }
		if (millis() - s_saveHoldStart >= SAVE_HOLD_MS) {
			s_saveHolding = false;
			s_saveLatched = true;
			// Refused while armed on purpose. Erasing flash parks core1 for tens
			// of milliseconds, which stops the DShot pump; the ESC would time out
			// and cut a spinning motor. Unreachable today -- CFG force-disarms
			// and this screen is behind it -- and checked anyway, because the day
			// that stops being true is not a day anyone will remember this.
			s_saveOutcome = (!uiArmed() && settingsSave())
			                    ? SaveOutcome::Ok : SaveOutcome::Failed;
		}
		s_redraw = true;
	} else if (s_saveHolding) {
		s_saveHolding = false;
		s_redraw = true;
	}

	if (t->pressed && !onSave) {
		int x = t->x, y = t->y;
		bool changed = true;

		if (hit(x, y, BACK_X, BACK_Y, BACK_W, BACK_H)) {
			s_leaving = true;
			return false;
		} else if (hit(x, y, BTN_M_X, R_PIN, BTN_W, ROW_H)) {
			s->dshotPin = settingsNextPin(s->dshotPin, -1, false);
		} else if (hit(x, y, BTN_P_X, R_PIN, BTN_W, ROW_H)) {
			s->dshotPin = settingsNextPin(s->dshotPin, +1, false);
		} else if (hit(x, y, BTN_M_X, R_SPEED, BTN_W, ROW_H)) {
			s->dshotKbaud = (uint16_t)(s->dshotKbaud <= 150 ? 1200 : s->dshotKbaud / 2);
		} else if (hit(x, y, BTN_P_X, R_SPEED, BTN_W, ROW_H)) {
			s->dshotKbaud = (uint16_t)(s->dshotKbaud >= 1200 ? 150 : s->dshotKbaud * 2);
		} else if (hit(x, y, BTN_M_X, R_KISS, TOGGLE_W, ROW_H)) {
			// Turning KISS on picks the first pin that can actually receive,
			// rather than enabling it against whatever was last stored and
			// letting validation switch it straight back off.
			if (s->kissEnable) {
				s->kissEnable = 0;
			} else {
				s->kissEnable = 1;
				if (settingsUartForPin(s->kissPin) < 0 || s->kissPin == s->dshotPin)
					s->kissPin = settingsNextPin(s->kissPin, +1, true);
			}
		} else if (hit(x, y, BTN_M_X, R_KISSPIN, BTN_W, ROW_H)) {
			s->kissPin = settingsNextPin(s->kissPin, -1, true);
		} else if (hit(x, y, BTN_P_X, R_KISSPIN, BTN_W, ROW_H)) {
			s->kissPin = settingsNextPin(s->kissPin, +1, true);
		} else if (hit(x, y, BTN_M_X, R_CONTRAST, TOGGLE_W, ROW_H)) {
			s->highContrast = s->highContrast ? 0 : 1;
		} else if (hit(x, y, BTN_M_X, R_BACKLIGHT, BTN_W, ROW_H)) {
			s->backlight = (uint8_t)(s->backlight <= BACKLIGHT_STEP
			                             ? 16 : s->backlight - BACKLIGHT_STEP);
		} else if (hit(x, y, BTN_P_X, R_BACKLIGHT, BTN_W, ROW_H)) {
			int v = s->backlight + BACKLIGHT_STEP;
			s->backlight = (uint8_t)(v > 255 ? 255 : v);
		} else if (hit(x, y, DEF_X, FOOT_Y, DEF_W, FOOT_H)) {
			// Restores the compiled defaults into the live settings only. Flash
			// is untouched until SAVE, so this is undoable by walking away.
			settingsDefaults(s);
			s_saveOutcome = SaveOutcome::None;
		} else {
			changed = false;
		}

		if (changed) {
			applyLive();
			s_saveOutcome = (s_saveOutcome == SaveOutcome::Ok)
			                    ? SaveOutcome::None : s_saveOutcome;
			s_redraw = true;
		}
	}

	if (s_redraw) {
		s_redraw = false;
		drawAll();
	} else {
		drawLink(false);
	}

	return !s_leaving;
}
