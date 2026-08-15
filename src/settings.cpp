/**
 * @file settings.cpp
 * @brief Settings defaults, validation, and the load/save rules.
 *
 * No hardware here on purpose: the flash sequence lives in settings_flash.cpp
 * behind @ref settingsStorageRead() / @ref settingsStorageWrite(), so
 * everything in this file is compiled into the host suite and exercised
 * directly. The rules that keep a corrupted block from becoming a live throttle
 * ceiling are exactly the rules worth testing.
 */

#include "settings.h"
#include "config.h"
#include "board_desc.h"

#include <string.h>

/**
 * @brief The working copy, seeded with the compiled defaults on first use.
 *
 * Seeded lazily rather than at static-init time: computing the defaults reads
 * `g_board`'s pointee, and the descriptors live in other translation units. On
 * the device they are constant-initialised, but the host build gives them
 * dynamic initialisers, so a static `defaultsValue()` here would read them in
 * whatever order the linker chose — it happened to work only because the
 * board_desc objects preceded this file in the test Makefile's source list.
 * First-use seeding runs after *all* static init, on either toolchain.
 *
 * The original goal is kept: nothing that calls settings() before
 * settingsLoad() ever sees a zero-filled Settings — a pole count of 0 and a
 * throttle ceiling of 0 — instead of a diagnosable default.
 */
static Settings s_live;
static Settings s_saved;
static bool     s_stored = false;
static bool     s_seeded = false;

static void ensureSeeded() {
	if (s_seeded) return;
	s_seeded = true;
	settingsDefaults(&s_live);
	s_saved = s_live;
}

// ---------------------------------------------------------------------------
// pin rules
// ---------------------------------------------------------------------------

/**
 * @brief The descriptor for @p id, or NULL if this image cannot drive it.
 *
 * Validation resolves the board *named by the settings being validated* rather
 * than whichever board is live: while the SETUP screen holds a pending board
 * choice, its pins must be judged against the board they are for.
 */
static const BoardDesc *descForId(uint8_t id) {
	for (int i = 0; i < boardCount(); i++)
		if (boardIdAt(i) == id) return boardAt(i);
	return (const BoardDesc *)0;
}

bool settingsPinFreeOn(uint8_t boardIdArg, uint8_t pin) {
	if (pin > 29) return false;
	const BoardDesc *b = descForId(boardIdArg);
	if (!b) b = g_board;
	return (b->freeGpioMask >> pin) & 1u;
}

bool settingsPinFree(uint8_t pin) {
	// From the active descriptor, not a compile-time mask: in a unified image
	// the answer differs between the two boards, and the SETUP screen's pin
	// steppers are the thing that must never offer an occupied GPIO.
	return settingsPinFreeOn(boardId(), pin);
}

uint8_t settingsNextPinOn(uint8_t boardIdArg, uint8_t from, int dir) {
	if (dir == 0) return from;
	int step = dir > 0 ? 1 : -1;
	int p = (int)from;
	// 30 hops covers every GPIO once, so a board offering no other candidate
	// returns the pin it was given rather than spinning.
	for (int i = 0; i < 30; i++) {
		p += step;
		if (p > 29) p = 0;
		if (p < 0) p = 29;
		if (settingsPinFreeOn(boardIdArg, (uint8_t)p)) return (uint8_t)p;
	}
	return from;
}

uint8_t settingsNextPin(uint8_t from, int dir) {
	return settingsNextPinOn(boardId(), from, dir);
}

// ---------------------------------------------------------------------------
// defaults and validation
// ---------------------------------------------------------------------------

void settingsDefaults(Settings *out) {
	memset(out, 0, sizeof(*out));
	// Pin defaults come from the board, not from config.h: a unified image has
	// two boards' worth of them and config.h can only hold one.
	out->boardId      = boardId();
	out->dshotPin     = g_board->defaultDshotPin;
	out->dshotKbaud   = DSHOT_SPEED_KBAUD;
	out->kissEnable   = g_board->defaultKissEnable ? 1 : 0;
	out->kissPin      = g_board->defaultKissPin;
	out->poles        = DEFAULT_MOTOR_POLES;
	out->maxThrottle  = DEFAULT_MAX_THROTTLE;
	out->backlight    = LCD_BACKLIGHT_DEFAULT;
	out->highContrast = 0;
}

