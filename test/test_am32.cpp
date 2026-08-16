/**
 * @file test_am32.cpp
 * @brief Protocol and settings-codec tests, checked against a reference tool.
 *
 * The blobs and expectations here are not invented: they are the default
 * settings the AM32 configurator ships, and the semantics documented in its own
 * byte-by-byte comments. Checking against an implementation known to work on
 * real hardware is what caught the one-based protocol enum and the x2 current
 * limit scaling, both of which looked perfectly plausible.
 */

#include "check.h"
#include "am32_eeprom.h"
#include "am32_bl.h"

/**
 * @brief "Air" defaults, from the configurator's compiled `.data` section.
 * @note Layout revision 2, so bytes 0x05..0x10 are the device name.
 */
static const uint8_t AIR[AM32_SETTINGS_SIZE] = {
	0x01,0x02,0x01,0x01,0x23,0x4e,0x45,0x4f,0x45,0x53,0x43,0x20,0x66,0x30,0x35,0x31,
	0x20,0x00,0x01,0x01,0x01,0x01,0x00,0x03,0x18,0x68,0x30,0x0e,0x01,0x01,0x05,0x00,
	0x80,0x80,0x80,0x32,0x01,0x50,0x00,0x00,0x0f,0x0a,0x0a,0x65,0x14,0x05,0x00,0x31};

/** @brief "Crawler" defaults, from the same section. */
static const uint8_t CRAWLER[AM32_SETTINGS_SIZE] = {
	0x01,0x02,0x01,0x01,0x23,0x4e,0x45,0x4f,0x45,0x53,0x43,0x20,0x66,0x30,0x35,0x31,
	0x20,0x00,0x00,0x00,0x01,0x01,0x01,0x02,0x18,0x64,0x37,0x0e,0x00,0x00,0x05,0x00,
	0x80,0x80,0x80,0x32,0x00,0x32,0x00,0x00,0x0f,0x0a,0x0a,0x8d,0x66,0x06,0x01,0x00};

/**
 * @brief The reference Python tool's AIR_START_EEPROM, whose byte comments
 *        document what each field means. The expectations below are lifted
 *        straight from those comments.
 */
static const uint8_t REF_PY[AM32_SETTINGS_SIZE] = {
	0x01,0x02,0x01,0x01,0x23,0x4E,0x45,0x4F,0x45,0x53,0x43,0x20,0x66,0x30,0x35,0x31,
	0x20,0x00,0x00,0x00,0x01,0x01,0x01,0x02,0x18,0x64,0x37,0x0E,0x00,0x00,0x05,0x00,
	0x80,0x80,0x80,0x32,0x00,0x32,0x00,0x00,0x0F,0x0A,0x0A,0x8D,0x66,0x06,0x01,0x00};

static const Am32Field *field(const char *name) {
	for (uint16_t i = 0; i < AM32_FIELD_COUNT; i++) {
		if (strcmp(AM32_FIELDS[i].name, name) == 0) return &AM32_FIELDS[i];
	}
	return nullptr;
}

static void expect(const uint8_t *ee, const char *name, const char *want) {
	const Am32Field *f = field(name);
	if (!f) { printf("  %-24s MISSING FIELD\n", name); g_failures++; return; }
	char got[24];
	am32FormatValue(f, ee, got, sizeof(got));
	checkStr(name, got, want);
}

/**
 * @brief Timing advance, which AM32 writes in two different encodings.
 *
 * Reported from the bench as "the initial value of timing is read without
 * conversion into the display; when you change it it gets corrected". Both
 * halves of that sentence were symptoms of the same thing: the field table
 * described the pre-1.90 encoding only, so a modern ESC's byte was scaled as
 * though it were an old one, and the first `-` or `+` press clamped it into the
 * old range -- which made the number plausible again by destroying it.
 *
 * The encodings, from AM32's own start-up code:
 *   0..3   pre-1.90, 7.5 degree steps
 *   10..42 1.90 and later, (raw - 10) steps of 0.9375 degrees
 */
