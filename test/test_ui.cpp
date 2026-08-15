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
#include "settings.h"
#include "ui_input.h"
#include "board_desc.h"
#include "gfx.h"
#include "ui.h"
#include "ui_setup.h"
#include "touch.h"
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
// The nav row is three buttons with gaps between them, so tapping the row
// centre lands in a gap and hits nothing -- which is how these constants first
// went wrong, back when the row held two.
// The AM32/LOG/SETUP row: three 68 px buttons with 4 px gaps, from x=14.
#define BTN_AM32_X    48   // BTN_AM32  spans x  14..81
#define BTN_LOG_X    120   // BTN_LOG   spans x  86..153
#define BTN_SETUP_X  192   // BTN_SETUP spans x 158..225
// --- the shared BACK button, which is now the same rectangle on SETTINGS,
//     SD LOG, SETUP and AM32. One constant for all four is the point: when
//     these were four different rectangles, this file had three of them and
//     was missing the fourth. @see ui_widgets.h ---
#define BACK_X      (UI_BACK_X + UI_BACK_W / 2)   // x 184..233
#define BACK_Y      (UI_BACK_Y + UI_BACK_H / 2)   // y   4..25
// --- AM32 screen ---
#define AM32_MINUS_X 34
#define AM32_PLUS_X 205
#define AM32_EDIT_Y 240
#define AM32_WRITE_X 50
#define AM32_WRITE_Y 290
// --- settings-screen command buttons, mirroring ui.cpp ---
#define CFG_BEEP_X  120   // BTN_BEEP spans the full row, x 14..225
#define CFG_CMD_Y_T 214   // y 192..235
#define CFG_BEEP_Y  CFG_CMD_Y_T
// --- settings-screen steppers: BTN_*_M x 14..59, BTN_*_P x 180..225 ---
#define CFG_POLES_M_X  37
#define CFG_POLES_P_X 203
#define CFG_POLES_Y    90   // BTN_POLES_* y  68..111
#define CFG_MAXT_M_X   37
#define CFG_MAXT_P_X  203
#define CFG_MAXT_Y    156   // BTN_MAXT_*  y 134..177
// --- the AM32/LOG/SETUP row: three 68 px buttons with 4 px gaps, from x=14 ---
#define BTN_AM32_Y   282   // all three span y 258..305
// --- throttle gauge: THR_TRACK_Y 248, height 26 ---
#define THR_Y         260
// --- logging-screen coordinates, mirroring ui.cpp ---
#define LOG_TOGGLE_Y 258   // BTN_LOG_TOGGLE y 236..279
#define LOG_RETRY_Y  301   // BTN_LOG_RETRY  y 286..315

/**
 * @brief The standard "ESC is talking" fixture, stamped as having just arrived.
 *
 * Call it again to keep the data alive. A real ESC sends continuously, but the
 * fake serves one frozen snapshot, so without re-stamping the readings expire
 * mid-test exactly as they are meant to -- which is correct behaviour and a
 * useless screenshot.
 */
static void feedLiveTelemetry() {
	EscTelemetry tel;
	memset(&tel, 0, sizeof(tel));
	tel.erpm = 84210; tel.rpm = 12030;
	tel.volts = 15.8f; tel.edtVoltsMs  = millis();
	tel.amps = 23.4f;  tel.edtAmpsMs   = millis();
	tel.tempC = 46;    tel.edtTempMs   = millis();
	tel.stress = 12;   tel.edtStressMs = millis();
	tel.edtStatusMs = millis();
	tel.lastRpmMs = millis();
	tel.packetRate = 998;
	fakeSetTelemetry(&tel);
}

/** @brief Hold the ARM button long enough to actually arm. */
static void holdToArm() {
	fakePress(BTN_ARM_X, BTN_ARM_Y); frames(1);
	for (int i = 0; i < 60; i++) { fakeHold(BTN_ARM_X, BTN_ARM_Y); frames(1); }
	fakeRelease(); frames(2);
}

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

	// A card that mounted but is not recording. The card details matter even
	// though no assertion reads them: this frame is published, and a screen
	// reading "MOUNT 0 OK" beside "CARD NONE" contradicts itself.
	SdLogStatus st;
	memset(&st, 0, sizeof(st));
	st.state = SdLogState::Idle;
	st.cardType = 3;             // SDHC/XC
	st.cardSizeMB = 30500;       // a 32 GB card, as the card reports itself
	fakeSdLogSet(&st);

	enterLogScreen();
	frames(2);
	checkTrue("not recording on arrival", !sdLogActive());
	fakeDumpFrame("shot_log_ready.ppm");

	tap(120, LOG_TOGGLE_Y);
	checkTrue("START begins a log", sdLogActive());

	sdLogStatus(&st);
	checkTrue("a file number is assigned", st.fileNumber != 0);

	tap(120, LOG_TOGGLE_Y);
	checkTrue("STOP ends it", !sdLogActive());

	// Counters worth showing: a full buffer and lost frames are the two things
	// the screen exists to make visible.
	memset(&st, 0, sizeof(st));
	st.state = SdLogState::Logging;
	st.cardType = 3;
	st.cardSizeMB = 30500;
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
	st.mountResult = 3;          // FR_NOT_READY: nothing answered on the bus
	fakeSdLogSet(&st);
	frames(2);
	// The first thing anyone sees, since the card slot is empty by default.
	fakeDumpFrame("shot_log_nocard.ppm");
	tap(120, LOG_TOGGLE_Y);
	checkTrue("START does nothing without a card", !sdLogActive());

	// A card inserted after boot. sdLogBegin() only runs once, so without this
	// button the card would stay invisible until a power cycle -- which looks
	// exactly like a card the firmware cannot read.
	tap(120, LOG_RETRY_Y);
	frames(2);
	sdLogStatus(&st);
	checkTrue("RETRY finds a card inserted later",
	          st.state == SdLogState::Idle);
	checkInt("and reports it mounted cleanly", st.mountResult, 0);
	checkInt("with its type", st.cardType, 3);
	checkTrue("and its size", st.cardSizeMB > 100000);
	fakeDumpFrame("shot_log_mounted.ppm");

	tap(BACK_X, BACK_Y);
	frames(2);
}

/**
 * @brief A hand-started log must survive arming and the disarm that follows.
 *
 * Reported from hardware, and a good bug: entering the settings screen
 * force-disarms, and the logging screen is reached *through* settings. So
 * auto-stop-on-disarm cancelled exactly the log you walked over to check on,
 * and there was no way to observe a manual log without ending it.
 */