/** @brief The bitrates the DShot driver is built and tested for. */
static const uint16_t LEGAL_KBAUD[4] = { 150, 300, 600, 1200 };

bool settingsValidate(Settings *s) {
	bool ok = true;

	// --- motor poles: even, and in range ---
	if (s->poles < MIN_MOTOR_POLES) { s->poles = MIN_MOTOR_POLES; ok = false; }
	if (s->poles > MAX_MOTOR_POLES) { s->poles = MAX_MOTOR_POLES; ok = false; }
	if (s->poles & 1u) { s->poles = (uint8_t)(s->poles - 1); ok = false; }

	// --- throttle ceiling: whole steps, never above the compiled ceiling ---
	if (s->maxThrottle > MAX_THROTTLE_CEILING) {
		s->maxThrottle = MAX_THROTTLE_CEILING;
		ok = false;
	}
	if (s->maxThrottle % MAX_THROTTLE_STEP) {
		s->maxThrottle = (uint16_t)(s->maxThrottle / MAX_THROTTLE_STEP * MAX_THROTTLE_STEP);
		ok = false;
	}
	if (s->maxThrottle < MAX_THROTTLE_STEP) {
		s->maxThrottle = MAX_THROTTLE_STEP;
		ok = false;
	}

	// --- DShot bitrate ---
	bool legal = false;
	for (int i = 0; i < 4; i++) if (s->dshotKbaud == LEGAL_KBAUD[i]) legal = true;
	if (!legal) { s->dshotKbaud = DSHOT_SPEED_KBAUD; ok = false; }

	// --- board ---
	// Not a setting, and never restored from the block. The board is identified
	// at boot — by boardProbe() on a unified image, by the build on a
	// single-board one — and this field only *records* which one, so that a
	// block written on other hardware is recognisable as such and the pin rules
	// below have a board to judge against.
	//
	// So an id that disagrees with the live board is overwritten rather than
	// honoured, and validation never re-points g_board as a side effect.
	// Honouring it is the bug this rule exists for: a stored id naming the wrong
	// board was applied by the next boot, which built the display from the wrong
	// pins, and the screen that could have undone it was the screen that no
	// longer came up. An alien id — a block from a build this one is not — lands
	// in the same place, for the same reason.
	if (s->boardId != boardId()) {
		s->boardId = boardId();
		ok = false;
	}
	const BoardDesc *b = g_board;

	// --- ESC pin ---
	if (!settingsPinFreeOn(s->boardId, s->dshotPin)) {
		s->dshotPin = b->defaultDshotPin;
		ok = false;
	}

	// --- KISS ---
	// Two rules left, now that the receiver is a PIO state machine and any free
	// GPIO can take it: the pin must be free on this board, and it must not be
	// the ESC's. The third — "and it must be one of the eight the hardware
	// UARTs can receive on" — is gone with the hardware UART. @see pio_uart_rx.h
	//
	// Switched off rather than moved, still. Moving it would silently start
	// listening on a pin the user never connected anything to, and a telemetry
	// wire that reads "ON" against the wrong pin is worse than one reading
	// "OFF". The SETUP screen does move it, because there the pin change is the
	// thing being asked for. @see uiSetupTick()
	if (s->kissEnable) {
		if (!settingsPinFreeOn(s->boardId, s->kissPin) ||
		    s->kissPin == s->dshotPin) {
			s->kissEnable = 0;
			ok = false;
		}
	}
	if (s->kissEnable != 0 && s->kissEnable != 1) { s->kissEnable = 1; ok = false; }
	if (s->highContrast != 0 && s->highContrast != 1) { s->highContrast = 1; ok = false; }

	// --- backlight: never so dim the panel looks dead ---
	if (s->backlight < 16) { s->backlight = 16; ok = false; }

	return ok;
}