static void testAdvanceEncodings() {
	section("AM32: timing advance, both encodings");

	uint8_t ee[AM32_SETTINGS_SIZE];
	memcpy(ee, REF_PY, sizeof(ee));

	// --- modern encoding, which is what the bug report was about ---
	ee[AM32_OFF_ADVANCE] = 26;              // AM32's own default
	expect(ee, "ADVANCE", "15.0DEG");
	ee[AM32_OFF_ADVANCE] = 10;
	expect(ee, "ADVANCE", "0.0DEG");
	ee[AM32_OFF_ADVANCE] = 42;
	expect(ee, "ADVANCE", "30.0DEG");

	// The number this used to show for a modern default: 26 scaled as if it
	// were an old-format byte. Nothing on the ESC was ever set to 195 degrees.
	checkTrue("a modern byte is no longer read as the old encoding",
	          am32AdvanceDeciDeg(26) != 26 * 75);

	// --- old encoding, which must keep working ---
	ee[AM32_OFF_ADVANCE] = 0;  expect(ee, "ADVANCE", "0.0DEG");
	ee[AM32_OFF_ADVANCE] = 2;  expect(ee, "ADVANCE", "15.0DEG");
	ee[AM32_OFF_ADVANCE] = 3;  expect(ee, "ADVANCE", "22.5DEG");

	// --- neither: say so rather than invent an angle ---
	// AM32 substitutes its own default for these, so the ESC is not running at
	// whatever we would otherwise have printed.
	checkInt("a byte in neither encoding has no angle",
	         am32AdvanceDeciDeg(7), -1);
	checkInt("nor does one above the modern maximum",
	         am32AdvanceDeciDeg(200), -1);
	ee[AM32_OFF_ADVANCE] = 7;
	expect(ee, "ADVANCE", "? (7)");

	// --- editing ---
	const Am32Field *f = field("ADVANCE");
	checkTrue("the field exists", f != nullptr);

	// Editing writes the modern encoding, whatever was read.
	ee[AM32_OFF_ADVANCE] = 26;
	am32Adjust(f, ee, +1);
	checkInt("one step up is one 0.9375 degree increment", ee[AM32_OFF_ADVANCE], 27);
	am32Adjust(f, ee, -1);
	checkInt("and one step back returns", ee[AM32_OFF_ADVANCE], 26);

	// This is the half that silently threw the setting away. An ESC holding the
	// old value 2 -- 15 degrees -- used to jump to the modern minimum on the
	// first press, because 2 is below it. It must convert, keeping the angle.
	ee[AM32_OFF_ADVANCE] = 2;
	am32Adjust(f, ee, +1);
	checkInt("an old-format value converts rather than clamping to zero",
	         am32AdvanceDeciDeg(ee[AM32_OFF_ADVANCE]), 150 + 9);
	ee[AM32_OFF_ADVANCE] = 2;
	am32Adjust(f, ee, -1);
	checkTrue("and a step down from it stays near 15 degrees",
	          am32AdvanceDeciDeg(ee[AM32_OFF_ADVANCE]) > 130);

	// The rails hold.
	ee[AM32_OFF_ADVANCE] = 10;
	am32Adjust(f, ee, -1);
	checkInt("it does not step below zero degrees", ee[AM32_OFF_ADVANCE], 10);
	ee[AM32_OFF_ADVANCE] = 42;
	am32Adjust(f, ee, +1);
	checkInt("nor above thirty", ee[AM32_OFF_ADVANCE], 42);

	// A byte in neither encoding becomes AM32's own default rather than 0.
	ee[AM32_OFF_ADVANCE] = 200;
	am32Adjust(f, ee, +1);
	checkTrue("an unreadable byte edits from AM32's default, not from zero",
	          ee[AM32_OFF_ADVANCE] >= 25 && ee[AM32_OFF_ADVANCE] <= 28);
}