static void testManualLogSurvivesArming() {
	section("Manual logging vs auto-on-arm");

	SdLogStatus st;
	memset(&st, 0, sizeof(st));
	st.state = SdLogState::Idle;
	st.cardSizeMB = 61000;
	fakeSdLogSet(&st);

	enterLogScreen();
	tap(120, LOG_TOGGLE_Y);
	checkTrue("manual START begins a log", sdLogActive());

	// Back to the main screen and arm.
	tap(BACK_X, BACK_Y);
	frames(2);
	// A tap will not arm: it takes a one-second hold, and using tap() here is
	// how an earlier version of this test passed against the broken rule.
	holdToArm();
	checkTrue("actually armed", uiArmed());
	checkTrue("still logging while armed", sdLogActive());

	// Walking back to the logging screen goes through settings, which
	// force-disarms. That must not end a log the operator started.
	enterLogScreen();
	frames(2);
	checkTrue("armed state cleared by entering settings", !uiArmed());
	checkTrue("manual log survives the force-disarm", sdLogActive());

	// STOP is still the operator's to press.
	tap(120, LOG_TOGGLE_Y);
	checkTrue("manual STOP ends it", !sdLogActive());
	tap(BACK_X, BACK_Y);
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
	tel.volts = 15.75f; tel.edtVoltsMs  = millis();  // EDT: 0.25 V steps
	tel.amps = 23.0f;   tel.edtAmpsMs   = millis();  // EDT: whole amps
	tel.tempC = 46;     tel.edtTempMs   = millis();
	tel.stress = 12;    tel.edtStressMs = millis();
	tel.edtStatusMs = millis();
	tel.lastRpmMs = millis();
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

	// --- what the screen shows, not what escMerge() returns ---
	//
	// This test used to call escMerge() itself and assert on the struct, which
	// is a pure-function test already covered in test_kiss.cpp, and is not what
	// the docstring above claims. It proved nothing about the screen: making
	// drawTelemetry() read the raw EDT field instead of the merged one passed
	// it, and would have put a coarse 0.25 V value on screen wearing a KISS tag.
	//
	// Tying it to pixels is what closes that. The two sources carry deliberately
	// different values, so the voltage tile must render differently depending on
	// which one won.
	// The *value* rows only, not the whole tile. The KISS/EDT tag sits on the
	// label row at the top, and it changes with the source all by itself -- so
	// hashing the whole tile passes even when the number below it came from the
	// wrong place, which is exactly the regression being guarded against.
	// drawLabelled() puts the label at y+4 and the value at y+15, scale 2.
	auto voltTile = []() { return fakeRegionHash(1, 143, 118, 18); };
	auto ampTile  = []() { return fakeRegionHash(121, 143, 118, 18); };

	uint32_t voltsFromKiss = voltTile();
	uint32_t ampsFromKiss  = ampTile();

	// Let KISS expire while EDT stays fresh. Same ESC, same tile, coarser
	// number -- and a different tag beside it.
	fakeAdvance(KISS_STALE_MS + 50);
	tel.edtVoltsMs = millis();
	tel.edtAmpsMs  = millis();
	tel.edtTempMs  = millis();
	tel.edtStressMs = millis();
	tel.edtStatusMs = millis();
	tel.lastRpmMs = millis();
	fakeSetTelemetry(&tel);
	frames(2);
	checkTrue("the voltage tile changes when KISS expires", voltTile() != voltsFromKiss);
	checkTrue("and so does the current tile", ampTile() != ampsFromKiss);

	// Bring KISS back and the fine values must return -- the same pixels as
	// before, not merely different ones.
	tel.kissLastMs = millis();
	fakeSetTelemetry(&tel);
	frames(2);
	checkTrue("fresh KISS restores the fine reading exactly", voltTile() == voltsFromKiss);
	checkTrue("for current too", ampTile() == ampsFromKiss);

	// The merge policy itself, for completeness. These are the assertions the
	// test used to consist of.
	EscReading r;
	escMerge(&tel, tel.kissLastMs + KISS_STALE_MS, KISS_STALE_MS, EDT_STALE_MS, &r);
	checkTrue("stale KISS falls back to EDT", r.voltsFrom == EscSource::Edt);
	escMerge(&tel, tel.kissLastMs + 1, KISS_STALE_MS, EDT_STALE_MS, &r);
	checkTrue("fresh KISS is preferred", r.voltsFrom == EscSource::Kiss);
	checkInt("and carries 0.01 V resolution",
	         (long)(r.volts * 100.0f + 0.5f), 1583);
}

/**
 * @brief Telemetry blanks when the ESC stops answering.
 *
 * Reported as: readings stay on screen after the ESC is unplugged or swapped,
 * looking exactly like live data from hardware that is no longer there.
 *
 * The merge policy has its own unit tests. What this covers is the part those
 * cannot: that the screen actually goes through it, and that an expired
 * reading renders identically to one that never arrived. Anything less strict
 * -- "the region changed" -- would pass on a display that merely dimmed the
 * stale number while still showing it.
 */
static void testTelemetryExpires() {
	section("Telemetry expiry on screen");

	// The LINK tile is excluded from every comparison below. Packet rate is a
	// rolling count of the last second's frames, so on hardware it falls to
	// zero by itself -- but the fake serves one fixed snapshot forever, so it
	// stays pinned at 998 and would mask the tiles that do expire.
	// Everything else in the band is covered: both top rows, plus the status
	// tile in the bottom-left.
	auto tiles = []() {
		return fakeRegionHash(0, 128, 240, 70) ^ fakeRegionHash(0, 199, 120, 34);
	};

	// Baseline: an ESC that has never said anything.
	EscTelemetry none;
	memset(&none, 0, sizeof(none));
	fakeSetTelemetry(&none);
	frames(2);
	uint32_t blank = tiles();

	EscTelemetry tel;
	memset(&tel, 0, sizeof(tel));
	tel.erpm = 84210; tel.rpm = 12030;
	tel.volts = 15.75f; tel.edtVoltsMs  = millis();
	tel.amps  = 23.0f;  tel.edtAmpsMs   = millis();
	tel.tempC = 46;     tel.edtTempMs   = millis();
	tel.stress = 12;    tel.edtStressMs = millis();
	tel.warning = true; tel.edtStatusMs = millis();
	tel.lastRpmMs = millis();
	tel.packetRate = 998;
	fakeSetTelemetry(&tel);
	frames(2);
	uint32_t live = tiles();
	checkTrue("live telemetry differs from blank", live != blank);
	fakeDumpFrame("shot_tester_live.ppm");

	// Still inside the window. frames() advances the clock itself, so the
	// budget has to cover the ticks as well as the wait -- getting that wrong
	// is what made this test fail first time round.
	fakeAdvance(EDT_STALE_MS - 200);
	frames(2);
	checkTrue("still shown just inside the window", tiles() == live);

	// Past it: gone, and gone completely. "The region changed" would not be
	// enough -- that passes on a display that merely dims a stale number while
	// still showing it.
	fakeAdvance(250);
	frames(2);
	uint32_t stale = tiles();
	checkTrue("expired telemetry is cleared", stale != live);
	checkTrue("and is indistinguishable from never-seen", stale == blank);
	fakeDumpFrame("shot_tester_stale.ppm");

	// The two checks above compare rendered states of the same code path, so a
	// change that corrupts both identically satisfies them. It did: mapping an
	// expired status block to "OK" rather than "--" kept stale == blank and
	// live != blank, and made an ESC that had stopped talking read as all-clear
	// -- the one direction a warning indicator may not fail.
	//
	// Comparing against a *known-good* render instead is what closes that. An
	// ESC reporting OK and an ESC reporting nothing must not look the same.
	auto statusTile = []() { return fakeRegionHash(0, 199, 120, 34); };

	EscTelemetry ok;
	memset(&ok, 0, sizeof(ok));
	ok.erpm = 84210; ok.rpm = 12030;
	ok.lastRpmMs = millis();
	ok.packetRate = 998;
	ok.edtStatusMs = millis();     // a status block arrived, and it is all clear
	fakeSetTelemetry(&ok);
	frames(2);
	uint32_t statusOk = statusTile();

	fakeAdvance(EDT_STALE_MS + 100);
	frames(2);
	checkTrue("an expired status never renders as OK", statusTile() != statusOk);

	// And the same for a warning: expiring must not silently clear it either.
	EscTelemetry warn = ok;
	warn.warning = true;
	warn.edtStatusMs = millis();
	warn.lastRpmMs = millis();
	fakeSetTelemetry(&warn);
	frames(2);
	uint32_t statusWarn = statusTile();
	checkTrue("a warning looks different from all-clear", statusWarn != statusOk);
	fakeAdvance(EDT_STALE_MS + 100);
	frames(2);
	checkTrue("an expired warning does not become OK", statusTile() != statusOk);
}

/**
 * @brief The EDT chip tracks reception, and BEEP acknowledges a press.
 *
 * BEEP was reported as showing no reaction. The command was going out -- the
 * ESC beeped -- but nothing on screen moved, because the command is over in
 * about 6 ms and the UI repaints at 40 Hz.
 *
 * The EDT chip beside it is read-only. There is no enable button any more:
 * the firmware sends one to each ESC as it appears, so the control was for
 * something already handled. What remains is worth showing, because "green"
 * and "all four telemetry tiles read --" are the same fact.
 */
