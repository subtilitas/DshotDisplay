/**
 * @file test_settings.cpp
 * @brief Persistence: defaults, validation, and what a bad block does.
 *
 * The rules under test here are the ones that decide what a board does with a
 * flash sector it cannot trust. Every one of them is written to fail towards
 * the compiled defaults, and each check below is the assertion that it does —
 * because the field this most matters for is the throttle ceiling, and the
 * failure mode of getting it wrong is a motor at full power.
 */

#include "check.h"
#include "fakes.h"

#include "settings.h"
#include "config.h"
#include "board_desc.h"

#include <string.h>
#include <stddef.h>

/** @brief Write @p s to the fake flash as a valid, correctly-CRC'd block. */
static void storeValid(const Settings *s) {
	SettingsBlock blk;
	memset(&blk, 0, sizeof(blk));
	blk.magic   = SETTINGS_MAGIC;
	blk.version = SETTINGS_VERSION;
	blk.size    = (uint16_t)sizeof(Settings);
	blk.s       = *s;
	blk.crc32   = settingsCrc32(&blk, (uint32_t)(sizeof(blk) - sizeof(uint32_t)));
	memcpy(fakeFlashBytes(), &blk, sizeof(blk));
}

/** @brief The block currently in the fake flash. */
static SettingsBlock stored() {
	SettingsBlock blk;
	memcpy(&blk, fakeFlashBytes(), sizeof(blk));
	return blk;
}

static void testBlankFlash() {
	section("Settings: a blank part");

	fakeFlashClear();
	settingsLoad();

	Settings def;
	settingsDefaults(&def);

	checkTrue("no stored block reported", !settingsStored());
	checkInt("ESC pin is the compiled default", settings()->dshotPin, def.dshotPin);
	checkInt("ceiling is the compiled default", settings()->maxThrottle, def.maxThrottle);
	checkTrue("nothing to save", !settingsDirty());
}

static void testRoundTrip() {
	section("Settings: save and load back");

	fakeFlashClear();
	settingsLoad();

	settings()->poles = 12;
	settings()->maxThrottle = 600;
	settings()->highContrast = 1;
	checkTrue("editing marks it dirty", settingsDirty());

	checkTrue("save reports success", settingsSave());
	checkTrue("saving clears dirty", !settingsDirty());
	checkTrue("now reports a stored block", settingsStored());

	// Reload from scratch, as a power cycle would.
	settings()->poles = 4;
	settingsLoad();
	checkInt("poles survived", settings()->poles, 12);
	checkInt("ceiling survived", settings()->maxThrottle, 600);
	checkInt("contrast survived", settings()->highContrast, 1);
	checkTrue("stored block found", settingsStored());
}

static void testRejectsCorruption() {
	section("Settings: a block that does not validate is discarded whole");

	Settings def;
	settingsDefaults(&def);

	// A ceiling nobody would choose, stored correctly, so the only thing under
	// test below is whether the *block* is trusted.
	Settings evil = def;
	evil.maxThrottle = MAX_THROTTLE_CEILING;
	evil.poles = 22;

	// --- one flipped payload byte ---
	fakeFlashClear();
	storeValid(&evil);
	fakeFlashBytes()[offsetof(SettingsBlock, s)] ^= 0x01;
	settingsLoad();
	checkTrue("bad CRC: not treated as stored", !settingsStored());
	checkInt("bad CRC: ceiling falls back", settings()->maxThrottle, def.maxThrottle);
	checkInt("bad CRC: poles fall back", settings()->poles, def.poles);

	// --- a future version, otherwise perfect ---
	fakeFlashClear();
	storeValid(&evil);
	{
		SettingsBlock blk = stored();
		blk.version = SETTINGS_VERSION + 1;
		blk.crc32 = settingsCrc32(&blk, (uint32_t)(sizeof(blk) - sizeof(uint32_t)));
		memcpy(fakeFlashBytes(), &blk, sizeof(blk));
	}
	settingsLoad();
	// This is the one that matters. A migration that guessed at an old layout
	// could hand back a plausible 2000 as easily as a 400, and there is no
	// version of that trade worth taking: losing a pole count costs two taps.
	checkTrue("version bump: not treated as stored", !settingsStored());
	checkInt("version bump: ceiling falls back, never carried over",
	         settings()->maxThrottle, def.maxThrottle);

	// --- wrong struct size, right version ---
	fakeFlashClear();
	storeValid(&evil);
	{
		SettingsBlock blk = stored();
		blk.size = (uint16_t)(sizeof(Settings) + 1);
		blk.crc32 = settingsCrc32(&blk, (uint32_t)(sizeof(blk) - sizeof(uint32_t)));
		memcpy(fakeFlashBytes(), &blk, sizeof(blk));
	}
	settingsLoad();
	checkTrue("size mismatch: not treated as stored", !settingsStored());
	checkInt("size mismatch: ceiling falls back",
	         settings()->maxThrottle, def.maxThrottle);

	// --- wrong magic ---
	fakeFlashClear();
	storeValid(&evil);
	fakeFlashBytes()[0] ^= 0xFF;
	settingsLoad();
	checkTrue("bad magic: not treated as stored", !settingsStored());
	checkInt("bad magic: ceiling falls back",
	         settings()->maxThrottle, def.maxThrottle);
}