// ---------------------------------------------------------------------------
// CRC
// ---------------------------------------------------------------------------

uint32_t settingsCrc32(const void *data, uint32_t len) {
	const uint8_t *p = (const uint8_t *)data;
	uint32_t crc = 0xFFFFFFFFu;
	for (uint32_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
		}
	}
	return ~crc;
}

/** @brief Bytes of a block that the CRC covers: everything before the CRC. */
static uint32_t crcSpan() {
	return (uint32_t)(sizeof(SettingsBlock) - sizeof(uint32_t));
}

// ---------------------------------------------------------------------------
// load / save
// ---------------------------------------------------------------------------

/**
 * @brief The blank-flash starting point: this board's compiled defaults.
 *
 * Which board that is has already been settled — setup() probes for it on a
 * unified image and the build fixes it on a single-board one — so the pin
 * defaults @ref settingsDefaults() reads out of `g_board` are the right
 * board's either way. There used to be a third case, a unified image that had
 * not found out yet and marked itself @ref BOARD_ID_UNSET so nothing mistook
 * the first descriptor for an answer; probing before the load retired it.
 */
static void freshDefaults() {
	s_seeded = true;
	settingsDefaults(&s_live);
	s_saved = s_live;
}

void settingsLoad() {
	freshDefaults();
	s_stored = false;

	SettingsBlock blk;
	memset(&blk, 0, sizeof(blk));
	if (!settingsStorageRead(&blk, sizeof(blk))) {
		return;
	}

	// Four independent reasons to distrust the block, all fatal, none partial.
	//
	// Rejecting a version mismatch outright rather than migrating is the whole
	// point: the field this file most wants to get right is the throttle
	// ceiling, and a migration that guesses at an old layout can hand back a
	// plausible-looking 2000 as easily as a 400. Losing a pole count is
	// recoverable in two taps; the other direction is a motor at full throttle.
	if (blk.magic != SETTINGS_MAGIC ||
	    blk.version != SETTINGS_VERSION ||
	    blk.size != (uint16_t)sizeof(Settings) ||
	    blk.crc32 != settingsCrc32(&blk, crcSpan())) {
		return;
	}

	s_live = blk.s;
	// Validation rewrites the block's board id to the one this boot detected and
	// re-judges every pin against it, so a block carried over from the other
	// board loads as this board's settings rather than as a pin map for hardware
	// that is not here. Nothing in this file selects a board: g_board was
	// settled before the load ran, and no stored value may move it.
	settingsValidate(&s_live);
	s_saved = s_live;
	s_stored = true;
}

Settings *settings() {
	ensureSeeded();
	return &s_live;
}

bool settingsSave() {
	ensureSeeded();
	settingsValidate(&s_live);

	// A save of exactly what is already stored is a no-op, reported as
	// success. Every real erase parks core1 and opens a torn-write window in
	// the only copy of the block — there is no A/B sector — so the write has
	// to buy something.
	if (s_stored && memcmp(&s_live, &s_saved, sizeof(Settings)) == 0)
		return true;

	SettingsBlock blk;
	memset(&blk, 0, sizeof(blk));
	blk.magic   = SETTINGS_MAGIC;
	blk.version = SETTINGS_VERSION;
	blk.size    = (uint16_t)sizeof(Settings);
	blk.s       = s_live;
	blk.crc32   = settingsCrc32(&blk, crcSpan());

	if (!settingsStorageWrite(&blk, sizeof(blk))) return false;

	// Read back rather than trusting the write, for the same reason the AM32
	// settings write does: a save that reports success but did not land is
	// worse than one that fails, because the next power cycle is where you
	// find out.
	SettingsBlock check;
	memset(&check, 0, sizeof(check));
	if (!settingsStorageRead(&check, sizeof(check))) return false;
	if (memcmp(&check, &blk, sizeof(blk)) != 0) return false;

	s_saved  = s_live;
	s_stored = true;
	return true;
}

bool settingsStored() { return s_stored; }

bool settingsDirty() {
	ensureSeeded();
	return memcmp(&s_live, &s_saved, sizeof(Settings)) != 0;
}