static void testSettingsCommandRow() {
	section("Settings command row");

	feedLiveTelemetry();
	tap(BTN_CFG_X, BTN_ARM_Y);          // into settings (this force-disarms)
	frames(2);
	uint32_t chipOn = fakeRegionHash(120, UI_STRIP_Y, 120, UI_STRIP_H);
	uint32_t idle   = fakeRegionHash(0, CFG_BEEP_Y - 24, 240, 60);
	fakeDumpFrame("shot_config_edt_on.ppm");

	// Letting telemetry expire has to change the chip, or the colour is
	// decoration rather than a readout.
	EscTelemetry none;
	memset(&none, 0, sizeof(none));
	fakeSetTelemetry(&none);
	frames(2);
	checkTrue("EDT chip changes when telemetry stops",
	          fakeRegionHash(120, UI_STRIP_Y, 120, UI_STRIP_H) != chipOn);
	fakeDumpFrame("shot_config_edt_off.ppm");
	feedLiveTelemetry();
	frames(2);
	checkTrue("and changes back when it returns",
	          fakeRegionHash(120, UI_STRIP_Y, 120, UI_STRIP_H) == chipOn);

	// The chip is not a button. Tapping it must not be mistaken for one. It
	// lives on the strip under the title now, not in the header: the header's
	// right-hand end is BACK's on every screen. Tapping the old spot would
	// leave the screen, which is a better reason to move it than tidiness.
	int beepBefore = fakeBeepRequests();
	tap(200, UI_STRIP_Y + UI_STRIP_H / 2);
	checkInt("tapping the chip does nothing", fakeBeepRequests(), beepBefore);

	// BEEP now has the row to itself, so a tap anywhere along it lands.
	fakePress(CFG_BEEP_X, CFG_CMD_Y_T); frames(1); fakeRelease(); frames(1);
	checkInt("tapping BEEP sends the command", fakeBeepRequests(), beepBefore + 1);
	uint32_t lit = fakeRegionHash(0, CFG_BEEP_Y - 24, 240, 60);
	checkTrue("and the button acknowledges it", lit != idle);
	fakeDumpFrame("shot_config_beep_flash.ppm");

	fakeAdvance(400);
	frames(2);
	checkTrue("the flash clears itself",
	          fakeRegionHash(0, CFG_BEEP_Y - 24, 240, 60) == idle);

	// Left-hand end of the row, where the EDT button used to be.
	fakePress(40, CFG_CMD_Y_T); frames(1); fakeRelease(); frames(1);
	checkInt("the old EDT slot is BEEP now", fakeBeepRequests(), beepBefore + 2);
	fakeAdvance(400); frames(2);

	// Refusal. Not reachable by hand -- opening settings force-disarms and
	// there is no ARM control on that screen -- so this drives the arm state
	// directly. It is tested because the request can refuse, not because a
	// user can currently make it.
	escSetArmed(true);
	fakePress(CFG_BEEP_X, CFG_CMD_Y_T); frames(1); fakeRelease(); frames(1);
	uint32_t refused = fakeRegionHash(0, CFG_BEEP_Y - 24, 240, 60);
	checkTrue("a refused command looks different from an accepted one",
	          refused != lit && refused != idle);
	fakeDumpFrame("shot_config_beep_refused.ppm");
	escSetArmed(false);
	fakeAdvance(400); frames(2);

	tap(BACK_X, BACK_Y);               // BACK, now in the header
	frames(2);
}


// --- SETUP screen coordinates, mirroring ui_setup.cpp. BACK is BACK_X/BACK_Y
//     above, the same rectangle as on every other screen. ---
#define SET_MINUS_X 168   // BTN_M_X 148 + BTN_W 40 -> centre 168
#define SET_PLUS_X  212   // BTN_P_X 192 + BTN_W 40 -> centre 212
#define SET_TOGGLE_X 190  // the wide toggle spans 148..231
// One fixed layout: seven rows from R_BOARD 36 at a 26 px pitch, ROW_H 24.
#define SET_R_BOARD    48
#define SET_R_PIN      74
#define SET_R_SPEED   100
#define SET_R_KISS    126
#define SET_R_KISSPIN 152
#define SET_R_CONTRAST 178
#define SET_R_BACKLIGHT 204
#define SET_SAVE_X     83  // SAVE_X 8, SAVE_W 150
#define SET_SAVE_Y    274  // FOOT_Y 254, FOOT_H 40
#define SET_RESET_X   198
#define SAVE_BAR_X     8   // progress bar spans SAVE_X..+SAVE_W
#define SAVE_BAR_Y   250   // FOOT_Y - 4

/** @brief Walk from the main screen into CFG, then into SETUP. */
static void enterSetup() {
	tap(BTN_CFG_X, BTN_ARM_Y);
	tap(BTN_SETUP_X, BTN_AM32_Y);
	frames(2);
}

/**
 * @brief The SETUP screen: wiring changes reach the pump, and only legal
 *        values can be selected.
 *
 * The last part is the one worth having. A pin picker whose buttons can land on
 * an occupied GPIO needs an error state, a message, and a rule about what
 * happens to a pin you cannot use; one that steps through the board's free mask
 * needs none of those, because the invalid selection cannot be made. Asserting
 * it here is what keeps that true when the mask changes.
 */
static void testSetupWiring() {
	section("SETUP: wiring");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	enterSetup();

	uint8_t start = settings()->dshotPin;
	checkTrue("entering SETUP does not disturb the pin",
	          escTaskDshotPin() == start);

	// One step first, so the assertions below hold on the 2.8" too: that board
	// offers exactly two free pins, so an even number of steps lands back where
	// it started and nothing would be dirty.
	tap(SET_PLUS_X, SET_R_PIN);
	checkTrue("one step moves the pin", settings()->dshotPin != start);
	checkTrue("changing the pin is unsaved", settingsDirty());
	checkTrue("the change reached the DShot pump",
	          escTaskDshotPin() == settings()->dshotPin);

	// Then step all the way round and check every stop is legal.
	bool allFree = true;
	for (int i = 0; i < 6; i++) {
		tap(SET_PLUS_X, SET_R_PIN);
		if (!settingsPinFree(settings()->dshotPin)) allFree = false;
		tap(SET_MINUS_X, SET_R_PIN);
		if (!settingsPinFree(settings()->dshotPin)) allFree = false;
	}
	checkTrue("every step lands on a free GPIO", allFree);

	// Bitrate halves and doubles through the four legal values, and wraps.
	settings()->dshotKbaud = 600;
	tap(SET_MINUS_X, SET_R_SPEED);
	checkInt("minus steps the bitrate down", settings()->dshotKbaud, 300);
	tap(SET_PLUS_X, SET_R_SPEED);
	tap(SET_PLUS_X, SET_R_SPEED);
	checkInt("plus steps it up", settings()->dshotKbaud, 1200);
	tap(SET_PLUS_X, SET_R_SPEED);
	checkInt("and wraps rather than sticking", settings()->dshotKbaud, 150);
	checkInt("the pump was told", fakeDshotKbaud(), 150);

	fakeDumpFrame("shot_setup.ppm");
}

/**
 * @brief KISS can only be switched on against a pin that can actually receive.
 *
 * On the 2.8" board there is exactly one such pin and it is also the default
 * ESC pin, so this is the case that decides whether the screen is honest or
 * merely tidy.
 */
static void testSetupKiss() {
	section("SETUP: KISS needs a pin of its own");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	enterSetup();

	// Force the clash the board can create, then let the screen resolve it.
	settings()->kissEnable = 0;
	settings()->kissPin = settings()->dshotPin;
	tap(SET_TOGGLE_X, SET_R_KISS);

	// This used to have a second outcome -- "the 2.8" has nowhere to put it, so
	// it stays off" -- because a KISS pin had to be one of the eight a hardware
	// UART can receive on, and that board frees exactly one of them: the pin
	// the ESC starts on. The receiver is a PIO state machine now, so every
	// board with two free pins can do this. @see pio_uart_rx.h
	checkInt("KISS comes on", settings()->kissEnable, 1);
	checkTrue("on a pin this board offers", settingsPinFree(settings()->kissPin));
	checkTrue("and not the ESC's", settings()->kissPin != settings()->dshotPin);

	// Stepping must not land back on the ESC's pin, however few the board
	// leaves free. On the 2.8" that is a two-pin board with both spoken for, so
	// the step has nowhere legal to go and stays put rather than colliding --
	// which is the one case where "only legal values can be selected" and "the
	// button does something" cannot both hold.
	for (int i = 0; i < 4; i++) {
		tap(SET_PLUS_X, SET_R_KISSPIN);
		checkTrue("stepping KISS never lands on the ESC pin",
		          settings()->kissPin != settings()->dshotPin);
		checkTrue("and never leaves the board's free set",
		          settingsPinFree(settings()->kissPin));
	}
	for (int i = 0; i < 4; i++) {
		tap(SET_MINUS_X, SET_R_PIN);
		checkTrue("and stepping the ESC pin avoids the KISS pin too",
		          settings()->dshotPin != settings()->kissPin);
		checkTrue("staying inside the free set as well",
		          settingsPinFree(settings()->dshotPin));
	}

	// Switching it off is always available.
	tap(SET_TOGGLE_X, SET_R_KISS);
	checkInt("toggling again switches it off", settings()->kissEnable, 0);
}

