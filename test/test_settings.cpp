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

static void testPinRules() {
	section("Settings: only pins this board actually offers");

	// Not asserted against specific numbers: the free set differs per board and
	// the suite runs for both. The rules are what is invariant.
	checkTrue("the default ESC pin is free on this board",
	          settingsPinFree(g_board->defaultDshotPin));
	checkTrue("GP30 is not a pin", !settingsPinFree(30));

	// UART RX exists only on GPn where n % 4 == 1, and which instance it is
	// alternates in pairs of groups.
	checkInt("GP1 is uart0 RX", settingsUartForPin(1), 0);
	checkInt("GP5 is uart1 RX", settingsUartForPin(5), 1);
	checkInt("GP9 is uart1 RX", settingsUartForPin(9), 1);
	checkInt("GP13 is uart0 RX", settingsUartForPin(13), 0);
	checkInt("GP17 is uart0 RX", settingsUartForPin(17), 0);
	checkInt("GP21 is uart1 RX", settingsUartForPin(21), 1);
	checkInt("GP25 is uart1 RX", settingsUartForPin(25), 1);
	checkInt("GP29 is uart0 RX", settingsUartForPin(29), 0);
	checkInt("GP4 cannot receive", settingsUartForPin(4), -1);
	checkInt("GP28 is TX, not RX", settingsUartForPin(28), -1);

	// Stepping only ever lands somewhere legal, in both directions, for every
	// GPIO -- which is what removes the invalid-selection state entirely.
	bool allFree = true, allUart = true;
	for (int p = 0; p <= 29; p++) {
		uint8_t up = settingsNextPin((uint8_t)p, +1, false);
		uint8_t dn = settingsNextPin((uint8_t)p, -1, false);
		if (!settingsPinFree(up) || !settingsPinFree(dn)) allFree = false;
		uint8_t uu = settingsNextPin((uint8_t)p, +1, true);
		if (!settingsPinFree(uu) || settingsUartForPin(uu) < 0) {
			// Legal only if this board has no UART-capable free pin at all.
			bool any = false;
			for (int q = 0; q <= 29; q++)
				if (settingsPinFree((uint8_t)q) && settingsUartForPin((uint8_t)q) >= 0)
					any = true;
			if (any) allUart = false;
		}
	}
	checkTrue("stepping always lands on a free pin", allFree);
	checkTrue("UART stepping always lands on a receiver", allUart);
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
	s.kissPin = 4;               // free on the 2.0", but UART TX, not RX
	settingsValidate(&s);
	checkInt("a pin that cannot receive turns KISS off", s.kissEnable, 0);

	settingsDefaults(&s);
	s.kissEnable = 1;
	s.kissPin = 18;              // the LCD's CTS/peripheral territory on both
	settingsValidate(&s);
	checkInt("a pin the board is using turns KISS off", s.kissEnable, 0);
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

void runSettingsTests() {
	testBlankFlash();
	testRoundTrip();
	testRejectsCorruption();
	testValidateRepairs();
	testPinRules();
	testKissNeedsItsOwnPin();
	testSaveIsVerified();
	testSaveValidatesFirst();

	// Leave the module in a known state: test_ui.cpp runs after this and reads
	// settings() for the pole count and throttle ceiling.
	fakeFlashClear();
	settingsLoad();
}
