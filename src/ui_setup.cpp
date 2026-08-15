/**
 * @file ui_setup.cpp
 * @brief SETUP screen: wiring, display, and the hold-to-save that persists it.
 *
 * Six rows and two buttons. The interesting parts are not the widgets:
 *
 * - **Only legal values can be selected.** The `-`/`+` buttons step through the
 *   board's free-GPIO mask and past whichever pin the other wire is on, so
 *   there is no invalid pin to reject and no error state to design.
 *   @see stepPinAvoiding()
 * - **LINK is on this screen.** Changing the ESC pin is otherwise unverifiable
 *   without walking back to the main screen, and "the ESC is silent" is exactly
 *   the failure this screen exists to fix.
 * - **Changes apply live; only persistence needs the hold.** Turn the bitrate
 *   down and the pump rebuilds this frame. The one-second hold is about writing
 *   flash, which parks core1, not about the value being dangerous.
 * - **The board is not one of them.** It is detected at boot and shown here
 *   read-only. It used to be a picker, and a picker over hardware is a way to
 *   be wrong about hardware: choosing the other board and saving built the
 *   next boot's display from the wrong pins, and the screen that could have
 *   put it back was the screen that no longer came up. @see boardProbe()
 *
 * Input follows the three rules in ui_input.h: taps fire on release (so a
 * mis-tap can slide off), a pressed control looks pressed, and the `-`/`+`
 * steppers repeat while held. SAVE stays a deliberate one-second hold and
 * only counts a hold that *began* on the button.
 */

#include "ui_setup.h"
#include "settings.h"
#include "esc_task.h"
#include "ui.h"
#include "ui_input.h"
#include "gfx.h"
#include "st7789.h"
#include "touch.h"
#include "config.h"
#include "board_desc.h"

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

/*
 * One layout, always seven rows, with no section captions.
 *
 * The board row is read-only on every image -- there is nothing to choose --
 * and it stays because "which board does this firmware think it is" is worth a
 * line of its own. Dropping it would buy one row of height and cost the only
 * on-screen answer to the first question a misbehaving panel raises. Keeping
 * the geometry fixed also keeps the layout asserts compile-time constants,
 * which are the only thing standing between this screen and a caption drawn
 * through a button.
 *
 * The captions went to pay for the row. Seven rows plus two captions do not fit
 * above LINK, and of the two, the rows carry the information.
 */
#define ROW_H       24   /**< Height of a row's controls. */
#define ROW_PITCH   26   /**< Row top to row top. */
#define R_BOARD     36   /**< Which board this is. */
#define R_PIN       (R_BOARD + 1 * ROW_PITCH)  /**< ESC pin. */
#define R_SPEED     (R_BOARD + 2 * ROW_PITCH)
#define R_KISS      (R_BOARD + 3 * ROW_PITCH)
#define R_KISSPIN   (R_BOARD + 4 * ROW_PITCH)
#define R_CONTRAST  (R_BOARD + 5 * ROW_PITCH)
#define R_BACKLIGHT (R_BOARD + 6 * ROW_PITCH)

#define LINK_Y      224  /**< Live link readout. */
#define NOTE_Y      236  /**< One line saying what state the settings are in. */

#define FOOT_Y      254
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

static_assert(R_BOARD >= HDR_H, "the first row overlaps the header");
static_assert(LINK_Y >= R_BACKLIGHT + ROW_H, "LINK overlaps the last row");
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
enum class SaveOutcome : uint8_t { None, Ok, Failed, FailedArmed };
static SaveOutcome s_saveOutcome = SaveOutcome::None;

/** @brief True when validation last had to repair something the user asked for. */
static bool s_repaired = false;
/** @brief True when that repair was specifically KISS being switched off. */
static bool s_repairKiss = false;

/** @brief Cached LINK values, so the live row repaints only when it changes. */
static int s_shownRate = -1, s_shownErr = -1;

/** @brief Held-repeat state, one per stepper pair. @see ui_input.h */
static Repeat s_pinRep, s_speedRep, s_kissPinRep, s_backlightRep;

/**
 * @brief The touch snapshot drawAll() paints pressed states from.
 *
 * Copied at the top of every tick; zeroed on entry so the first frame after
 * uiSetupEnter() draws everything at rest.
 */