/** @brief Contrast and backlight take effect immediately and persist. */
static void testSetupDisplay() {
	section("SETUP: high contrast");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	enterSetup();

	checkTrue("starts in the dark theme", themeGet() == Theme::Dark);
	uint16_t darkBg = C_BG;

	tap(SET_TOGGLE_X, SET_R_CONTRAST);
	checkTrue("toggle switches the theme", themeGet() == Theme::HighContrast);
	checkTrue("the background inverted", C_BG != darkBg);
	checkTrue("text is now the darker of the two", C_TEXT < C_BG);
	checkTrue("strokes got heavier", themeStroke() > 1);
	checkTrue("text got heavier", themeBold());
	checkInt("backlight is forced to full", themeBacklight(100), 255);
	fakeDumpFrame("shot_setup_contrast.ppm");

	tap(SET_MINUS_X, SET_R_BACKLIGHT);
	checkTrue("the preferred level still moves", settings()->backlight < LCD_BACKLIGHT_DEFAULT);
	checkInt("but full brightness still wins while high contrast is on",
	         themeBacklight(settings()->backlight), 255);

	// The screen worth checking in high contrast is not this one: it is the
	// tester, which is what you are looking at outdoors with a motor running.
	// Rendering it here is what puts it in the published preview.
	tap(BACK_X, BACK_Y);
	frames(2);
	tap(BACK_X, BACK_Y);               // BACK, out of settings
	// Live data in both halves of the published pair. A dark screen full of
	// numbers next to a light screen reading "--" compares two different things.
	feedLiveTelemetry();
	frames(3);
	fakeDumpFrame("shot_tester_contrast.ppm");
	tap(BTN_CFG_X, BTN_ARM_Y);
	tap(BTN_SETUP_X, BTN_AM32_Y);
	frames(2);

	tap(SET_TOGGLE_X, SET_R_CONTRAST);
	checkTrue("toggling back restores the dark theme", themeGet() == Theme::Dark);
	checkInt("and the preferred backlight level",
	         themeBacklight(settings()->backlight), settings()->backlight);
	checkInt("dark theme draws single-pixel frames", themeStroke(), 1);
}

/**
 * @brief SAVE needs a full second held on the button, and nothing less.
 *
 * Same interlock as the AM32 write, for a weaker reason and a real one: a flash
 * erase parks core1, which stops the DShot pump. It is a thing to do on purpose.
 */
static void testSetupSave() {
	section("SETUP: hold to save");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	enterSetup();

	tap(SET_PLUS_X, SET_R_PIN);
	checkTrue("a change is pending", settingsDirty());

	// A tap is not a save.
	tap(SET_SAVE_X, SET_SAVE_Y);
	checkTrue("a tap does not commit", settingsDirty());
	checkTrue("and nothing was stored", !settingsStored());

	// Releasing early does not commit either.
	fakePress(SET_SAVE_X, SET_SAVE_Y); frames(1);
	for (int i = 0; i < 20; i++) { fakeHold(SET_SAVE_X, SET_SAVE_Y); frames(1); }
	fakeRelease(); frames(1);
	checkTrue("half a second does not commit", settingsDirty());

	// Moving off the button mid-hold cancels.
	fakePress(SET_SAVE_X, SET_SAVE_Y); frames(1);
	for (int i = 0; i < 20; i++) { fakeHold(SET_SAVE_X, SET_SAVE_Y); frames(1); }
	for (int i = 0; i < 40; i++) { fakeHold(SET_RESET_X, SET_SAVE_Y); frames(1); }
	fakeRelease(); frames(1);
	checkTrue("sliding off cancels the hold", settingsDirty());

	// The bar has to actually appear while holding, or the hold is invisible.
	uint32_t idle = fakeRegionHash(SAVE_BAR_X, SAVE_BAR_Y, 150, 3);
	fakePress(SET_SAVE_X, SET_SAVE_Y); frames(1);
	for (int i = 0; i < 20; i++) { fakeHold(SET_SAVE_X, SET_SAVE_Y); frames(1); }
	checkTrue("the hold draws a progress bar",
	          fakeRegionHash(SAVE_BAR_X, SAVE_BAR_Y, 150, 3) != idle);
	fakeRelease(); frames(2);
	checkTrue("and it clears on release",
	          fakeRegionHash(SAVE_BAR_X, SAVE_BAR_Y, 150, 3) == idle);

	// A full second does.
	uint8_t want = settings()->dshotPin;
	fakePress(SET_SAVE_X, SET_SAVE_Y); frames(1);
	for (int i = 0; i < 60; i++) { fakeHold(SET_SAVE_X, SET_SAVE_Y); frames(1); }
	checkTrue("a one-second hold commits", !settingsDirty());
	checkTrue("and reports a stored block", settingsStored());

	// Keeping the finger down must not save again a second later: each save is
	// a flash erase, and a long press is not a request for several.
	settings()->poles = (uint8_t)(settings()->poles == 12 ? 14 : 12);
	for (int i = 0; i < 120; i++) { fakeHold(SET_SAVE_X, SET_SAVE_Y); frames(1); }
	checkTrue("holding on does not save again", settingsDirty());
	fakeRelease(); frames(2);

	// Survives a reload, which is the only thing the user cares about.
	settings()->dshotPin = 0;
	settingsLoad();
	checkInt("the pin came back after a reload", settings()->dshotPin, want);

	// RESET restores compiled defaults into the live copy without touching flash.
	Settings def;
	settingsDefaults(&def);
	tap(SET_RESET_X, SET_SAVE_Y);
	checkInt("RESET restores the compiled ESC pin", settings()->dshotPin, def.dshotPin);
	checkTrue("RESET alone leaves flash as it was", settingsStored());

	// BACK returns to the settings screen.
	tap(BACK_X, BACK_Y);
	frames(2);
	checkTrue("BACK leaves SETUP", true);
	fakeDumpFrame("shot_settings_unsaved.ppm");
}


/**
 * @brief Sliding a finger off a button before lifting cancels it.
 *
 * The escape hatch. Every screen but the ARM button used to commit on
 * touch-down, which means a mis-tap has already happened by the time you see
 * it -- and on glass there is no travel to warn you first.
 */
static void testTapCancels() {
	section("Touch: a tap can be cancelled by sliding off");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);

	// HOLD is observable through the throttle gauge: with it on, the bar latches
	// instead of springing back. Arm, prove it springs back, then try to turn
	// HOLD on by a press that slides away -- it must still spring back.
	holdToArm();
	swipe(20, 160, THR_Y, 8);
	frames(2);
	checkInt("spring mode: released throttle returns to zero", fakeThrottle(), 0);

	fakePress(BTN_HOLD_X, BTN_ARM_Y); frames(2);
	fakeHold(BTN_ARM_X, BTN_ARM_Y);   frames(2);   // slide off, onto ARM
	fakeRelease();                    frames(2);
	swipe(20, 160, THR_Y, 8);
	frames(2);
	checkInt("sliding off HOLD never turned it on", fakeThrottle(), 0);

	// The same press, released in place, does toggle it.
	tap(BTN_HOLD_X, BTN_ARM_Y);
	swipe(20, 160, THR_Y, 8);
	frames(2);
	checkTrue("released in place, HOLD latched the throttle", fakeThrottle() > 0);
	tap(BTN_HOLD_X, BTN_ARM_Y);       // back off, which zeroes it
	tap(BTN_CFG_X, BTN_ARM_Y);        // into settings for the rest

	// The settings screen: press BEEP, slide off it, release. Neither fires.
	int beeps = fakeBeepRequests();
	fakePress(120, CFG_BEEP_Y);  frames(2);
	fakeHold(120, CFG_BEEP_Y + 60); frames(2);
	fakeRelease();               frames(2);
	checkInt("sliding off BEEP sent nothing", fakeBeepRequests(), beeps);

	// Still on the settings screen, which the next tap proves by working: the
	// row the finger slid onto is only hit-tested, so if the slide had
	// activated anything this fails.
	fakePress(120, CFG_BEEP_Y);  frames(2);
	fakeRelease();               frames(2);
	checkInt("released in place, it fires", fakeBeepRequests(), beeps + 1);
	checkTrue("so the screen never left", true);
}

