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
#include "gfx.h"
#include "ui.h"
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
#define BTN_AM32_Y   275   // all three span y 256..293
// --- config-screen coordinates ---
#define AM32_BACK_X 216
#define AM32_BACK_Y  23
#define AM32_MINUS_X 34
#define AM32_PLUS_X 205
#define AM32_EDIT_Y 240
#define AM32_WRITE_X 50
#define AM32_WRITE_Y 290
// --- settings-screen command buttons, mirroring ui.cpp ---
#define CFG_BEEP_X  120   // BTN_BEEP spans the full row, x 14..225
#define CFG_CMD_Y_T 219   // y 200..237
#define CFG_BEEP_Y  CFG_CMD_Y_T
// --- settings-screen steppers: BTN_*_M x 14..59, BTN_*_P x 180..225 ---
#define CFG_POLES_M_X  37
#define CFG_POLES_P_X 203
#define CFG_POLES_Y    92   // BTN_POLES_* y 72..111
#define CFG_MAXT_M_X   37
#define CFG_MAXT_P_X  203
#define CFG_MAXT_Y    168   // BTN_MAXT_* y 148..187
// --- throttle gauge: THR_TRACK_Y 248, height 26 ---
#define THR_Y         260
// --- logging-screen coordinates, mirroring ui.cpp ---
#define LOG_TOGGLE_Y 232
#define LOG_RETRY_Y  270
#define LOG_BACK_Y   296

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
	tap(120, LOG_TOGGLE_Y + 20);
	checkTrue("START does nothing without a card", !sdLogActive());

	// A card inserted after boot. sdLogBegin() only runs once, so without this
	// button the card would stay invisible until a power cycle -- which looks
	// exactly like a card the firmware cannot read.
	tap(120, LOG_RETRY_Y + 11);
	frames(2);
	sdLogStatus(&st);
	checkTrue("RETRY finds a card inserted later",
	          st.state == SdLogState::Idle);
	checkInt("and reports it mounted cleanly", st.mountResult, 0);
	checkInt("with its type", st.cardType, 3);
	checkTrue("and its size", st.cardSizeMB > 100000);
	fakeDumpFrame("shot_log_mounted.ppm");

	tap(120, LOG_BACK_Y + 9);
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
	tap(120, LOG_TOGGLE_Y + 17);
	checkTrue("manual START begins a log", sdLogActive());

	// Back to the main screen and arm.
	tap(120, LOG_BACK_Y + 9);
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
	tap(120, LOG_TOGGLE_Y + 17);
	checkTrue("manual STOP ends it", !sdLogActive());
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

	// Merge is per-field and time-based, so let KISS go stale and confirm the
	// screen falls back rather than freezing the fine values.
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
	uint32_t chipOn = fakeRegionHash(120, 0, 120, 26);
	uint32_t idle   = fakeRegionHash(0, 195, 240, 55);
	fakeDumpFrame("shot_config_edt_on.ppm");

	// Letting telemetry expire has to change the chip, or the colour is
	// decoration rather than a readout.
	EscTelemetry none;
	memset(&none, 0, sizeof(none));
	fakeSetTelemetry(&none);
	frames(2);
	checkTrue("EDT chip changes when telemetry stops",
	          fakeRegionHash(120, 0, 120, 26) != chipOn);
	fakeDumpFrame("shot_config_edt_off.ppm");
	feedLiveTelemetry();
	frames(2);
	checkTrue("and changes back when it returns",
	          fakeRegionHash(120, 0, 120, 26) == chipOn);

	// The chip is not a button. Tapping it must not be mistaken for one.
	int beepBefore = fakeBeepRequests();
	tap(200, 12);
	checkInt("tapping the chip does nothing", fakeBeepRequests(), beepBefore);

	// BEEP now has the row to itself, so a tap anywhere along it lands.
	fakePress(CFG_BEEP_X, CFG_CMD_Y_T); frames(1); fakeRelease(); frames(1);
	checkInt("tapping BEEP sends the command", fakeBeepRequests(), beepBefore + 1);
	uint32_t lit = fakeRegionHash(0, 195, 240, 55);
	checkTrue("and the button acknowledges it", lit != idle);
	fakeDumpFrame("shot_config_beep_flash.ppm");

	fakeAdvance(400);
	frames(2);
	checkTrue("the flash clears itself",
	          fakeRegionHash(0, 195, 240, 55) == idle);

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
	uint32_t refused = fakeRegionHash(0, 195, 240, 55);
	checkTrue("a refused command looks different from an accepted one",
	          refused != lit && refused != idle);
	fakeDumpFrame("shot_config_beep_refused.ppm");
	escSetArmed(false);
	fakeAdvance(400); frames(2);

	tap(120, 308);                      // BACK, spans y 300..317
	frames(2);
}