static TouchState s_touchSnap;

static void button(int x, int y, int w, int h, const char *label,
                   uint16_t fill, uint16_t fg, int scale, bool pressed = false) {
	gfxRoundRect(x, y, w, h, 5, fill);
	gfxRoundFrame(x, y, w, h, 5, pressed ? C_INK : C_GRID);
	if (pressed && w > 6 && h > 6)
		gfxRoundFrame(x + 2, y + 2, w - 4, h - 4, 4, C_INK);
	int dx = pressed ? 1 : 0;
	gfxText(x + dx + (w - gfxTextW(label, scale)) / 2,
	        y + dx + (h - 7 * scale) / 2, label, fg, scale);
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
	gfxText(8, y + 8, label, C_DIM, 1);
	gfxText(VAL_R - gfxTextW(value, 2), y + 5, value, vcol, 2);
	button(BTN_M_X, y, BTN_W, ROW_H, "-", C_PANEL, C_TEXT, 2,
	       inputPressing(&s_touchSnap, BTN_M_X, y, BTN_W, ROW_H));
	button(BTN_P_X, y, BTN_W, ROW_H, "+", C_PANEL, C_TEXT, 2,
	       inputPressing(&s_touchSnap, BTN_P_X, y, BTN_W, ROW_H));
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
	gfxText(8, y + 8, label, C_DIM, 1);
	button(BTN_M_X, y, TOGGLE_W, ROW_H, value,
	       on ? C_BLUE : C_PANEL, on ? C_ONACCENT : C_DIM, 1,
	       inputPressing(&s_touchSnap, BTN_M_X, y, TOGGLE_W, ROW_H));
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
	if (s_saveOutcome == SaveOutcome::FailedArmed) {
		*col = C_RED;
		return "NOT WHILE ARMED - DISARM TO SAVE";
	}
	if (s_saveOutcome == SaveOutcome::Failed) {
		*col = C_RED;
		return "SAVE FAILED - FLASH REFUSED THE WRITE";
	}
	if (s_repaired) {
		*col = C_AMBER;
		return s_repairKiss ? "KISS OFF: NEEDS A PIN OF ITS OWN"
		                    : "ADJUSTED TO THIS BOARD'S LIMITS";
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
	button(BACK_X, BACK_Y, BACK_W, BACK_H, "BACK", C_PANEL, C_TEXT, 1,
	       inputPressing(&s_touchSnap, BACK_X, BACK_Y, BACK_W, BACK_H));

	// Read-only on every image: the board is what the hardware answered at
	// boot, not something to pick. Drawn as text rather than a dead-looking
	// button so that nothing invites the tap.
	gfxRect(0, R_BOARD, GFX_W, ROW_H, C_BG);
	gfxText(8, R_BOARD + 8, "BOARD", C_DIM, 1);
	gfxText(GFX_W - 8 - gfxTextW(g_board->label, 1), R_BOARD + 8,
	        g_board->label, C_GRID, 1);

	snprintf(buf, sizeof(buf), "GP%u", (unsigned)s->dshotPin);
	drawStepRow(R_PIN, "ESC PIN", buf, C_LIME);

	snprintf(buf, sizeof(buf), "%u", (unsigned)s->dshotKbaud);
	drawStepRow(R_SPEED, "DSHOT KBAUD", buf, C_TEXT);

	drawToggleRow(R_KISS, "KISS TELEM", s->kissEnable ? "ON" : "OFF", s->kissEnable);

	if (s->kissEnable) snprintf(buf, sizeof(buf), "GP%u", (unsigned)s->kissPin);
	else               snprintf(buf, sizeof(buf), "--");
	drawStepRow(R_KISSPIN, "KISS PIN", buf,
	            s->kissEnable ? C_CYAN : C_DIM);

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
	gfxRect(SAVE_X, FOOT_Y - 4, SAVE_W, 3, C_BG);
	if (s_saveHolding) {
		uint32_t held = millis() - s_saveHoldStart;
		int pct = (int)(held * 100 / SAVE_HOLD_MS);
		if (pct > 100) pct = 100;
		saveFill = C_BLUE;
		gfxRect(SAVE_X, FOOT_Y - 4, SAVE_W * pct / 100, 3, C_LIME);
	}
	button(SAVE_X, FOOT_Y, SAVE_W, FOOT_H, "HOLD TO SAVE", saveFill,
	       s_saveHolding ? C_ONACCENT : C_TEXT, 1, s_saveHolding);
	button(DEF_X, FOOT_Y, DEF_W, FOOT_H, "RESET", C_PANEL, C_AMBER, 1,
	       inputPressing(&s_touchSnap, DEF_X, FOOT_Y, DEF_W, FOOT_H));
}

/**
 * @brief Push the current wiring to core1 and re-apply the display settings.
 *
 * Called after every edit. Each call is a no-op when nothing changed, so this
 * can be unconditional rather than tracking which field moved — which is the
 * kind of bookkeeping that goes wrong exactly once and then behaves like a
 * hardware fault.
 */
static void applyLive() {
	Settings *s = settings();
	uint8_t kissBefore = s->kissEnable;
	s_repaired = !settingsValidate(s);
	s_repairKiss = s_repaired && kissBefore && !s->kissEnable;

	escTaskConfigure(s->dshotPin, s->dshotKbaud, s->kissEnable != 0, s->kissPin);
	// Pure bookkeeping (the eRPM divisor), so unlike the wiring it always
	// follows the settings -- RESET used to change the stored pole count while
	// the RPM readout kept dividing by the old one.
	escSetPoles(s->poles);
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
	memset(&s_touchSnap, 0, sizeof(s_touchSnap));
	memset(&s_pinRep, 0, sizeof(s_pinRep));
	memset(&s_speedRep, 0, sizeof(s_speedRep));
	memset(&s_kissPinRep, 0, sizeof(s_kissPinRep));
	memset(&s_backlightRep, 0, sizeof(s_backlightRep));
	applyLive();
}

/**
 * @brief Step a pin, stepping past the one the other wire is on.
 *
 * The ESC and the telemetry wire cannot share a GPIO. On the 2.8" that is not
 * an abstract rule: the board leaves exactly two pins free, so whichever one
 * you are not on is the other wire's. The validator's answer to a collision is
 * to switch KISS off, which is right for a stored block nobody chose and wrong
 * for a stepper — you asked for the next pin, not for the telemetry to stop.
 *
 * @param from   Current pin.
 * @param dir    +1 or -1.
 * @param avoid  The other wire's pin.
 * @param active Whether the other wire is actually in use.
 * @return The next legal pin, or @p from when the board offers no other.
 */
static uint8_t stepPinAvoiding(uint8_t from, int dir, uint8_t avoid, bool active) {
	uint8_t board = settings()->boardId;
	uint8_t p = settingsNextPinOn(board, from, dir);
	if (active && p == avoid) p = settingsNextPinOn(board, p, dir);
	// Two free pins and both spoken for: there is no third answer, so stay put
	// rather than landing on the pin this function exists to avoid.
	if (active && p == avoid) return from;
	return p;
}

/** @brief Which direction a stepper row is being held in: -1, +1 or 0. */
static int stepDir(const TouchState *t, int rowY) {
	return inputPressing(t, BTN_M_X, rowY, BTN_W, ROW_H) ? -1
	     : inputPressing(t, BTN_P_X, rowY, BTN_W, ROW_H) ? +1 : 0;
}

bool uiSetupTick(const TouchState *t) {
	Settings *s = settings();
	s_touchSnap = *t;

	// --- hold to save ---
	//
	// Evaluated every frame including finger-off, so moving away from the button
	// cancels rather than commits — and only a press that *began* on the button
	// counts, so a finger sliding on from RESET cannot start a hold. Deliberately
	// not an early return: the hold's progress bar is drawn by the block at the
	// bottom of this function, so returning here would make the one control that
	// needs continuous feedback the only one that never repaints.
	bool onSave = inputPressing(t, SAVE_X, FOOT_Y, SAVE_W, FOOT_H);
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
			if (uiArmed()) {
				s_saveOutcome = SaveOutcome::FailedArmed;
			} else {
				// Nothing here can need a reboot to take effect: every value on
				// this screen is applied live by applyLive(), and the one that
				// could not be -- the board -- is no longer a value on this
				// screen. @see boardProbe()
				s_saveOutcome = settingsSave() ? SaveOutcome::Ok
				                               : SaveOutcome::Failed;
			}
		}
		s_redraw = true;
	} else if (s_saveHolding) {
		s_saveHolding = false;
		s_redraw = true;
	}

	bool changed = false;

	// --- steppers: first step on touch-down, then repeat while held ---
	int d = stepDir(t, R_PIN);
	if (repeatFires(&s_pinRep, d, millis())) {
		s->dshotPin = stepPinAvoiding(s->dshotPin, d, s->kissPin,
		                              s->kissEnable != 0);
		changed = true;
	}
	d = stepDir(t, R_SPEED);
	if (repeatFires(&s_speedRep, d, millis())) {
		if (d < 0) s->dshotKbaud = (uint16_t)(s->dshotKbaud <= 150 ? 1200 : s->dshotKbaud / 2);
		else       s->dshotKbaud = (uint16_t)(s->dshotKbaud >= 1200 ? 150 : s->dshotKbaud * 2);
		changed = true;
	}
	d = stepDir(t, R_KISSPIN);
	if (repeatFires(&s_kissPinRep, d, millis())) {
		s->kissPin = stepPinAvoiding(s->kissPin, d, s->dshotPin, true);
		changed = true;
	}
	d = stepDir(t, R_BACKLIGHT);
	if (repeatFires(&s_backlightRep, d, millis())) {
		if (d < 0) {
			s->backlight = (uint8_t)(s->backlight <= BACKLIGHT_STEP
			                             ? 16 : s->backlight - BACKLIGHT_STEP);
		} else {
			int v = s->backlight + BACKLIGHT_STEP;
			s->backlight = (uint8_t)(v > 255 ? 255 : v);
		}
		changed = true;
	}

	// --- everything else: fires on release, inside, having started inside ---
	if (inputTapped(t, BACK_X, BACK_Y, BACK_W, BACK_H)) {
		s_leaving = true;
		return false;
	} else if (inputTapped(t, BTN_M_X, R_KISS, TOGGLE_W, ROW_H)) {
		// Turning KISS on moves it off the ESC's pin rather than enabling it
		// against a collision and letting validation switch it straight back
		// off. That refusal is what "KISS cannot be enabled on the 2.8\"" was:
		// the board frees GP28 and GP29, the ESC starts on GP29, and the pin
		// rules used to insist KISS take a hardware UART RX pin -- of which
		// GP29 was the only free one. Any GPIO receives now. @see pio_uart_rx.h
		if (s->kissEnable) {
			s->kissEnable = 0;
		} else {
			s->kissEnable = 1;
			if (s->kissPin == s->dshotPin)
				s->kissPin = settingsNextPinOn(s->boardId, s->kissPin, +1);
		}
		changed = true;
	} else if (inputTapped(t, BTN_M_X, R_CONTRAST, TOGGLE_W, ROW_H)) {
		s->highContrast = s->highContrast ? 0 : 1;
		changed = true;
	} else if (inputTapped(t, DEF_X, FOOT_Y, DEF_W, FOOT_H)) {
		// Restores the compiled defaults into the live settings only. Flash
		// is untouched until SAVE, so this is undoable by walking away. The
		// defaults are this board's -- settingsDefaults() reads them out of the
		// descriptor the boot probe selected.
		settingsDefaults(s);
		s_saveOutcome = SaveOutcome::None;
		changed = true;
	}

	if (changed) {
		applyLive();
		s_saveOutcome = (s_saveOutcome == SaveOutcome::Ok)
		                    ? SaveOutcome::None : s_saveOutcome;
		s_redraw = true;
	}

	// Pressed looks have to appear on touch-down and clear on release, so any
	// frame with a finger involved repaints. This is the fix for the stepper
	// that stayed pressed-looking after the finger lifted: the release frame
	// used to change nothing and therefore drew nothing.
	if (t->down || t->released) s_redraw = true;

	if (s_redraw) {
		s_redraw = false;
		drawAll();
	} else {
		drawLink(false);
	}

	return !s_leaving;
}