/** @brief A held stepper repeats and accelerates; a tap moves exactly one step. */
static void testStepperRepeat() {
	section("Touch: hold-to-repeat on the settings steppers");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	tap(BTN_CFG_X, BTN_ARM_Y);

	// One tap, one step. The delay before repeating is what makes this true.
	uint16_t start = settings()->maxThrottle;
	tap(CFG_MAXT_P_X, CFG_MAXT_Y);
	checkInt("a tap moves exactly one step",
	         settings()->maxThrottle, start + MAX_THROTTLE_STEP);

	// Held: many steps, and the whole range is reachable without lifting.
	start = settings()->maxThrottle;
	fakePress(CFG_MAXT_P_X, CFG_MAXT_Y); frames(1);
	for (int i = 0; i < 200; i++) { fakeHold(CFG_MAXT_P_X, CFG_MAXT_Y); frames(1); }
	fakeRelease(); frames(1);
	checkTrue("holding walks the ceiling up", settings()->maxThrottle > start);
	checkInt("and reaches the top without lifting",
	         settings()->maxThrottle, MAX_THROTTLE_CEILING);
	checkTrue("never past it", settings()->maxThrottle <= MAX_THROTTLE_CEILING);

	// And back down, without underflowing past one step.
	fakePress(CFG_MAXT_M_X, CFG_MAXT_Y); frames(1);
	for (int i = 0; i < 300; i++) { fakeHold(CFG_MAXT_M_X, CFG_MAXT_Y); frames(1); }
	fakeRelease(); frames(1);
	checkInt("and back down to one step, never below",
	         settings()->maxThrottle, MAX_THROTTLE_STEP);

	// Poles stay even throughout, which is what the eRPM maths assumes.
	fakePress(CFG_POLES_P_X, CFG_POLES_Y); frames(1);
	bool even = true;
	for (int i = 0; i < 120; i++) {
		fakeHold(CFG_POLES_P_X, CFG_POLES_Y); frames(1);
		if (settings()->poles & 1) even = false;
	}
	fakeRelease(); frames(1);
	checkTrue("pole count stays even while repeating", even);
	checkTrue("and inside its range",
	          settings()->poles >= MIN_MOTOR_POLES && settings()->poles <= MAX_MOTOR_POLES);
}

/** @brief The repeat rule itself, away from any screen. */
static void testRepeatRule() {
	section("Touch: the repeat rule");

	Repeat r{};
	checkTrue("fires once immediately on press", repeatFires(&r, +1, 1000));
	checkTrue("then pauses", !repeatFires(&r, +1, 1100));
	checkTrue("still paused just before the delay",
	          !repeatFires(&r, +1, 1000 + REPEAT_DELAY_MS));
	checkTrue("repeats after the delay", repeatFires(&r, +1, 1000 + REPEAT_DELAY_MS + 130));

	// Accelerates: the gap between steps shrinks the longer it is held.
	checkInt("slow tier", repeatInterval(500), 120);
	checkInt("faster after 1.2 s", repeatInterval(1500), 60);
	checkInt("fastest after 2.5 s", repeatInterval(3000), 25);

	// Reversing restarts the acceleration rather than carrying it over.
	Repeat r2{};
	repeatFires(&r2, +1, 0);
	for (uint32_t t = 100; t < 4000; t += 20) repeatFires(&r2, +1, t);
	checkTrue("reversing fires at once", repeatFires(&r2, -1, 4000));
	checkTrue("and starts slow again", !repeatFires(&r2, -1, 4100));

	// Letting go resets.
	checkTrue("release is not a step", !repeatFires(&r2, 0, 4200));
	checkTrue("and the next press fires immediately", repeatFires(&r2, -1, 4300));
}

/** @brief Arming and disarming dip the backlight, so the panel announces them. */
static void testArmBacklightDip() {
	section("Touch: the panel announces arm and disarm");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(6);
	uint8_t resting = fakeBacklight();
	checkTrue("resting at the configured level", resting > 0);

	fakeBacklightResetMin();
	holdToArm();
	frames(1);
	checkTrue("arming dips the panel", fakeBacklightMin() < resting);

	frames(20);   // the dip is short
	checkInt("and comes back", fakeBacklight(), resting);

	fakeBacklightResetMin();
	tap(BTN_ARM_X, BTN_ARM_Y);          // DISARM fires on press
	frames(1);
	checkTrue("no longer armed", !uiArmed());
	checkTrue("disarming dips it too", fakeBacklightMin() < resting);
	frames(20);
	checkInt("and comes back again", fakeBacklight(), resting);
}


/**
 * @brief The interlocks that stand between a tap and a spinning motor.
 *
 * Every check here corresponds to a mutation that used to survive the entire
 * suite: shortening ARM_HOLD_MS to a millisecond, forcing the zero-throttle
 * precondition true, dropping the check that the press began on the button, and
 * deleting the disarmed-throttle backstop outright.
 */
static void testArmInterlocks() {
	section("Safety: arming takes a deliberate hold");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(4);

	// A tap is not a hold. Nothing in the suite asserted this, which is how
	// ARM_HOLD_MS could be reduced to 1 ms without a single check noticing.
	tap(BTN_ARM_X, BTN_ARM_Y);
	checkTrue("a tap does not arm", !uiArmed());
	checkTrue("nor does the ESC think so", !fakeArmed());

	// Nor is most of a hold.
	fakePress(BTN_ARM_X, BTN_ARM_Y); frames(1);
	for (int i = 0; i < 30; i++) { fakeHold(BTN_ARM_X, BTN_ARM_Y); frames(1); }
	checkTrue("750 ms of holding does not arm", !uiArmed());
	fakeRelease(); frames(2);
	checkTrue("and releasing early does not either", !uiArmed());

	// Sliding onto the button from elsewhere must not arm, however long it is
	// then held: the press has to have begun on it.
	fakePress(BTN_CFG_X - 40, BTN_ARM_Y - 60); frames(1);
	for (int i = 0; i < 80; i++) { fakeHold(BTN_ARM_X, BTN_ARM_Y); frames(1); }
	checkTrue("sliding onto ARM and holding does not arm", !uiArmed());
	fakeRelease(); frames(2);

	// The real thing does.
	holdToArm();
	checkTrue("a full hold arms", uiArmed());
	checkTrue("and the ESC is told", fakeArmed());
}

/**
 * @brief DISARM. Never once pressed by the suite before.
 *
 * Every path out of the armed state went through CFG, so the incidental disarm
 * was covered and the deliberate one was not: replacing the button's body with
 * nothing, or inverting it so that pressing DISARM re-armed, both passed.
 */
static void testDisarmButton() {
	section("Safety: the DISARM button");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(4);
	holdToArm();
	checkTrue("armed to begin with", uiArmed());

	// Put some throttle on and make it stay, so the zeroing is observable.
	// HOLD latches it; in spring mode the release at the end of a swipe would
	// have already returned it to zero and the check below would prove nothing.
	tap(BTN_HOLD_X, BTN_ARM_Y);
	swipe(20, 120, THR_Y, 8);
	frames(2);
	checkTrue("throttle is latched up", fakeThrottle() > 0);

	// Disarm fires on press, not release: this is the one control that must not
	// wait for a lift.
	fakePress(BTN_ARM_X, BTN_ARM_Y); frames(1);
	checkTrue("disarms on press, without waiting for release", !uiArmed());
	fakeRelease(); frames(2);

	checkTrue("the ESC is told", !fakeArmed());
	checkInt("and the commanded throttle is zeroed", fakeThrottle(), 0);
	checkInt("the UI agrees", uiThrottle(), 0);

	// HOLD is dropped too, so re-arming cannot resume a latched throttle.
	holdToArm();
	swipe(20, 120, THR_Y, 8);
	frames(2);
	checkInt("re-arming does not restore a latched throttle", fakeThrottle(), 0);
}

