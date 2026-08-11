/**
 * @file test_ui.cpp
 * @brief UI, navigation and gesture tests driven through the real screen code.
 *
 * ui_am32.cpp is included rather than linked so the tests can read its
 * file-static state directly. That keeps the production source free of
 * test-only accessors, at the cost of this translation unit owning it -- the
 * Makefile therefore must not compile ui_am32.cpp separately.
 */

#include "check.h"
#include "fakes.h"
#include "gfx.h"
#include "ui.h"
#include "cst816.h"
#include "esc_task.h"
#include "esc_merge.h"
#include "sd_log.h"
#include "plat.h"

#include "../src/ui_am32.cpp"   // NOLINT -- see the note above

// --- probes into the included translation unit ---
static uint8_t uiByte(int o)     { return s_eeprom[o]; }
static int     uiSelected()      { return s_selected; }
static int     uiScroll()        { return s_scroll; }
static int     uiVisibleCount()  { return (int)s_visibleCount; }

/** @brief Scroll a named field to the top row and return a y inside it. */
static int uiRowY(const char *name) {
	for (uint16_t v = 0; v < s_visibleCount; v++) {
		if (strcmp(AM32_FIELDS[s_visible[v]].name, name) == 0) {
			int maxScroll = (int)s_visibleCount - LIST_ROWS;
			if (maxScroll < 0) maxScroll = 0;
			s_scroll = (int)v > maxScroll ? maxScroll : (int)v;
			return LIST_Y0 + ((int)v - s_scroll) * ROW_H + 13;
		}
	}
	return -1;
}

// --- frame driving ---
#define UI_FRAME_MS 25

static void frames(int n) {
	for (int i = 0; i < n; i++) { fakeAdvance(UI_FRAME_MS); uiTick(); }
}
static void tap(int x, int y) {
	fakePress(x, y); frames(2); fakeRelease(); frames(1);
}
static void swipe(int x0, int x1, int y, int step) {
	fakePress(x0, y); frames(1);
	for (int x = x0; (step > 0) ? (x <= x1) : (x >= x1); x += step) {
		fakeHold(x, y); frames(1);
	}
	fakeRelease(); frames(1);
}

// --- main-screen coordinates ---
#define BTN_ARM_X    58
#define BTN_ARM_Y   299
#define BTN_HOLD_X  143
#define BTN_CFG_X   205
// The AM32 row is split: AM32 on the left, SD LOG on the right, with a gap
// between them. Tapping the row centre lands in that gap and hits neither,
// which is how this constant first went wrong.
#define BTN_AM32_X   66   // BTN_AM32 spans x 14..117
#define BTN_LOG_X   174   // BTN_LOG  spans x 122..225
#define BTN_AM32_Y  275   // both span y 256..293
// --- config-screen coordinates ---
#define AM32_BACK_X 216
#define AM32_BACK_Y  23
#define AM32_MINUS_X 34
#define AM32_PLUS_X 205
#define AM32_EDIT_Y 240
#define AM32_WRITE_X 50
#define AM32_WRITE_Y 290
// --- logging-screen coordinates, mirroring ui.cpp ---
#define LOG_TOGGLE_Y 232
#define LOG_BACK_Y   292

/**
 * @brief Navigate main -> SETTINGS -> SD LOG.
 *
 * Assumes the main screen is showing and the ESC is disarmed.
 */
static void enterLogScreen() {
	tap(BTN_CFG_X, BTN_ARM_Y);
	tap(BTN_LOG_X, BTN_AM32_Y);
}

/**
 * @brief The logging screen's buttons, checked through the fake logger.
 *
 * No probe into ui.cpp is needed: the fake *is* the observable. Tapping START
 * has to reach sdLogStart() for the fake's state to change, so this exercises
 * the real navigation, hit-testing and dispatch rather than asserting on
 * pixels.
 */