void runAm32Tests() {
	testAdvanceEncodings();
	section("AM32 init string (must be byte-exact)");
	{
		// Twelve zeros, 0x0D, "BLHeli", then the fixed signature F4 7D.
		static const uint8_t REF[21] = {
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x0D,'B','L','H','e','l','i',0xF4,0x7D};
		checkInt("init string length", sizeof(REF), 21);
		int zeros = 0;
		while (zeros < 21 && REF[zeros] == 0) zeros++;
		checkInt("leading zero count", zeros, 12);
		// F4 7D is a literal, not a checksum over the body.
		checkTrue("trailer is not a CRC of the body",
		          am32Crc16(REF, 19) != (uint16_t)(0x7DF4));
	}

	section("CRC-16/ARC (poly 0xA001, init 0)");
	{
		const uint8_t v[] = {'1','2','3','4','5','6','7','8','9'};
		checkInt("crc16(\"123456789\")", am32Crc16(v, 9), 0xBB3D);
	}

	section("MCU family to settings address");
	{
		checkInt("G071 type byte", AM32_ESC_G071_2KB, 0x2B);
		checkInt("F051 type byte", AM32_ESC_F051_1KB, 0x1F);
		checkInt("F3 type byte",   AM32_ESC_F3_2KB,   0x35);
	}

	section("Device identity");
	{
		char name[AM32_NAME_LEN + 1];
		am32DeviceName(AIR, name);
		checkStr("device name", name, "NEOESC f051");
		checkTrue("AIR blob recognised", am32Plausible(AIR));
		checkTrue("CRAWLER blob recognised", am32Plausible(CRAWLER));

		uint8_t erased[AM32_SETTINGS_SIZE];
		memset(erased, 0xFF, sizeof(erased));
		checkTrue("erased page rejected", !am32Plausible(erased));
	}

	section("AIR preset decodes to the tool's values");
	expect(AIR, "POLES", "14");
	expect(AIR, "PWM FREQ", "24KHZ");
	expect(AIR, "KV", "1920KV");
	expect(AIR, "POWER", "104");
	expect(AIR, "ADVANCE", "22.5DEG");
	expect(AIR, "DIRECTION", "NORMAL");
	expect(AIR, "BIDIRECTIONAL", "ON");
	expect(AIR, "LOW VOLT CUTOFF", "ON");
	expect(AIR, "CELL CUTOFF", "3.3V");
	expect(AIR, "TEMP LIMIT", "101C");
	expect(AIR, "CURRENT LIMIT", "40A");
	expect(AIR, "NEUTRAL", "1502US");
	expect(AIR, "LOW THRESHOLD", "1006US");
	expect(AIR, "HIGH THRESHOLD", "2006US");
	expect(AIR, "DEAD BAND", "50US");
	expect(AIR, "BEEP VOLUME", "5");
	expect(AIR, "BRAKE ON STOP", "ON");

	section("CRAWLER preset differences show through");
	expect(CRAWLER, "BIDIRECTIONAL", "OFF");
	expect(CRAWLER, "SINE STARTUP", "OFF");
	expect(CRAWLER, "ADVANCE", "15.0DEG");
	expect(CRAWLER, "POWER", "100");
	expect(CRAWLER, "KV", "2200KV");
	expect(CRAWLER, "LOW VOLT CUTOFF", "OFF");
	expect(CRAWLER, "CELL CUTOFF", "3.0V");
	expect(CRAWLER, "TEMP LIMIT", "141C");
	expect(CRAWLER, "CURRENT LIMIT", "204A");
	expect(CRAWLER, "STUCK ROTOR", "ON");
	expect(CRAWLER, "BRAKE ON STOP", "OFF");

	section("Field table vs the reference tool's documented semantics");
	// Each expectation is what constants.py's own byte comment states.
	expect(REF_PY, "ADVANCE", "15.0DEG");        // "2 = 15 degrees"
	expect(REF_PY, "PWM FREQ", "24KHZ");         // "default 24khz"
	expect(REF_PY, "POWER", "100");              // "default 100 percent"
	expect(REF_PY, "KV", "2200KV");              // "55 = 2200kv"
	expect(REF_PY, "POLES", "14");               // "default 14"
	expect(REF_PY, "LOW THRESHOLD", "1006US");   // "(value*2) + 750"
	expect(REF_PY, "HIGH THRESHOLD", "2006US");  // "(value*2) + 1750"
	expect(REF_PY, "NEUTRAL", "1502US");         // "128 = 1500 us"
	expect(REF_PY, "CELL CUTOFF", "3.0V");       // "default 50 3.0volts"
	expect(REF_PY, "SINE RANGE", "15");          // "default 15"
	expect(REF_PY, "TEMP LIMIT", "141C");        // "default 141"
	expect(REF_PY, "CURRENT LIMIT", "204A");     // "(value x 2) ... default 102"
	expect(REF_PY, "SINE POWER", "6");           // "default 6"
	expect(REF_PY, "PROTOCOL", "AUTO");          // "1)Auto" -- one-based

	section("PWM frequency range");
	{
		const Am32Field *f = field("PWM FREQ");
		checkInt("max is 144 kHz, not the old 48", f->rawMax, 144);
		checkInt("min", f->rawMin, 8);
	}

	section("Layout-revision gating");
	{
		int n2 = 0, n3 = 0;
		for (uint16_t i = 0; i < AM32_FIELD_COUNT; i++) {
			if (am32FieldApplies(&AM32_FIELDS[i], 2)) n2++;
			if (am32FieldApplies(&AM32_FIELDS[i], 3)) n3++;
		}
		checkInt("revision 3 exposes 8 more fields", n3 - n2, 8);

		uint8_t v3[AM32_SETTINGS_SIZE];
		memcpy(v3, AIR, sizeof(v3));
		v3[AM32_OFF_LAYOUT_REVISION] = 3;
		char name[AM32_NAME_LEN + 1];
		am32DeviceName(v3, name);
		// Those bytes are settings in revision 3, so rendering them as text
		// would print binary garbage as a device name.
		checkTrue("name suppressed at revision 3", name[0] == '\0');
	}

	section("Editing clamps and steps");
	{
		uint8_t e[AM32_SETTINGS_SIZE];
		memcpy(e, AIR, sizeof(e));
		const Am32Field *poles = field("POLES");
		for (int i = 0; i < 40; i++) am32Adjust(poles, e, +1);
		checkInt("poles clamp at max", e[poles->offset], poles->rawMax);
		for (int i = 0; i < 80; i++) am32Adjust(poles, e, -1);
		checkInt("poles clamp at min", e[poles->offset], poles->rawMin);

		const Am32Field *pwm = field("PWM FREQ");
		for (int i = 0; i < 200; i++) am32Adjust(pwm, e, +1);
		checkInt("pwm reaches 144", e[pwm->offset], 144);
	}
}