/**
 * @brief The throttle ceiling binds in relative mode, not just absolute.
 *
 * Deleting the upper clamp in relativeThrottle() outright used to pass: only
 * the lower rail was tested, and only absolute mode's ceiling.
 */
static void testCeilingBindsInRelativeMode() {
	section("Safety: the ceiling binds however the throttle is driven");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(4);
	holdToArm();

	uint16_t ceiling = settings()->maxThrottle;

	// HOLD mode: the gauge is relative. Swipe far past the right-hand end,
	// several times, and it must sit exactly on the ceiling.
	tap(BTN_HOLD_X, BTN_ARM_Y);
	for (int i = 0; i < 4; i++) swipe(10, 235, THR_Y, 6);
	frames(2);
	checkTrue("HOLD mode never exceeds the ceiling", fakeThrottle() <= ceiling);
	checkInt("and reaches it", fakeThrottle(), ceiling);

	// The pad: always relative, in either mode. Swipe up the full height
	// repeatedly.
	for (int i = 0; i < 6; i++) {
		fakePress(120, 220); frames(1);
		for (int y = 220; y >= 40; y -= 10) { fakeHold(120, y); frames(1); }
		fakeRelease(); frames(1);
	}
	checkTrue("the pad never exceeds it either", fakeThrottle() <= ceiling);
	checkInt("and reaches it too", fakeThrottle(), ceiling);

	// Lowering the ceiling while a latched throttle is above it must not leave
	// the motor above the new limit.
	tap(BTN_CFG_X, BTN_ARM_Y);
	checkTrue("entering settings disarmed", !uiArmed());
	checkInt("which zeroed the throttle", fakeThrottle(), 0);
}

/** @brief The disarmed-throttle backstop, independent of every other rule. */
static void testDisarmedThrottleBackstop() {
	section("Safety: no throttle while disarmed, by any route");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(4);

	// Never armed: every throttle surface must be inert.
	swipe(20, 200, THR_Y, 8);
	frames(2);
	checkInt("the gauge does nothing while disarmed", fakeThrottle(), 0);

	fakePress(120, 220); frames(1);
	for (int y = 220; y >= 40; y -= 10) { fakeHold(120, y); frames(1); }
	fakeRelease(); frames(2);
	checkInt("nor does the pad", fakeThrottle(), 0);
	checkInt("nor is one commanded", uiThrottle(), 0);
}

/**
 * @brief What core1 puts on the wire, including the interlocks core0 cannot see.
 *
 * escFrameAction() is pure precisely so these can be reached: esc_task.cpp
 * cannot be linked here, and the heartbeat timeout in particular had no
 * coverage of any kind -- deleting escHeartbeat()'s call site passed the suite.
 */
static void testFrameAction() {
	section("Safety: core1's frame decision");

	// Armed, alive, throttle commanded: it goes out.
	EscFrame f = escFrameAction(true, true, 1500, 0, 0, false);
	checkTrue("armed and alive sends the throttle", !f.sendCommand);
	checkInt("at the commanded value", f.throttle, 1500);

	// The heartbeat backstop. This is the interlock that does not need core0 to
	// be well enough to act, and it was untested.
	f = escFrameAction(true, false, 1500, 0, 0, false);
	checkInt("a dead UI forces the throttle to zero", f.throttle, 0);
	checkTrue("still a throttle frame, not silence", !f.sendCommand);

	// Disarmed means zero whatever was last commanded.
	f = escFrameAction(false, true, 1500, 0, 0, false);
	checkInt("disarmed forces zero", f.throttle, 0);

	// Commands go out only while disarmed, and are not consumed while armed --
	// a queued command must not be eaten by a frame that cannot carry it.
	f = escFrameAction(false, true, 0, 42, 6, false);
	checkTrue("a queued command is sent while disarmed", f.sendCommand);
	checkInt("and it is the queued one", f.command, 42);

	f = escFrameAction(true, true, 800, 42, 6, false);
	checkTrue("armed, the command waits rather than going out", !f.sendCommand);
	checkInt("and the throttle goes instead", f.throttle, 800);

	// KISS requests never ride alongside a command.
	f = escFrameAction(false, true, 0, 0, 0, true);
	checkTrue("a due KISS request is made", f.requestKiss);
	f = escFrameAction(false, true, 0, 42, 6, true);
	checkTrue("but never during a command sequence", !f.requestKiss);
}

/** @brief The automatic EDT enable, at the boundaries the UI cares about. */
static void testEdtAutoRule() {
	section("Safety: the automatic EDT enable");

	const uint32_t RETRY = 1000;
	checkTrue("an ESC that just appeared is sent one",
	          edtAutoAction(true, false, true, false, 0, RETRY) == EdtAutoAction::Send);
	checkTrue("and asked again if it did not take",
	          edtAutoAction(true, false, true, true, RETRY, RETRY) == EdtAutoAction::Send);
	checkTrue("but left alone once EDT is actually arriving",
	          edtAutoAction(true, true, true, true, RETRY, RETRY) == EdtAutoAction::None);
	checkTrue("losing the link re-arms it for the next ESC",
	          edtAutoAction(false, false, true, true, 0, RETRY) == EdtAutoAction::Rearm);
	checkTrue("no link and nothing sent is nothing to do",
	          edtAutoAction(false, false, true, false, 0, RETRY) == EdtAutoAction::None);
	// Armed, mid-burst, or a prop still turning: no attempt is made and, just as
	// importantly, none is recorded -- so the retry clock is not started by a
	// frame the ESC was never going to act on.
	checkTrue("an ESC that cannot execute a command is not sent one",
	          edtAutoAction(true, false, false, false, 0, RETRY) == EdtAutoAction::None);
}


/** @brief The idle interlock actually fires, and says so first. */
static void testIdleAutoDisarm() {
	section("Safety: idle auto-disarm");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(4);
	holdToArm();
	checkTrue("armed", uiArmed());

	// Not yet: well inside the window, with no touches at all.
	fakeAdvance(IDLE_DISARM_MS / 2);
	frames(2);
	checkTrue("still armed halfway through the window", uiArmed());

	// The countdown appears before it acts. A motor stopping unannounced is
	// indistinguishable from a fault.
	auto statusBar = []() { return fakeRegionHash(120, 0, 120, 26); };
	uint32_t quiet = statusBar();
	fakeAdvance(IDLE_DISARM_MS / 2 - 3000);
	frames(2);
	checkTrue("the status bar warns before it disarms", statusBar() != quiet);
	checkTrue("and is still armed while warning", uiArmed());

	// Past the window: disarmed, and the ESC told.
	fakeAdvance(4000);
	frames(2);
	checkTrue("idle disarms", !uiArmed());
	checkTrue("and the ESC is told", !fakeArmed());
	checkInt("throttle zeroed", fakeThrottle(), 0);

	// Touching resets the timer, so an operator who is present is not disarmed.
	holdToArm();
	for (int i = 0; i < 3; i++) {
		fakeAdvance(IDLE_DISARM_MS - 2000);
		frames(2);
		tap(BTN_HOLD_X, BTN_ARM_Y);      // any touch counts
		tap(BTN_HOLD_X, BTN_ARM_Y);      // and back off
	}
	checkTrue("touching keeps it armed", uiArmed());
}

/** @brief The pole count is clamped at both ends, and reaches the ESC. */
static void testPoleCountReachesEsc() {
	section("Safety: pole count");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	tap(BTN_CFG_X, BTN_ARM_Y);

	checkInt("the ESC starts with the stored count", fakePoles(), settings()->poles);

	tap(CFG_POLES_P_X, CFG_POLES_Y);
	checkInt("a step reaches the ESC", fakePoles(), settings()->poles);

	// All the way down: the lower clamp is the one nothing tested, and the
	// eRPM conversion divides by poles/2.
	fakePress(CFG_POLES_M_X, CFG_POLES_Y); frames(1);
	for (int i = 0; i < 200; i++) { fakeHold(CFG_POLES_M_X, CFG_POLES_Y); frames(1); }
	fakeRelease(); frames(1);
	checkInt("never below the minimum", settings()->poles, MIN_MOTOR_POLES);
	checkInt("and the ESC has that too", fakePoles(), MIN_MOTOR_POLES);

	fakePress(CFG_POLES_P_X, CFG_POLES_Y); frames(1);
	for (int i = 0; i < 200; i++) { fakeHold(CFG_POLES_P_X, CFG_POLES_Y); frames(1); }
	fakeRelease(); frames(1);
	checkInt("never above the maximum", settings()->poles, MAX_MOTOR_POLES);
	checkInt("and the ESC has that too", fakePoles(), MAX_MOTOR_POLES);
}

