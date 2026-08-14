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

/**
 * @brief Which UART instance owns the RX function in each group of four GPIOs.
 *
 * RP2350 lays the UARTs out in blocks of four — TX, RX, CTS, RTS — and the
 * instance alternates in *pairs* of blocks rather than every block, which is
 * why this is a table and not arithmetic. Index is `pin / 4`.
 *
 *     GP1 GP5 GP9 GP13 GP17 GP21 GP25 GP29
 *      u0  u1  u1   u0   u0   u1   u1   u0
 */
static const uint8_t UART_BY_GROUP[8] = { 0, 1, 1, 0, 0, 1, 1, 0 };

int settingsUartForPin(uint8_t pin) {
	if (pin > 29) return -1;
	// Only the second pin of each block of four is an RX function.
	if ((pin & 3u) != 1u) return -1;
	return (int)UART_BY_GROUP[pin >> 2];
}

uint8_t settingsNextPinOn(uint8_t boardIdArg, uint8_t from, int dir, bool uartOnly) {
	if (dir == 0) return from;
	int step = dir > 0 ? 1 : -1;
	int p = (int)from;
	// 30 hops covers every GPIO once, so a board offering no other candidate
	// returns the pin it was given rather than spinning.
	for (int i = 0; i < 30; i++) {
		p += step;
		if (p > 29) p = 0;
		if (p < 0) p = 29;
		if (!settingsPinFreeOn(boardIdArg, (uint8_t)p)) continue;
		if (uartOnly && settingsUartForPin((uint8_t)p) < 0) continue;
		return (uint8_t)p;
	}
	return from;
}

uint8_t settingsNextPin(uint8_t from, int dir, bool uartOnly) {
	return settingsNextPinOn(boardId(), from, dir, uartOnly);
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
	// An id this image cannot drive is as untrustworthy as a bad CRC: every pin
	// below is validated against the board's free mask, and validating them
	// against the wrong board is worse than starting over.
	//
	// Resolved to a descriptor rather than *selected*: validation must not
	// re-point g_board as a side effect, because the SETUP screen validates a
	// pending board choice while the display is still running on the live one.
	// settingsLoad() applies the board explicitly after validating.
	const BoardDesc *b;
	if (s->boardId == BOARD_ID_UNSET) {
		// Nothing chosen yet (unified image, blank flash). Judge the pins
		// against the live board; the probe or the picker fills this in.
		b = g_board;
	} else {
		b = descForId(s->boardId);
		if (!b) {
			s->boardId = boardId();
			b = g_board;
			ok = false;
		}
	}

	// --- ESC pin ---
	if (!settingsPinFreeOn(s->boardId, s->dshotPin)) {
		s->dshotPin = b->defaultDshotPin;
		ok = false;
	}

	// --- KISS ---
	// Switched off rather than moved. Moving it would silently start listening
	// on a pin the user never connected anything to, and a telemetry wire that
	// reads "ON" against the wrong pin is worse than one that reads "OFF".
	if (s->kissEnable) {
		if (!settingsPinFreeOn(s->boardId, s->kissPin) ||
		    settingsUartForPin(s->kissPin) < 0 ||
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
 * @brief The blank-flash starting point: defaults, with the board honestly
 *        marked unchosen on an image that carries more than one.
 *
 * A single-board image knows what it is, so its defaults name it. A unified
 * image does not — its `g_board` merely points at the first descriptor so that
 * early code has coherent values to read — and pretending that guess is a
 * choice is exactly the bug that used to boot 2.8" hardware with the 2.0"
 * board's pin map. @see boardProbe() and setup() in main.cpp for who answers.
 */
static void freshDefaults() {
	s_seeded = true;
	settingsDefaults(&s_live);
	if (boardCount() > 1) s_live.boardId = BOARD_ID_UNSET;
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
	settingsValidate(&s_live);
	// Validation resolved the board id (repairing it if this image cannot
	// drive it) without touching g_board; a *load* is the one moment the
	// stored choice becomes the live board.
	if (s_live.boardId != BOARD_ID_UNSET) boardSelect(s_live.boardId);
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