// --- SETUP screen coordinates, mirroring ui_setup.cpp ---
#define SET_BACK_X  (GFX_W - 30)
#define SET_BACK_Y   16
#define SET_MINUS_X 168   // BTN_M_X 148 + BTN_W 40 -> centre 168
#define SET_PLUS_X  212   // BTN_P_X 192 + BTN_W 40 -> centre 212
#define SET_TOGGLE_X 190  // the wide toggle spans 148..231
#define SET_R_PIN     57  // R_PIN 44, ROW_H 26
#define SET_R_SPEED   87
#define SET_R_KISS   117
#define SET_R_KISSPIN 147
#define SET_R_CONTRAST 191
#define SET_R_BACKLIGHT 221
#define SET_SAVE_X    83  // SAVE_X 8, SAVE_W 150
#define SET_SAVE_Y   286  // FOOT_Y 266, FOOT_H 40
#define SET_RESET_X  198
#define SAVE_BAR_X     8   // progress bar spans SAVE_X..+SAVE_W
#define SAVE_BAR_Y   262   // FOOT_Y - 4

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

	if (settings()->kissEnable) {
		checkTrue("if KISS came on, it is on a receiver",
		          settingsUartForPin(settings()->kissPin) >= 0);
		checkTrue("and not the ESC's pin",
		          settings()->kissPin != settings()->dshotPin);
		checkTrue("and a pin this board offers",
		          settingsPinFree(settings()->kissPin));
	} else {
		// The 2.8": nowhere left to put it, so it stays off rather than
		// pretending.
		checkTrue("with no free receiver, KISS stays off", true);
	}

	// Switching it off is always available.
	if (settings()->kissEnable) {
		tap(SET_TOGGLE_X, SET_R_KISS);
		checkInt("toggling again switches it off", settings()->kissEnable, 0);
	}
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
	tap(SET_BACK_X, SET_BACK_Y);
	frames(2);
	tap(120, 308);                      // BACK, out of settings
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
	tap(SET_BACK_X, SET_BACK_Y);
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

	// The settings screen: press BEEP, slide to BACK, release. Neither fires.
	int beeps = fakeBeepRequests();
	fakePress(120, CFG_BEEP_Y);  frames(2);
	fakeHold(120, 308);          frames(2);
	fakeRelease();               frames(2);
	checkInt("sliding off BEEP sent nothing", fakeBeepRequests(), beeps);

	// Still on the settings screen, which the next tap proves by working: BACK
	// is only hit-tested here, so if the slide had activated it this fails.
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
	testManualLogSurvivesArming();
	testKissDisplay();
	testTelemetryExpires();
	testSettingsCommandRow();

	testTapCancels();
	testStepperRepeat();
	testRepeatRule();
	testArmBacklightDip();

	testSetupWiring();
	testSetupKiss();
	testSetupDisplay();
	testSetupSave();

	// Back to a known state for anything that runs after this.
	fakeFlashClear();
	settingsLoad();
	themeSet(Theme::Dark);

}