/**
 * @brief The RPM readout goes dead when the ESC stops answering.
 *
 * Deliberately its own test: testTelemetryExpires() excludes the RPM band from
 * every comparison, because the fake serves a fixed packet rate that would mask
 * the tiles. So nothing looked at the readout, and hardcoding telemetryAlive()
 * to true -- which removes NO TELEMETRY and the dead-digit colour entirely --
 * passed the suite.
 */
static void testRpmGoesDead() {
	section("Safety: the RPM readout reports a dead link");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);

	auto rpmBand = []() { return fakeRegionHash(0, 27, 240, 100); };

	feedLiveTelemetry();
	frames(2);
	uint32_t live = rpmBand();

	// Past ESC_LINK_STALE_MS with nothing new arriving.
	fakeAdvance(ESC_LINK_STALE_MS + 200);
	frames(2);
	checkTrue("a dead link changes the RPM readout", rpmBand() != live);

	// And it comes back.
	feedLiveTelemetry();
	frames(2);
	checkTrue("and a live one restores it exactly", rpmBand() == live);
}

/** @brief Arming starts a log by itself, which is the shipped default. */
static void testAutoLogOnArm() {
	section("Safety: logging follows the arm state");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);

	SdLogStatus st;
	memset(&st, 0, sizeof(st));
	st.state = SdLogState::Idle;          // a card is present and mounted
	fakeSdLogSet(&st);
	frames(2);
	checkTrue("not logging before arming", !sdLogActive());

	holdToArm();
	frames(2);
	// The existing manual-log test always hand-starts a log first, so
	// SD_LOG_AUTO_ON_ARM -- the default -- had no positive test at all.
	checkTrue("arming starts a log by itself", sdLogActive());

	tap(BTN_ARM_X, BTN_ARM_Y);            // DISARM
	frames(2);
	checkTrue("and disarming stops it", !sdLogActive());
}

/** @brief Hit-testing is half-open: a button's far edge belongs to its neighbour. */
static void testHitBoundaries() {
	section("Safety: button edges");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	tap(BTN_CFG_X, BTN_ARM_Y);

	// BTN_AM32 spans x 14..81; BTN_LOG starts at 86. x=82 is in the gap between
	// them and must hit neither -- an inclusive far edge would make adjacent
	// buttons overlap, and this row has three of them.
	int beeps = fakeBeepRequests();
	uint16_t polesBefore = settings()->poles;
	tap(82, BTN_AM32_Y);
	frames(2);
	checkInt("a tap in the gap changes nothing", settings()->poles, polesBefore);
	checkInt("and sends nothing", fakeBeepRequests(), beeps);

	// BTN_POLES_M spans x 14..59, y 72..111. One past each far edge is outside.
	tap(60, CFG_POLES_Y);
	checkInt("one past the right edge misses", settings()->poles, polesBefore);
	tap(CFG_POLES_M_X, 112);
	checkInt("one past the bottom edge misses", settings()->poles, polesBefore);

	// And the last pixel inside still hits.
	tap(59, 111);
	checkTrue("the last pixel inside still hits", settings()->poles != polesBefore);
}

/**
 * @brief Reversing a relative drag at a rail responds immediately.
 *
 * The classic relative-control failure: without re-anchoring, travel past an
 * end keeps accumulating and reversing does nothing until all of that phantom
 * distance has been unwound.
 */
static void testRailReAnchor() {
	section("Safety: relative drags re-anchor at the rails");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(4);
	holdToArm();
	tap(BTN_HOLD_X, BTN_ARM_Y);           // HOLD: the gauge is relative

	uint16_t ceiling = settings()->maxThrottle;

	// Get to the ceiling first, and let go. One track width is exactly one
	// ceiling of travel at 100 % sensitivity, so this leaves the throttle
	// pinned with no overshoot yet.
	fakePress(5, THR_Y); frames(1);
	for (int x = 5; x <= 235; x += 5) { fakeHold(x, THR_Y); frames(1); }
	fakeRelease(); frames(1);
	checkInt("pinned at the ceiling", fakeThrottle(), ceiling);

	// Now a second full-width drag from a throttle that is *already* at the
	// rail. That is what generates real overshoot: without re-anchoring, the
	// anchor keeps a whole ceiling of phantom travel, and reversing has to
	// unwind all of it before anything moves. Starting from below the rail --
	// which is what this test did at first -- overshoots by a single unit and
	// proves nothing.
	fakePress(5, THR_Y); frames(1);
	for (int x = 5; x <= 235; x += 5) { fakeHold(x, THR_Y); frames(1); }
	checkInt("still exactly at the ceiling", fakeThrottle(), ceiling);
	for (int x = 235; x >= 205; x -= 5) { fakeHold(x, THR_Y); frames(1); }
	fakeRelease(); frames(1);
	checkTrue("a small reverse responds at once", fakeThrottle() < ceiling);
	checkTrue("and not by collapsing to zero", fakeThrottle() > 0);

	// Same at the bottom rail. The full width of the track, because at 100 %
	// sensitivity that is exactly one ceiling of travel -- a shorter drag from a
	// high starting throttle never reaches the rail at all, and would be testing
	// nothing.
	fakePress(235, THR_Y); frames(1);
	for (int x = 235; x >= 5; x -= 5) { fakeHold(x, THR_Y); frames(1); }
	for (int i = 0; i < 4; i++) { fakeHold(5, THR_Y); frames(1); }
	checkInt("pinned at zero", fakeThrottle(), 0);
	for (int x = 5; x <= 40; x += 5) { fakeHold(x, THR_Y); frames(1); }
	fakeRelease(); frames(1);
	checkTrue("a small push off the bottom responds at once", fakeThrottle() > 0);
}


/**
 * @brief The board: detected at boot, and not offered as a choice here.
 *
 * The picker this replaces only ever appeared on a unified build. What is left
 * holds on every image, which is why `make both` runs it for all three: there
 * is nothing on this screen that can change which board the firmware drives.
 */
static void testBoardSelection() {
	section("Board: selection");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);

	checkTrue("a board is always selected", g_board != nullptr);
	checkTrue("with a label", g_board->label && g_board->label[0]);
	checkTrue("and at least one free GPIO", g_board->freeGpioMask != 0);
	checkTrue("whose default ESC pin is one of them",
	          settingsPinFree(g_board->defaultDshotPin));

	// Every board this image carries must be internally consistent. A
	// descriptor that names a pin outside its own free mask would make the
	// SETUP screen's steppers unable to return to their starting point.
	for (int i = 0; i < boardCount(); i++) {
		const BoardDesc *b = boardAt(i);
		checkTrue("descriptor is present", b != nullptr);
		if (!b) continue;
		checkTrue("its default ESC pin is in its own free mask",
		          (b->freeGpioMask >> b->defaultDshotPin) & 1u);
		checkTrue("it names a touch driver", b->touch && b->touch->init && b->touch->poll);
		checkTrue("and an id that selects it", boardSelect(boardIdAt(i)));
	}
	boardSelect(settings()->boardId ? settings()->boardId : boardIdAt(0));

	// --- the board is not a setting, on any image ---
	//
	// It used to be, on a unified one: a toggle on this row cycled the choice,
	// and a save persisted it and rebooted into it. Picking the wrong board that
	// way built the next boot's display from the wrong pins, and the screen that
	// could have put it back was the screen that no longer came up -- leaving a
	// reflash over USB as the only way in. The row is read-only now, and these
	// are the assertions that keep it that way.
	enterSetup();
	uint8_t live     = boardId();
	uint8_t recorded = settings()->boardId;
	uint8_t pumpPin  = escTaskDshotPin();
	int configures   = fakeConfigureCount();

	tap(SET_TOGGLE_X, SET_R_BOARD);
	checkInt("tapping the board row changes no choice",
	         settings()->boardId, recorded);
	checkInt("nor the live board", boardId(), live);
	checkInt("nor the pump's pin", escTaskDshotPin(), pumpPin);
	checkInt("nor rebuilds the pump at all", fakeConfigureCount(), configures);

	// Held, too. The row carries no control at all, so neither the tap path nor
	// the held-repeat path has anything to reach.
	fakePress(SET_TOGGLE_X, SET_R_BOARD); frames(1);
	for (int i = 0; i < 40; i++) { fakeHold(SET_TOGGLE_X, SET_R_BOARD); frames(1); }
	fakeRelease(); frames(2);
	checkInt("holding it does nothing either", settings()->boardId, recorded);
	checkInt("and the board is still this one", boardId(), live);

	// And no save reboots. platReboot() is what a saved board change used to
	// call; this counter staying still is what fails if anything on this screen
	// ever starts re-pointing the hardware again.
	int reboots = fakeRebootCount();
	tap(SET_PLUS_X, SET_R_PIN);
	fakePress(SET_SAVE_X, SET_SAVE_Y); frames(1);
	for (int i = 0; i < 60; i++) { fakeHold(SET_SAVE_X, SET_SAVE_Y); frames(1); }
	fakeRelease(); frames(2);
	checkTrue("an ordinary save lands", !settingsDirty());
	checkInt("and no save reboots", fakeRebootCount() - reboots, 0);
	checkInt("the saved block records the live board",
	         settings()->boardId, live);

	tap(BACK_X, BACK_Y);
	frames(2);

	fakeFlashClear();
	settingsLoad();
	boardSelect(boardIdAt(0));
}

