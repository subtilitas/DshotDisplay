/**
 * @file am32_eeprom.cpp
 * @brief AM32 settings field table and value formatting.
 *
 * Scaling factors follow the AM32 configurator's conversions. Where a factor
 * was inferred rather than taken from the layout definition it is called out in
 * a comment, because a wrong divisor here shows a plausible-looking but false
 * number rather than failing loudly.
 */

#include "am32_eeprom.h"

#include <stdio.h>
#include <string.h>

static const char *const NAMES_DIRECTION[] = {"NORMAL", "REVERSED"};
// One-based, per the reference tool: 1) Auto 2) Dshot 3) Servo 4) PWM
// 5) Serial 6) Betaflight safe arming. Index 0 is an uninitialised ESC.
static const char *const NAMES_PROTOCOL[]  = {
	"UNSET", "AUTO", "DSHOT", "SERVO", "PWM", "SERIAL", "BF SAFE"
};
static const char *const NAMES_OFFON[]     = {"OFF", "ON"};

/**
 * @defgroup am32_tablemacros Field table shorthand
 * @brief Keeps the settings table readable; unused columns stay zero.
 *
 * Written out longhand, each row would be fifteen columns of mostly zeroes and
 * the table would stop being scannable — which is the entire point of holding
 * the settings in a table rather than in code.
 *
 * Common arguments: @c grp section heading, @c nm display label, @c off byte
 * offset, @c ver minimum EEPROM layout revision (0 for any).
 * @{
 */

/** @brief Off/on field. */
#define F_BOOL(grp, nm, off, ver)                                              \
	{grp, nm, off, A32_BOOL, 0, 1, 1, ver, 0, 0, 0, 0, "", NAMES_OFFON, 2}
/** @brief Plain integer over @c lo..@c hi in steps of @c st, suffixed @c unit. */
#define F_RAW(grp, nm, off, lo, hi, st, unit, ver)                             \
	{grp, nm, off, A32_RAW, lo, hi, st, ver, 0, 0, 0, 0, unit, nullptr, 0}
/** @brief Displayed as `(raw * m + a) / d`, with one decimal when @c d > 1. */
#define F_SCALED(grp, nm, off, lo, hi, st, m, d, a, unit, ver)                 \
	{grp, nm, off, A32_SCALED, lo, hi, st, ver, 0, m, d, a, unit, nullptr, 0}
/** @brief Index into the @c cnt labels of @c tbl. */
#define F_ENUM(grp, nm, off, tbl, cnt, ver)                                    \
	{grp, nm, off, A32_ENUM, 0, (uint8_t)((cnt) - 1), 1, ver, 0, 0, 0, 0, "", tbl, cnt}
/** @} */