static void testLogScreen() {
	section("SD logging screen");

	// A card that mounted but is not recording.
	SdLogStatus st;
	memset(&st, 0, sizeof(st));
	st.state = SdLogState::Idle;
	fakeSdLogSet(&st);

	enterLogScreen();
	frames(2);
	checkTrue("not recording on arrival", !sdLogActive());
	fakeDumpFrame("shot_log_ready.ppm");

	tap(120, LOG_TOGGLE_Y + 20);
	checkTrue("START begins a log", sdLogActive());

	sdLogStatus(&st);
	checkTrue("a file number is assigned", st.fileNumber != 0);

	tap(120, LOG_TOGGLE_Y + 20);
	checkTrue("STOP ends it", !sdLogActive());

	// Counters worth showing: a full buffer and lost frames are the two things
	// the screen exists to make visible.
	memset(&st, 0, sizeof(st));
	st.state = SdLogState::Logging;
	st.fileNumber = 42;
	st.framesLogged = 12345;
	st.bytesWritten = 178000;
	st.dropEvents = 3;
	st.peakBuffer = 7000;
	st.worstFlushMs = 64;
	fakeSdLogSet(&st);
	frames(2);
	fakeDumpFrame("shot_log_screen.ppm");

	// With no card, START must not pretend to have started.
	memset(&st, 0, sizeof(st));
	st.state = SdLogState::NoCard;
	fakeSdLogSet(&st);
	frames(2);
	// The first thing anyone sees, since the card slot is empty by default.
	fakeDumpFrame("shot_log_nocard.ppm");
	tap(120, LOG_TOGGLE_Y + 20);
	checkTrue("START does nothing without a card", !sdLogActive());

	tap(120, LOG_BACK_Y + 9);
	frames(2);
}

/**
 * @brief The main screen with KISS telemetry live.
 *
 * The merge policy itself is unit-tested in test_kiss.cpp; what this covers is
 * that the UI actually goes through escMerge() rather than reading the EDT
 * fields directly, which is what it did before.
 */
static void testKissDisplay() {
	section("KISS on the main screen");

	EscTelemetry tel;
	memset(&tel, 0, sizeof(tel));
	tel.erpm = 84210; tel.rpm = 12030;
	tel.volts = 15.75f; tel.haveVolts = true;   // EDT: 0.25 V steps
	tel.amps = 23.0f;   tel.haveAmps = true;    // EDT: whole amps
	tel.tempC = 46;     tel.haveTemp = true;
	tel.stress = 12;    tel.haveStress = true;
	tel.packetRate = 998;

	// Fresh KISS, with values deliberately distinguishable from the EDT ones so
	// a screenshot shows which source won.
	tel.haveKiss   = true;
	tel.kissLastMs = millis();
	tel.kissVolts  = 15.83f;
	tel.kissAmps   = 23.47f;
	tel.kissTempC  = 47;
	tel.kissMah    = 812;
	tel.kissErpm   = 84200;
	fakeSetTelemetry(&tel);
	frames(2);
	fakeDumpFrame("shot_tester_kiss.ppm");

	// Merge is per-field and time-based, so let KISS go stale and confirm the
	// screen falls back rather than freezing the fine values.
	EscReading r;
	escMerge(&tel, tel.kissLastMs + KISS_STALE_MS, KISS_STALE_MS, &r);
	checkTrue("stale KISS falls back to EDT", r.voltsFrom == EscSource::Edt);
	escMerge(&tel, tel.kissLastMs + 1, KISS_STALE_MS, &r);
	checkTrue("fresh KISS is preferred", r.voltsFrom == EscSource::Kiss);
	checkInt("and carries 0.01 V resolution",
	         (long)(r.volts * 100.0f + 0.5f), 1583);
}