/**
 * @brief The armed-save backstop: flash is never erased over a live motor.
 *
 * A save parks core1 for the erase, which stalls the DShot pump; a spinning
 * ESC would time out and cut. CFG force-disarms, so the setup screen cannot be
 * reached armed through the UI today -- which is exactly why the backstop is
 * driven directly here: the day that routing changes is not a day anyone will
 * remember this rule, but this test will.
 */
static void testArmedSaveRefused() {
	section("SETUP: a save while armed is refused");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	holdToArm();
	checkTrue("armed to begin with", uiArmed());

	settings()->poles = (uint8_t)(settings()->poles == 12 ? 14 : 12);
	checkTrue("a change is pending", settingsDirty());

	uiSetupEnter();
	int writes = fakeFlashWrites();
	int reboots = fakeRebootCount();

	TouchState t;
	memset(&t, 0, sizeof(t));
	t.down = true; t.pressed = true;
	t.x = t.downX = SET_SAVE_X;
	t.y = t.downY = SET_SAVE_Y;
	uiSetupTick(&t);
	t.pressed = false;
	for (int i = 0; i < 60; i++) { fakeAdvance(UI_FRAME_MS); uiSetupTick(&t); }

	checkTrue("still armed afterwards", uiArmed());
	checkTrue("the save was refused", settingsDirty());
	checkTrue("nothing was stored", !settingsStored());
	checkInt("flash was never touched", fakeFlashWrites() - writes, 0);
	checkInt("and nothing rebooted", fakeRebootCount() - reboots, 0);

	t.down = false; t.released = true;
	uiSetupTick(&t);

	// Disarm on the main screen and leave the module clean.
	fakePress(BTN_ARM_X, BTN_ARM_Y); frames(1);
	fakeRelease(); frames(2);
	checkTrue("disarmed again", !uiArmed());
	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
}

/**
 * @brief A held control looks pressed, and stops looking pressed.
 *
 * The second half is the regression: the release frame used to change no
 * cached value, so nothing repainted and the pressed double-frame survived
 * until something else happened to invalidate the screen.
 */
static void testPressedLookClears() {
	section("Buttons: the pressed look appears, and clears on release");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);
	tap(BTN_CFG_X, BTN_ARM_Y);           // to the settings screen
	frames(2);

	// BTN_POLES_M = { 14, 72, 46, 40 } in ui.cpp. The pole *value* is drawn
	// outside this rectangle, so the hash isolates the button's own look.
	uint32_t idle = fakeRegionHash(14, 72, 46, 40);
	fakePress(CFG_POLES_M_X, CFG_POLES_Y); frames(1);
	checkTrue("a held stepper draws its pressed state",
	          fakeRegionHash(14, 72, 46, 40) != idle);
	fakeRelease(); frames(1);
	checkTrue("and returns to rest on release",
	          fakeRegionHash(14, 72, 46, 40) == idle);

	// The same rule on SETUP, which never had pressed states at all:
	// the ESC-pin '-' button is at { BTN_M_X 148, R_PIN 62, 40, 24 }.
	tap(BTN_SETUP_X, BTN_AM32_Y);
	frames(2);
	idle = fakeRegionHash(148, 62, 40, 24);
	fakePress(SET_MINUS_X, SET_R_PIN); frames(1);
	checkTrue("SETUP's stepper draws its pressed state",
	          fakeRegionHash(148, 62, 40, 24) != idle);
	fakeRelease(); frames(1);
	checkTrue("and returns to rest on release",
	          fakeRegionHash(148, 62, 40, 24) == idle);

	tap(BACK_X, BACK_Y);
	frames(2);
	tap(120, 309);                       // CFG BACK, to the tester screen
	frames(2);
}

/**
 * @brief Screen changes leave nothing behind.
 *
 * The companion to the compile-time tiling asserts in ui.cpp: those prove
 * every row belongs to a region, this proves the transitions blank what the
 * incoming screen does not cover.
 */
static void testScreenChangeResidue() {
	section("Screen changes leave nothing behind");

	fakeFlashClear();
	settingsLoad();
	uiInit();
	frames(2);

	// The tester screen legitimately shows some cyan of its own -- KISS source
	// tags, the EDT chip -- so the assertion is against its *own* baseline: a
	// round trip through the cyan-rich settings screen must not add any.
	int before = fakeCountColour(C_CYAN);

	tap(BTN_CFG_X, BTN_ARM_Y);           // into the settings screen
	frames(2);
	int onSettings = fakeCountColour(C_CYAN);
	checkTrue("the settings screen shows more cyan", onSettings > before);

	tap(BACK_X, BACK_Y);                 // BACK, to the tester screen
	frames(3);
	int after = fakeCountColour(C_CYAN);
	checkTrue("no settings cyan survives on the tester screen",
	          after <= before);
}

void runUiTests() {
	// Off zero before anything is stamped. Virtual time starts at 0, and 0 is
	// reserved for "this frame type has never arrived" -- so telemetry stamped
	// at t=0 is correctly treated as absent, and every screenshot below would
	// render an ESC that had said nothing.
	fakeAdvance(1000);

	feedLiveTelemetry();

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
		feedLiveTelemetry(); frames(2);   // the arm hold outlasted the fixture
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
		// One more frame before the dump. The tap that navigates is handled by
		// the *outgoing* screen's handler, so the incoming one is not painted
		// until the following tick -- and this screenshot, which docs/ publishes,
		// was of the tester screen for as long as it has existed.
		frames(2);
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
		tap(BACK_X, BACK_Y);                   // leave AM32, back to SETTINGS
		checkTrue("DShot pin handed back", fakePinReturned());
		// And back to the main screen, which is what the sections after this
		// one assume. It used to be left on SETTINGS and get away with it: the
		// settings nav row stopped at y=293, so enterLogScreen()'s first tap at
		// y=299 fell into dead space and was harmlessly ignored. The row is
		// 48 px tall now and that tap lands on SETUP.
		tap(BACK_X, BACK_Y);
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
	testManualLogSurvivesArming();
	testKissDisplay();
	testTelemetryExpires();
	testSettingsCommandRow();

	testBoardSelection();
	testArmInterlocks();
	testIdleAutoDisarm();
	testPoleCountReachesEsc();
	testRpmGoesDead();
	testAutoLogOnArm();
	testHitBoundaries();
	testRailReAnchor();
	testDisarmButton();
	testCeilingBindsInRelativeMode();
	testDisarmedThrottleBackstop();
	testFrameAction();
	testEdtAutoRule();

	testTapCancels();
	testStepperRepeat();
	testRepeatRule();
	testArmBacklightDip();

	testSetupWiring();
	testSetupKiss();
	testSetupDisplay();
	testSetupSave();
	testArmedSaveRefused();
	testPressedLookClears();
	testScreenChangeResidue();

	// Back to a known state for anything that runs after this.
	fakeFlashClear();
	settingsLoad();
	themeSet(Theme::Dark);

}