const Am32Field AM32_FIELDS[] = {
	// ---- Motor -----------------------------------------------------------
	F_ENUM  ("MOTOR", "DIRECTION",        0x11, NAMES_DIRECTION, 2, 0),
	F_BOOL  ("MOTOR", "BIDIRECTIONAL",    0x12, 0),
	F_RAW   ("MOTOR", "POLES",            0x1B, 2, 36, 2, "", 0),
	// Stored as KV/40, so the editable range 1..255 spans 40..10200 KV.
	F_SCALED("MOTOR", "KV",               0x1A, 1, 255, 1, 40, 1, 0, "KV", 0),
	F_BOOL  ("MOTOR", "HALL SENSORS",     0x27, 0),

	// ---- Timing and PWM --------------------------------------------------
	// AM32 stores timing in 7.5 degree steps, 0..4 => 0..30 degrees.
	F_SCALED("TIMING", "ADVANCE",         0x17, 0, 4, 1, 75, 10, 0, "DEG", 0),
	F_BOOL  ("TIMING", "AUTO ADVANCE",    0x2F, 0),
	// AM32 accepts up to 144 kHz, not the 48 the older configurator UI capped
	// its slider at. Worth a coarse swipe rather than 136 taps.
	F_RAW   ("TIMING", "PWM FREQ",        0x18, 8, 144, 1, "KHZ", 0),
	F_BOOL  ("TIMING", "VARIABLE PWM",    0x15, 0),
	F_BOOL  ("TIMING", "COMPLEMENTARY",   0x14, 0),

	// ---- Startup ---------------------------------------------------------
	F_RAW   ("STARTUP", "POWER",          0x19, 50, 150, 1, "", 0),
	F_BOOL  ("STARTUP", "SINE STARTUP",   0x13, 0),
	F_RAW   ("STARTUP", "SINE RANGE",     0x28, 5, 25, 1, "", 0),
	F_RAW   ("STARTUP", "SINE POWER",     0x2D, 1, 10, 1, "", 0),
	F_BOOL  ("STARTUP", "STUCK ROTOR",    0x16, 0),
	F_BOOL  ("STARTUP", "STALL PROTECT",  0x1D, 0),

	// ---- Braking ---------------------------------------------------------
	F_BOOL  ("BRAKE", "BRAKE ON STOP",    0x1C, 0),
	F_RAW   ("BRAKE", "STOPPED LEVEL",    0x29, 1, 10, 1, "", 0),
	F_RAW   ("BRAKE", "RUNNING LEVEL",    0x2A, 1, 10, 1, "", 0),
	F_BOOL  ("BRAKE", "RC CAR REVERSE",   0x26, 0),

	// ---- Limits ----------------------------------------------------------
	F_BOOL  ("LIMITS", "LOW VOLT CUTOFF", 0x24, 0),
	// Cell cutoff is stored as hundredths of a volt above 2.50 V.
	F_SCALED("LIMITS", "CELL CUTOFF",     0x25, 0, 100, 1, 1, 100, 250, "V", 0),
	// Above 140 the ESC treats the temperature limit as disabled.
	F_RAW   ("LIMITS", "TEMP LIMIT",      0x2B, 70, 141, 1, "C", 0),
	// Inferred: the tool's presets read 20 and 102, which as amps*2 give the
	// 40 A and 204 A (effectively off) that its UI shows.
	F_SCALED("LIMITS", "CURRENT LIMIT",   0x2C, 0, 102, 1, 2, 1, 0, "A", 0),

	// ---- Input -----------------------------------------------------------
	F_ENUM  ("INPUT", "PROTOCOL",         0x2E, NAMES_PROTOCOL, 7, 0),
	F_BOOL  ("INPUT", "30MS TELEMETRY",   0x1F, 0),
	F_RAW   ("INPUT", "BEEP VOLUME",      0x1E, 0, 11, 1, "", 0),

	// ---- Servo -----------------------------------------------------------
	// Thresholds are (microseconds - base) / 2; neutral is a plain offset from
	// 1374 us, which is what makes the stock value of 128 read as 1502 us.
	F_SCALED("SERVO", "LOW THRESHOLD",    0x20, 0, 255, 1, 2, 1, 750,  "US", 0),
	F_SCALED("SERVO", "HIGH THRESHOLD",   0x21, 0, 255, 1, 2, 1, 1750, "US", 0),
	F_SCALED("SERVO", "NEUTRAL",          0x22, 0, 255, 1, 1, 1, 1374, "US", 0),
	F_RAW   ("SERVO", "DEAD BAND",        0x23, 0, 100, 1, "US", 0),

	// ---- Layout revision 3 and later only --------------------------------
	F_RAW   ("ADVANCED", "MAX RAMP",      0x05, 0, 255, 1, "", 3),
	F_RAW   ("ADVANCED", "MIN DUTY",      0x06, 0, 255, 1, "", 3),
	F_BOOL  ("ADVANCED", "NO STICK CAL",  0x07, 3),
	F_RAW   ("ADVANCED", "ABS VOLT CUT",  0x08, 0, 255, 1, "", 3),
	F_RAW   ("ADVANCED", "CURRENT P",     0x09, 0, 255, 1, "", 3),
	F_RAW   ("ADVANCED", "CURRENT I",     0x0A, 0, 255, 1, "", 3),
	F_RAW   ("ADVANCED", "CURRENT D",     0x0B, 0, 255, 1, "", 3),
	F_RAW   ("ADVANCED", "ACTIVE BRAKE",  0x0C, 0, 255, 1, "", 3),
};