static void testValidateRepairs() {
	section("Settings: validation clamps everything downstream assumes");

	Settings s;
	settingsDefaults(&s);

	s.poles = 0;
	checkTrue("a zero pole count is repaired", !settingsValidate(&s));
	checkInt("clamped up to the minimum", s.poles, MIN_MOTOR_POLES);

	settingsDefaults(&s);
	s.poles = 13;
	settingsValidate(&s);
	checkInt("odd pole counts are made even", s.poles, 12);

	settingsDefaults(&s);
	s.poles = 200;
	settingsValidate(&s);
	checkInt("pole count clamped down", s.poles, MAX_MOTOR_POLES);

	settingsDefaults(&s);
	s.maxThrottle = 60000;
	settingsValidate(&s);
	checkInt("ceiling clamped to the compiled maximum",
	         s.maxThrottle, MAX_THROTTLE_CEILING);

	settingsDefaults(&s);
	s.maxThrottle = 0;
	settingsValidate(&s);
	checkInt("a zero ceiling becomes one step", s.maxThrottle, MAX_THROTTLE_STEP);

	settingsDefaults(&s);
	s.maxThrottle = 455;
	settingsValidate(&s);
	checkInt("ceiling snapped to a whole step", s.maxThrottle, 400);

	settingsDefaults(&s);
	s.dshotKbaud = 999;
	settingsValidate(&s);
	checkInt("an illegal bitrate falls back", s.dshotKbaud, DSHOT_SPEED_KBAUD);

	settingsDefaults(&s);
	s.backlight = 0;
	settingsValidate(&s);
	checkTrue("backlight never left looking like a dead panel", s.backlight >= 16);
}

/**
 * @brief The pin defaults are the macros', and the running board's.
 *
 * Both halves are load-bearing and only one of them is a compile-time fact.
 *
 * The descriptors used to carry literals while config.h still defined
 * `DSHOT_PIN` and friends, so the macros were read by nothing: `-DDSHOT_PIN=28`
 * configured, printed a status line claiming it had taken, and left the image
 * on GP29. CI asserted the option was honoured and passed, because it was
 * grepping CMake's own output rather than the firmware. The first block below
 * is what would have caught that -- tautological at face value, which is the
 * point, because CI runs this suite again with the macros overridden and a
 * descriptor that stopped reading them fails there.
 *
 * The second block is the half a macro cannot do: on a unified image, which
 * board's defaults apply is not a build-time choice at all. boardProbe() picks
 * the descriptor at boot and settingsDefaults() reads whichever one that was.
 */
