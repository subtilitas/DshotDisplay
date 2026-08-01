/**
 * @file fakes.cpp
 * @brief Fake platform, fake ESC and scripted touch for the host tests.
 *
 * Stands in for everything the firmware talks to that is not present on a PC:
 * the Arduino/Pico SDK surface, the display, the touch panel, the DShot task,
 * and an AM32 ESC that serves a real settings blob and accepts writes.
 *
 * Time is virtual. millis() advances only when the test says so, which makes
 * hold-to-arm, hold-to-write and gesture repeat deterministic rather than
 * dependent on how fast the machine runs.
 */

#include "Arduino.h"
#include "gfx.h"
#include "st7789.h"
#include "cst816.h"
#include "esc_task.h"
#include "am32_bl.h"
#include "am32_eeprom.h"
#include "fakes.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// virtual clock and Arduino surface
// ---------------------------------------------------------------------------
static uint32_t g_ms = 0;

void fakeAdvance(uint32_t ms) { g_ms += ms; }
uint32_t millis() { return g_ms; }
uint32_t time_us_32() { return g_ms * 1000; }
void delay(uint32_t d) { g_ms += d; }

void pinMode(uint32_t, uint32_t) {}
void digitalWrite(uint32_t, uint32_t) {}
void analogWrite(uint32_t, int) {}
void analogWriteFreq(uint32_t) {}
void analogWriteRange(uint32_t) {}
void analogReadResolution(int) {}
/** @brief ~3.92 V pack through the board's 200k/100k divider. */
int analogRead(uint32_t) { return (int)(3.92f / 3.0f / 3.3f * 4095.0f); }

SerialStub Serial;
void SerialStub::begin(unsigned long) {}
int SerialStub::printf(const char *, ...) { return 0; }

// ---------------------------------------------------------------------------
// display: render into the real framebuffer, never push pixels anywhere
// ---------------------------------------------------------------------------
void st7789FlushDirty() { gfxClearDirty(); }
void st7789SetBacklight(uint8_t) {}

// ---------------------------------------------------------------------------
// scripted touch
// ---------------------------------------------------------------------------
static TouchState g_touch;

bool touchInit() { return true; }

void touchPoll(TouchState *t) {
	*t = g_touch;
	// Edge flags last exactly one poll, as the real driver guarantees.
	g_touch.pressed = false;
	g_touch.released = false;
}

void fakePress(int x, int y) {
	g_touch.down = true;
	g_touch.pressed = true;
	g_touch.x = (int16_t)x;
	g_touch.y = (int16_t)y;
	g_touch.downX = (int16_t)x;
	g_touch.downY = (int16_t)y;
}

void fakeHold(int x, int y) {
	g_touch.down = true;
	g_touch.x = (int16_t)x;
	g_touch.y = (int16_t)y;
}

void fakeRelease() {
	g_touch.down = false;
	g_touch.released = true;
}

// ---------------------------------------------------------------------------
// fake AM32 ESC
//
// Seeded with the Air preset recovered from the reference configurator, so the
// UI tests operate on values a real ESC would actually report.
// ---------------------------------------------------------------------------
static uint8_t g_esc[AM32_EEPROM_SIZE] = {
	0x01,0x02,0x01,0x01,0x23,0x4e,0x45,0x4f,0x45,0x53,0x43,0x20,0x66,0x30,0x35,0x31,
	0x20,0x00,0x01,0x01,0x01,0x01,0x00,0x03,0x18,0x68,0x30,0x0e,0x01,0x01,0x05,0x00,
	0x80,0x80,0x80,0x32,0x01,0x50,0x00,0x00,0x0f,0x0a,0x0a,0x65,0x14,0x05,0x00,0x31};

uint8_t fakeEscByte(int offset) { return g_esc[offset]; }

uint16_t am32Crc16(const uint8_t *d, uint16_t n) {
	uint16_t c = 0;
	for (uint16_t i = 0; i < n; i++) {
		uint8_t b = d[i];
		for (int k = 0; k < 8; k++) {
			if ((b ^ (uint8_t)c) & 1) { c >>= 1; c ^= 0xA001; } else { c >>= 1; }
			b >>= 1;
		}
	}
	return c;
}

void am32BlBegin(uint8_t) {}
void am32BlEnd() {}
void am32BlJumpToBootloader() {}
void am32BlSetBaud(uint32_t) {}
uint32_t am32BlBaud() { return 19200; }
Am32Result am32BlHandshake(uint8_t *) { return AM32_OK; }
Am32Result am32BlRead(uint16_t, uint8_t *b, uint8_t n) { memcpy(b, g_esc, n); return AM32_OK; }
Am32Result am32BlWrite(uint16_t, const uint8_t *b, uint8_t n) { memcpy(g_esc, b, n); return AM32_OK; }
Am32Result am32BlErasePage(uint16_t) { return AM32_OK; }
Am32Result am32BlRun() { return AM32_OK; }
const char *am32ResultText(Am32Result r) { return r == AM32_OK ? "OK" : "ERR"; }
uint8_t am32BlLastRx(const uint8_t **b) { static uint8_t none[1] = {0}; *b = none; return 0; }
uint16_t am32BlEepromAddr() { return 0x7C00; }
uint8_t am32BlEscType() { return AM32_ESC_F051_1KB; }
const char *am32BlEscTypeName() { return "F051"; }
bool am32BlAddrDiv4() { return false; }

// ---------------------------------------------------------------------------
// DShot task
// ---------------------------------------------------------------------------
static uint16_t g_throttle = 0;
static bool     g_armed = false;
static bool     g_suspended = false;
static EscTelemetry g_tel;

uint16_t fakeThrottle() { return g_throttle; }
bool fakeArmed() { return g_armed; }
bool fakePinReturned() { return !g_suspended; }
void fakeSetTelemetry(const EscTelemetry *t) { g_tel = *t; }

void escSetThrottle(uint16_t t) { g_throttle = t; }
void escSetArmed(bool a) { g_armed = a; }
void escSetPoles(uint8_t) {}
void escHeartbeat() {}
void escRequestEdtEnable() {}
void escRequestBeep(uint8_t) {}
bool escEdtRequested() { return true; }
void escSnapshot(EscTelemetry *o) { *o = g_tel; o->lastRpmMs = g_ms; }
void escTaskSuspend() { g_suspended = true; }
void escTaskResume() { g_suspended = false; }
bool escTaskSuspended() { return g_suspended; }

// ---------------------------------------------------------------------------
// PPM dump, so a failing layout test can be looked at
// ---------------------------------------------------------------------------
void fakeDumpFrame(const char *name) {
	const uint16_t *fb = gfxBuffer();
	FILE *f = fopen(name, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
	for (int i = 0; i < GFX_W * GFX_H; i++) {
		uint16_t c = fb[i];
		uint8_t px[3] = { (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
		                  (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
		                  (uint8_t)((c & 0x1F) * 255 / 31) };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
}