const uint16_t AM32_FIELD_COUNT = (uint16_t)(sizeof(AM32_FIELDS) / sizeof(AM32_FIELDS[0]));

bool am32FieldApplies(const Am32Field *f, uint8_t layoutRev) {
	if (f->minVer && layoutRev < f->minVer) return false;
	if (f->maxVer && layoutRev > f->maxVer) return false;
	return true;
}

void am32FormatValue(const Am32Field *f, const uint8_t *eeprom, char *out, int outLen) {
	uint8_t raw = eeprom[f->offset];

	switch (f->type) {
		case A32_BOOL:
			snprintf(out, outLen, "%s", raw ? "ON" : "OFF");
			return;

		case A32_ENUM:
			if (f->names && raw < f->nameCount) snprintf(out, outLen, "%s", f->names[raw]);
			else                                snprintf(out, outLen, "? (%u)", raw);
			return;

		case A32_RAW:
			if (f->unit && f->unit[0]) snprintf(out, outLen, "%u%s", raw, f->unit);
			else                       snprintf(out, outLen, "%u", raw);
			return;

		case A32_SCALED: {
			// (raw * mul + add) / div. The offset is applied *before* the
			// divide: the cell cutoff is (raw + 250) / 100 volts, not
			// raw / 100 + 250.
			int32_t num = (int32_t)raw * f->mul + f->add;
			int32_t div = f->div ? f->div : 1;
			if (div == 1) {
				snprintf(out, outLen, "%ld%s", (long)num, f->unit);
			} else {
				// One decimal place, without dragging in floating point.
				int32_t scaled = num * 10 / div;
				int32_t frac = scaled % 10;
				snprintf(out, outLen, "%ld.%ld%s",
				         (long)(scaled / 10), (long)(frac < 0 ? -frac : frac), f->unit);
			}
			return;
		}
	}
	snprintf(out, outLen, "%u", raw);
}

void am32DeviceName(const uint8_t *eeprom, char *out) {
	if (eeprom[AM32_OFF_LAYOUT_REVISION] >= 3) {
		// Those bytes are settings in this revision, not text.
		out[0] = '\0';
		return;
	}
	int n = 0;
	for (int i = 0; i < AM32_NAME_LEN; i++) {
		uint8_t c = eeprom[AM32_OFF_NAME + i];
		if (c < 0x20 || c > 0x7E) break;
		out[n++] = (char)c;
	}
	while (n > 0 && out[n - 1] == ' ') n--;   // trim the padding
	out[n] = '\0';
}

void am32Adjust(const Am32Field *f, uint8_t *eeprom, int dir) {
	int32_t v = (int32_t)eeprom[f->offset];
	int32_t step = f->step ? f->step : 1;

	// A value already outside the field's range would otherwise need several
	// presses before it re-entered it; snap to the nearest end instead.
	if (v < f->rawMin) v = f->rawMin;
	else if (v > f->rawMax) v = f->rawMax;
	else v += (dir > 0 ? step : -step);

	if (v < f->rawMin) v = f->rawMin;
	if (v > f->rawMax) v = f->rawMax;
	eeprom[f->offset] = (uint8_t)v;
}

bool am32Plausible(const uint8_t *eeprom) {
	// An erased or absent settings page reads as all 0xFF or all 0x00.
	if (eeprom[AM32_OFF_BOOT_BYTE] == 0x00 || eeprom[AM32_OFF_BOOT_BYTE] == 0xFF) return false;
	uint8_t rev = eeprom[AM32_OFF_LAYOUT_REVISION];
	if (rev == 0 || rev > 8) return false;
	if (eeprom[0x1B] == 0) return false;   // a motor with zero poles is nonsense
	return true;
}