static void testPinDefaults() {
	section("Settings: pin defaults come from each board's own macros");

	checkInt("2.0\" ESC pin", BOARD_DESC_LCD_2.defaultDshotPin, DSHOT_PIN_LCD_2);
	checkInt("2.0\" KISS pin", BOARD_DESC_LCD_2.defaultKissPin, KISS_PIN_LCD_2);
	checkInt("2.0\" KISS default",
	         BOARD_DESC_LCD_2.defaultKissEnable ? 1 : 0, KISS_ENABLE_LCD_2 ? 1 : 0);
	checkInt("2.8\" ESC pin", BOARD_DESC_LCD_2_8.defaultDshotPin, DSHOT_PIN_LCD_2_8);
	checkInt("2.8\" KISS pin", BOARD_DESC_LCD_2_8.defaultKissPin, KISS_PIN_LCD_2_8);
	checkInt("2.8\" KISS default",
	         BOARD_DESC_LCD_2_8.defaultKissEnable ? 1 : 0, KISS_ENABLE_LCD_2_8 ? 1 : 0);

	// The 2.8" pair specifically, because it is the one with no slack: two free
	// pins, one wire each, and a default that put both on GP29 would describe a
	// wiring nobody can solder.
	checkTrue("the 2.8\" wires are on different pins",
	          BOARD_DESC_LCD_2_8.defaultDshotPin != BOARD_DESC_LCD_2_8.defaultKissPin);

	// Every descriptor's defaults are free on its own board. The descriptors
	// static_assert this too, so a bad override never links -- this is the
	// version that says so in the output rather than in a compiler error.
	for (int i = 0; i < boardCount(); i++) {
		const BoardDesc *b = boardAt(i);
		checkTrue("its ESC default is free on it",
		          (b->freeGpioMask >> b->defaultDshotPin) & 1u);
		checkTrue("its KISS default is free on it",
		          (b->freeGpioMask >> b->defaultKissPin) & 1u);
	}
}

/** @brief settingsDefaults() follows whichever board booted. @see testPinDefaults */
static void testDefaultsFollowTheBoard() {
	if (boardCount() < 2) return;
	section("Settings: the defaults are the detected board's");

	uint8_t was = boardId();
	for (int i = 0; i < boardCount(); i++) {
		// What boardSelect() stands in for here is boardProbe() on real
		// hardware: a unified image does not know which set of defaults it
		// wants until it has asked the board. @see setup() in main.cpp
		boardSelect(boardIdAt(i));
		Settings s;
		settingsDefaults(&s);
		checkInt("the ESC default is this board's",
		         s.dshotPin, boardAt(i)->defaultDshotPin);
		checkInt("the KISS default is this board's",
		         s.kissPin, boardAt(i)->defaultKissPin);
		checkTrue("and both are legal on it",
		          settingsPinFreeOn(boardIdAt(i), s.dshotPin) &&
		          settingsPinFreeOn(boardIdAt(i), s.kissPin));
		checkTrue("and never the same pin", s.dshotPin != s.kissPin);
	}
	boardSelect(was);
}

static void testPinRules() {
	section("Settings: only pins this board actually offers");

	// Not asserted against specific numbers: the free set differs per board and
	// the suite runs for both. The rules are what is invariant.
	checkTrue("the default ESC pin is free on this board",
	          settingsPinFree(g_board->defaultDshotPin));
	checkTrue("GP30 is not a pin", !settingsPinFree(30));

	// Stepping only ever lands somewhere legal, in both directions, for every
	// GPIO -- which is what removes the invalid-selection state entirely.
	//
	// There used to be a second flavour of this, stepping only through the
	// eight GPIOs where n % 4 == 1, because those are the only ones the two
	// hardware UARTs can receive on and the KISS pin had to be one of them.
	// The receiver is a PIO state machine now and samples whatever pin it is
	// given, so the telemetry wire steps through the same free set as the ESC
	// wire and there is one rule here instead of two. @see pio_uart_rx.h
	bool allFree = true;
	for (int p = 0; p <= 29; p++) {
		uint8_t up = settingsNextPin((uint8_t)p, +1);
		uint8_t dn = settingsNextPin((uint8_t)p, -1);
		if (!settingsPinFree(up) || !settingsPinFree(dn)) allFree = false;
	}
	checkTrue("stepping always lands on a free pin", allFree);
	checkInt("and stepping nowhere stays put",
	         settingsNextPin(g_board->defaultDshotPin, 0),
	         g_board->defaultDshotPin);
}