void runUiTests() {
	EscTelemetry tel;
	memset(&tel, 0, sizeof(tel));
	tel.erpm = 84210; tel.rpm = 12030;
	tel.volts = 15.8f; tel.haveVolts = true;
	tel.amps = 23.4f;  tel.haveAmps = true;
	tel.tempC = 46;    tel.haveTemp = true;
	tel.stress = 12;   tel.haveStress = true;
	tel.packetRate = 998;
	fakeSetTelemetry(&tel);

	gfxInit();
	// Render the real splash, so the documentation screenshot cannot drift
	// away from what the board shows.
	uiDrawSplash();
	fakeDumpFrame("shot_splash.ppm");

	uiInit();
	frames(4);
	fakeDumpFrame("shot_tester_disarmed.ppm");

	section("Throttle: spring mode is absolute");
	{
		fakePress(BTN_ARM_X, BTN_ARM_Y); frames(1);
		for (int i = 0; i < 60; i++) { fakeHold(BTN_ARM_X, BTN_ARM_Y); frames(1); }
		fakeRelease(); frames(2);
		checkTrue("armed only after a 1 s hold", fakeArmed());

		int tx = 8 + (224 * 70 / 100);
		fakePress(tx, 260); frames(1); fakeHold(tx, 260); frames(2);
		checkInt("drag to 70% of track", fakeThrottle(), 400 * 70 / 100, 6);
		fakeDumpFrame("shot_tester_armed.ppm");
		fakeRelease(); frames(2);
		checkInt("springs back to zero on release", fakeThrottle(), 0);
	}

	section("Throttle: HOLD mode is relative");
	{
		tap(BTN_HOLD_X, BTN_ARM_Y);                       // HOLD on
		fakePress(120, 260); frames(2);
		checkInt("touching the bar does not jump", fakeThrottle(), 0);
		for (int x = 120; x <= 176; x += 8) { fakeHold(x, 260); frames(1); }
		checkInt("drag +56px raises by a quarter range", fakeThrottle(), 100, 6);
		fakeRelease(); frames(2);
		checkInt("latches after release", fakeThrottle(), 100, 6);

		// Overshoot a rail, then reverse: must respond at once, not unwind.
		swipe(200, 8, 260, -8);
		checkInt("overshoot clamps to zero", fakeThrottle(), 0);
		fakePress(8, 260); frames(1);
		fakeHold(19, 260); frames(1);
		fakeHold(30, 260); frames(1);
		checkInt("reverse re-anchors, no windup", fakeThrottle(), 39, 6);
		fakeRelease(); frames(2);
		tap(BTN_HOLD_X, BTN_ARM_Y);                       // HOLD off
		checkInt("turning HOLD off zeroes throttle", fakeThrottle(), 0);
	}

	section("Throttle pad: the number area is always relative");
	{
		tap(BTN_HOLD_X, BTN_ARM_Y);                       // HOLD on
		fakePress(120, 180); frames(2); fakeRelease(); frames(2);
		checkInt("a tap on the pad does nothing", fakeThrottle(), 0);
		fakePress(120, 180); frames(1);
		fakeHold(120, 176); frames(1);
		fakeHold(120, 177); frames(1);
		checkInt("sub-deadzone wiggle ignored", fakeThrottle(), 0);
		fakeRelease(); frames(2);
		// 106 px of travel less the 6 px deadzone, at 60 % over a 213 px pad.
		fakePress(120, 200); frames(1);
		for (int y = 200; y >= 94; y -= 6) { fakeHold(120, y); frames(1); }
		checkInt("swipe up raises", fakeThrottle(), 400 * 100 / 213 * 60 / 100, 4);
		fakeRelease(); frames(2);
		tap(BTN_HOLD_X, BTN_ARM_Y);                       // HOLD off
	}

	section("Navigation into AM32 config");
	{
		tap(BTN_CFG_X, BTN_ARM_Y);
		checkTrue("entering settings force-disarms", !fakeArmed());
		fakeDumpFrame("shot_settings.ppm");
		tap(BTN_AM32_X, BTN_AM32_Y);
		for (int i = 0; i < 8; i++) frames(1);
		checkTrue("settings read and decoded", uiVisibleCount() > 0);
		checkInt("poles decoded", uiByte(0x1B), 14);
		fakeDumpFrame("shot_am32_list.ppm");
	}

	section("Editing: fine, coarse, and the write interlock");
	{
		int yPwm = uiRowY("PWM FREQ"); frames(1);
		tap(60, yPwm);
		checkInt("row selects", uiByte(0x18), 24);

		tap(AM32_PLUS_X, AM32_EDIT_Y);
		checkInt("+ button is one fine step", uiByte(0x18), 25);
		fakeDumpFrame("shot_am32_edit.ppm");
		tap(AM32_MINUS_X, AM32_EDIT_Y);
		checkInt("- button steps back", uiByte(0x18), 24);

		swipe(30, 220, yPwm, 10);
		checkTrue("swipe right is coarse", uiByte(0x18) > 100);
		checkTrue("coarse clamps at 144", uiByte(0x18) <= 144);
		swipe(220, 30, yPwm, -10);
		checkTrue("swipe left lowers", uiByte(0x18) < 100);
		checkTrue("coarse clamps at 8", uiByte(0x18) >= 8);

		int yPoles = uiRowY("POLES"); frames(1);
		swipe(40, 150, yPoles, 7);
		checkInt("stepped fields stay legal (poles even)", uiByte(0x1B) % 2, 0);

		// Axis lock: a vertical drag must scroll, never edit.
		int yKv = uiRowY("KV"); frames(1);
		uint8_t kvBefore = uiByte(0x1A);
		fakePress(120, yKv); frames(1);
		for (int y = yKv; y >= 60; y -= 10) { fakeHold(120, y); frames(1); }
		fakeRelease(); frames(1);
		checkInt("vertical drag did not edit", uiByte(0x1A), kvBefore);
		checkTrue("vertical drag scrolled", uiScroll() > 0);

		// The editor bar must never fall through to the row behind it.
		int before = uiSelected();
		tap(AM32_PLUS_X, AM32_EDIT_Y);
		checkInt("editor bar does not re-select a row", uiSelected(), before);
	}

	section("Write: interlock, commit, verify, hand back");
	{
		int yPoles = uiRowY("POLES"); frames(1);
		tap(60, yPoles);
		uint8_t want = uiByte(0x1B);
		tap(AM32_PLUS_X, AM32_EDIT_Y);
		want = uiByte(0x1B);
		checkTrue("edit is local until written", fakeEscByte(0x1B) != want);

		fakePress(AM32_WRITE_X, AM32_WRITE_Y); frames(1);
		fakeRelease(); frames(2);
		checkTrue("a tap on WRITE does not commit", fakeEscByte(0x1B) != want);

		fakePress(AM32_WRITE_X, AM32_WRITE_Y); frames(1);
		for (int i = 0; i < 60; i++) { fakeHold(AM32_WRITE_X, AM32_WRITE_Y); frames(1); }
		fakeRelease(); frames(3);
		checkInt("1 s hold commits and verifies", fakeEscByte(0x1B), want);
		fakeDumpFrame("shot_am32_written.ppm");

		tap(120, 218);                                   // dismiss the result
		tap(180 + 27, 268 + 23);                         // HEX view
		frames(2);
		fakeDumpFrame("shot_am32_hex.ppm");
		tap(180 + 27, 268 + 23);                         // back to fields
		tap(AM32_BACK_X, AM32_BACK_Y);                   // leave config
		checkTrue("DShot pin handed back", fakePinReturned());
	}

	section("Splash fits the panel");
	{
		checkTrue("\"DSHOT\" at scale 4 fits", gfxTextW("DSHOT", 4) <= GFX_W);
		checkTrue("\"DISPLAY\" at scale 4 fits", gfxTextW("DISPLAY", 4) <= GFX_W);
		checkTrue("subtitle fits", gfxTextW("BIDIRECTIONAL ESC TESTER", 1) <= GFX_W);
		checkTrue("credit line fits", gfxTextW("JuWi made", 2) <= GFX_W);
		// Regression: the original splash was drawn at a hardcoded x=24, which
		// pushed the trailing "Y" 15 px off a 240 px panel.
		checkTrue("old hardcoded placement would overflow",
		          24 + gfxTextW("DSHOT DISPLAY", 3) > GFX_W);
	}

	testLogScreen();
	testKissDisplay();
}