static void testKissNeedsItsOwnPin() {
	section("Settings: KISS is switched off, never quietly moved");

	Settings s;
	settingsDefaults(&s);
	s.kissEnable = 1;
	s.kissPin = s.dshotPin;
	checkTrue("sharing a pin with the ESC is repaired", !settingsValidate(&s));
	// Moving it would start listening on a pin the user never connected, and a
	// telemetry row reading ON against the wrong pin is worse than one reading
	// OFF.
	checkInt("KISS switched off rather than relocated", s.kissEnable, 0);

	settingsDefaults(&s);
	s.kissEnable = 1;
	s.kissPin = 18;              // the LCD's CTS/peripheral territory on both
	settingsValidate(&s);
	checkInt("a pin the board is using turns KISS off", s.kissEnable, 0);

	// The rule that used to sit between those two is gone: a KISS pin no longer
	// has to be one of the eight a hardware UART can receive on. That rule is
	// what made KISS impossible to enable on the 2.8" -- two free pins, only
	// GP29 of them a UART RX, and the ESC starts on GP29 -- so every free pin
	// that is not the ESC's is now a legal telemetry pin on every board.
	int legal = 0;
	for (int p = 0; p <= 29 && legal < 4; p++) {
		settingsDefaults(&s);
		if (!settingsPinFree((uint8_t)p) || (uint8_t)p == s.dshotPin) continue;
		s.kissEnable = 1;
		s.kissPin = (uint8_t)p;
		checkTrue("any free pin that is not the ESC's is a legal KISS pin",
		          settingsValidate(&s) && s.kissEnable == 1);
		legal++;
	}
	checkTrue("and this board offers at least one", legal > 0);
}

static void testSaveIsVerified() {
	section("Settings: a save that cannot land reports failure");

	fakeFlashClear();
	settingsLoad();
	settings()->poles = 8;

	fakeFlashSetWritable(false);
	checkTrue("refused write is reported, not swallowed", !settingsSave());
	checkTrue("still dirty afterwards", settingsDirty());
	checkTrue("still no stored block", !settingsStored());

	fakeFlashSetWritable(true);
	checkTrue("succeeds once flash accepts it", settingsSave());
	checkTrue("clean afterwards", !settingsDirty());
}

static void testSaveValidatesFirst() {
	section("Settings: a save can only ever store something loadable");

	fakeFlashClear();
	settingsLoad();
	settings()->poles = 13;               // odd
	settings()->maxThrottle = 60000;      // absurd
	checkTrue("save succeeds", settingsSave());

	settingsLoad();
	checkInt("stored pole count was made even", settings()->poles, 12);
	checkInt("stored ceiling was clamped",
	         settings()->maxThrottle, MAX_THROTTLE_CEILING);
}

static void testCrcKnownAnswer() {
	section("Settings: the CRC is CRC-32, not merely self-consistent");

	// The reflected-0xEDB88320 check value for "123456789". Every other test
	// here only proves the implementation agrees with itself; a wrong init or
	// final XOR would pass all of them and surface later as a forced format
	// break, because fixing the CRC would orphan every stored block in the
	// field.
	checkTrue("CRC-32(\"123456789\") == 0xCBF43926",
	          settingsCrc32("123456789", 9) == 0xCBF43926u);
	checkTrue("CRC-32 of nothing is 0", settingsCrc32("", 0) == 0u);
}

static void testForeignBoardBlock() {
	section("Settings: a valid block naming a board this image is not");

	// Magic, version, size and CRC all correct -- only the *content* is
	// foreign: a board id no build recognises, carrying a pin that every
	// board's mask refuses. The load-path validate call is the sole interlock
	// between this block and the pump being seeded with an occupied GPIO;
	// delete it and these checks are what fails.
	Settings evil;
	settingsDefaults(&evil);
	evil.boardId = 99;
	evil.dshotPin = 18;                    // LCD clock / touch INT territory
	fakeFlashClear();
	storeValid(&evil);
	settingsLoad();
	checkInt("the alien board id is repaired to this board's",
	         settings()->boardId, boardId());
	checkTrue("the ESC pin was re-judged against this board",
	          settingsPinFree(settings()->dshotPin));
	checkInt("and fell back to this board's default",
	         settings()->dshotPin, g_board->defaultDshotPin);
}

static void testNoOpSaveWritesNothing() {
	section("Settings: saving what is already stored writes nothing");

	fakeFlashClear();
	settingsLoad();
	settings()->poles = 10;
	checkTrue("the first save lands", settingsSave());

	// On the device every write attempt is an erase first: wear, core1 parked,
	// and a torn-write window in the only copy of the block. A save of exactly
	// what is stored must therefore not touch flash at all.
	int before = fakeFlashWrites();
	checkTrue("saving again unchanged still reports success", settingsSave());
	checkInt("but attempts no write", fakeFlashWrites() - before, 0);

	settings()->poles = 12;
	checkTrue("a real change saves again", settingsSave());
	checkInt("with exactly one write", fakeFlashWrites() - before, 1);
}

static void testBlankNamesTheLiveBoard() {
	section("Settings: blank flash names the board this boot detected");

	// Which board it is has been settled before this load runs -- setup()
	// probes for it on a unified image, the build fixes it on a single-board
	// one -- so there is no third state left for the settings to be in.
	// BOARD_ID_UNSET used to mean "carries two descriptors, does not yet know
	// which"; probing on every boot, before the load, retired it.
	fakeFlashClear();
	settingsLoad();
	checkInt("the board id is the live board's", settings()->boardId, boardId());
	checkTrue("which is a board this image holds", boardId() != BOARD_ID_UNSET);
	checkTrue("and the default ESC pin is free on it",
	          settingsPinFree(settings()->dshotPin));
}

/**
 * @brief The bricking path, as a test.
 *
 * A stored board id used to be applied on load. So a block naming the wrong
 * board built the next boot's display, touch controller and pin map for
 * hardware that was not there — and the SETUP screen that could have put it
 * back was the screen that no longer came up. One wrong tap on a picker, and
 * the only way in was a reflash over USB. Detection is the only thing that
 * selects a board now; this is what proves the stored value cannot get its
 * vote back.
 */
static void testStoredBoardNeverSelects() {
	if (boardCount() < 2) return;
	section("Settings: a block naming the other board cannot re-point this one");

	uint8_t live  = boardId();
	uint8_t other = boardIdAt(0) == live ? boardIdAt(1) : boardIdAt(0);

	// The trap: a GPIO the other board leaves free and this one does not. That
	// is the pin a load which honoured the block would have handed to the DShot
	// pump -- an output driven into an on-board peripheral.
	int trap = -1;
	for (int p = 0; p < 30 && trap < 0; p++)
		if (settingsPinFreeOn(other, (uint8_t)p) &&
		    !settingsPinFreeOn(live, (uint8_t)p))
			trap = p;

	Settings s;
	settingsDefaults(&s);
	s.boardId = other;
	if (trap >= 0) s.dshotPin = (uint8_t)trap;
	fakeFlashClear();
	storeValid(&s);
	settingsLoad();

	checkInt("the live board is untouched", boardId(), live);
	checkInt("and the stored id is rewritten to it", settings()->boardId, live);
	checkTrue("the ESC pin was re-judged against this board",
	          settingsPinFree(settings()->dshotPin));
	if (trap >= 0)
		checkTrue("so the other board's pin did not survive",
		          settings()->dshotPin != (uint8_t)trap);
}

void runSettingsTests() {
	testBlankFlash();
	testRoundTrip();
	testRejectsCorruption();
	testValidateRepairs();
	testPinDefaults();
	testDefaultsFollowTheBoard();
	testPinRules();
	testKissNeedsItsOwnPin();
	testSaveIsVerified();
	testSaveValidatesFirst();
	testCrcKnownAnswer();
	testForeignBoardBlock();
	testNoOpSaveWritesNothing();
	testBlankNamesTheLiveBoard();
	testStoredBoardNeverSelects();

	// Leave the module in a known state: test_ui.cpp runs after this and reads
	// settings() for the pole count and throttle ceiling.
	fakeFlashClear();
	settingsLoad();
}
