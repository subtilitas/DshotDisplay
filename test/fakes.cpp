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

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "gfx.h"
#include "st7789.h"
#include "touch.h"
#include "esc_task.h"
#include "settings.h"
#include "config.h"
#include "am32_bl.h"
#include "am32_eeprom.h"
#include "sd_log.h"
#include "fakes.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// virtual clock and the Pico SDK surface the firmware uses
// ---------------------------------------------------------------------------

// Time is virtual: it advances only when a test says so. plat.h's millis() and
// micros() are inline wrappers over these, so faking the SDK timebase fakes
// both without the firmware knowing.
static uint32_t g_ms = 0;

void fakeAdvance(uint32_t ms) { g_ms += ms; }
uint64_t time_us_64() { return (uint64_t)g_ms * 1000u; }
uint32_t time_us_32() { return g_ms * 1000u; }
void sleep_ms(uint32_t d) { g_ms += d; }
void sleep_us(uint64_t us) { g_ms += (uint32_t)(us / 1000u); }

// --- ADC: a steady ~3.92 V pack through the board's 200k/100k divider ---
void adc_init() {}
void adc_gpio_init(unsigned) {}
void adc_select_input(unsigned) {}
uint16_t adc_read() { return (uint16_t)(3.92f / 3.0f / 3.3f * 4095.0f); }

// --- PWM: the backlight. Levels are accepted and discarded ---
unsigned pwm_gpio_to_slice_num(unsigned) { return 0; }
pwm_config pwm_get_default_config() { return pwm_config{}; }
void pwm_config_set_clkdiv(pwm_config *, float) {}
void pwm_config_set_wrap(pwm_config *, uint16_t) {}
void pwm_init(unsigned, pwm_config *, bool) {}
void pwm_set_gpio_level(unsigned, uint16_t) {}

uint32_t clock_get_hz(enum clock_index) { return 150000000u; }

// --- I2C: every transfer fails, which is what an absent touch controller
//     does. touchPoll() then reports no contact and the fake touch injector
//     in this file drives the UI tests instead ---
i2c_inst_t *i2c0 = nullptr;
i2c_inst_t *i2c1 = nullptr;

// The board descriptors name an SPI instance and a touch driver each. On the
// host none of them is ever dialled -- the panel and the touch chip are both
// faked -- but the descriptors are real objects whose fields have to resolve
// at link time.
spi_inst_t *spi0 = nullptr;
spi_inst_t *spi1 = nullptr;

static bool fakeTouchDriverInit() { return true; }
static void fakeTouchDriverPoll(TouchState *) {}
extern const TouchDriver TOUCH_DRIVER_CST816D;
extern const TouchDriver TOUCH_DRIVER_CST816D = { "CST816D", fakeTouchDriverInit, fakeTouchDriverPoll };
extern const TouchDriver TOUCH_DRIVER_CST328;
extern const TouchDriver TOUCH_DRIVER_CST328  = { "CST328",  fakeTouchDriverInit, fakeTouchDriverPoll };
void i2c_init(i2c_inst_t *, unsigned) {}
int i2c_write_timeout_us(i2c_inst_t *, uint8_t, const uint8_t *, size_t, bool, unsigned) { return -1; }
int i2c_read_timeout_us(i2c_inst_t *, uint8_t, uint8_t *, size_t, bool, unsigned) { return -1; }

// --- UART: never readable. No ESC here to answer a telemetry request ---
uart_inst_t *uart0 = nullptr;
uart_inst_t *uart1 = nullptr;
unsigned uart_init(uart_inst_t *, unsigned baud) { return baud; }
void uart_deinit(uart_inst_t *) {}
bool uart_is_readable(uart_inst_t *) { return false; }
uint8_t uart_getc(uart_inst_t *) { return 0; }
void uart_set_format(uart_inst_t *, unsigned, unsigned, enum uart_parity_t) {}
void uart_set_fifo_enabled(uart_inst_t *, bool) {}

// ---------------------------------------------------------------------------
// display: render into the real framebuffer, never push pixels anywhere
// ---------------------------------------------------------------------------
void st7789FlushDirty() { gfxClearDirty(); }
// Recorded rather than discarded: the arm-feedback dip is only observable as a
// backlight level, so a fake that threw it away would make it untestable.
static uint8_t g_backlight = 0;
static uint8_t g_backlightMin = 255;
uint8_t fakeBacklight() { return g_backlight; }
uint8_t fakeBacklightMin() { return g_backlightMin; }
void fakeBacklightResetMin() { g_backlightMin = 255; }
void st7789SetBacklight(uint8_t level) {
	g_backlight = level;
	if (level < g_backlightMin) g_backlightMin = level;
}

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

// These three mirror esc_task.cpp deliberately, clamp for clamp. They used not
// to: escSetArmed() only set a flag, so "disarming zeroes the commanded
// throttle" -- a rule production does enforce -- could not be observed by any
// test, and escSetThrottle() had no clamp, so nothing checked the one in
// production either.
void escSetThrottle(uint16_t t) { g_throttle = t > 2000 ? 2000 : t; }
void escSetArmed(bool a) {
	if (!a) g_throttle = 0;
	g_armed = a;
}
static uint8_t g_poles = 14;
uint8_t fakePoles() { return g_poles; }
void escSetPoles(uint8_t p) { g_poles = p < 2 ? 2 : p; }
void escHeartbeat() {}
// Mirrors the two-line rule in esc_task.cpp: the command is refused while
// armed. esc_task.cpp cannot be linked here (PIO, UART), so this is a copy --
// but the thing under test is what the UI does with the answer, and that is
// the shipped code.
// The counter now increments only when the command is actually queued, as
// production does. Counting refused requests too meant a test asserting "the
// button sent the command" passed for a request the ESC never saw.
static int g_beepRequests = 0;
int  fakeBeepRequests() { return g_beepRequests; }
bool escRequestBeep(uint8_t) {
	if (g_armed) return false;
	g_beepRequests++;
	return true;
}
// Settable, because production only reports true once an ESC has answered and
// been sent an enable -- so a hardcoded true made the DS600 chip's two states
// indistinguishable and the settings screen's EDT indicator untestable.
static bool g_edtRequested = true;
void fakeSetEdtRequested(bool on) { g_edtRequested = on; }
bool escEdtRequested() { return g_edtRequested; }
// Verbatim, with no helpful stamping of arrival times. That stamping used to
// be here, and it meant every test saw permanently fresh telemetry -- so the
// bug where readings never expired could not have been caught by any of them.
// Tests that want live data now say when it arrived.
void escSnapshot(EscTelemetry *o) { *o = g_tel; }
void escTaskSuspend() { g_suspended = true; }
void escTaskResume() { g_suspended = false; }
bool escTaskSuspended() { return g_suspended; }

// Mirrors esc_task.cpp: a wiring change disarms and zeroes the throttle,
// because the ESC on the old pin stops hearing frames the moment it is
// released. Tests assert on that, so the fake has to do it too.
static uint8_t  g_dshotPin = DSHOT_PIN;
static uint16_t g_dshotKbaud = DSHOT_SPEED_KBAUD;
static int      g_configures = 0;
int fakeConfigureCount() { return g_configures; }
uint16_t fakeDshotKbaud() { return g_dshotKbaud; }
void escTaskConfigure(uint8_t pin, uint16_t kbaud, bool, uint8_t) {
	if (g_dshotPin == pin && g_dshotKbaud == kbaud) return;
	g_dshotPin = pin;
	g_dshotKbaud = kbaud;
	g_armed = false;
	g_throttle = 0;
	g_configures++;
}
uint8_t escTaskDshotPin() { return g_dshotPin; }

// ---------------------------------------------------------------------------
// Settings storage
//
// settings.cpp holds every rule -- defaults, validation, the CRC, and the
// discard-whole-block fallback -- and none of the flash sequence, so it links
// here against this RAM array instead of settings_flash.cpp. That split is what
// makes the rules testable at all: the interesting behaviour is "what happens
// to a block that does not validate", and answering it needs to be able to
// write a bad one.
// ---------------------------------------------------------------------------
static uint8_t g_flash[256];
static bool    g_flashWritable = true;
static int     g_flashWrites = 0;

void fakeFlashClear() { memset(g_flash, 0xFF, sizeof(g_flash)); }
void fakeFlashSetWritable(bool on) { g_flashWritable = on; }
uint8_t *fakeFlashBytes() { return g_flash; }
int fakeFlashWrites() { return g_flashWrites; }

bool settingsStorageRead(void *dst, uint32_t len) {
	if (len > sizeof(g_flash)) return false;
	memcpy(dst, g_flash, len);
	return true;
}

bool settingsStorageWrite(const void *src, uint32_t len) {
	// Counted even when refused: on the device the sector is erased before the
	// program, so a write *attempt* is already wear and already the torn
	// window. "How many attempts" is the number the no-op-save test cares
	// about.
	g_flashWrites++;
	if (!g_flashWritable) return false;
	if (len > sizeof(g_flash)) return false;
	memcpy(g_flash, src, len);
	return true;
}

// ---------------------------------------------------------------------------
// Reboot
//
// platReboot() has no caller left. A saved *board* change used to take effect
// by rebooting into it, and that is the mechanism that made one wrong tap on a
// board picker cost a reflash; the board is detected every boot now. So this
// counts a thing that must never happen -- see testBoardSelection().
// ---------------------------------------------------------------------------
static int g_reboots = 0;

int fakeRebootCount() { return g_reboots; }

extern "C" void watchdog_reboot(uint32_t, uint32_t, uint32_t) { g_reboots++; }

// ---------------------------------------------------------------------------
// Region fingerprint
//
// Lets a test say "this part of the screen changed" -- or, more usefully,
// "this part of the screen is now pixel-identical to the no-data case" --
// without hard-coding glyph positions that every layout tweak would break.
// ---------------------------------------------------------------------------
uint32_t fakeRegionHash(int x, int y, int w, int h) {
	const uint16_t *fb = gfxBuffer();
	uint32_t hash = 2166136261u;            // FNV-1a
	for (int yy = y; yy < y + h; yy++) {
		for (int xx = x; xx < x + w; xx++) {
			if (xx < 0 || yy < 0 || xx >= GFX_W || yy >= GFX_H) continue;
			hash = (hash ^ fb[yy * GFX_W + xx]) * 16777619u;
		}
	}
	return hash;
}

/** @brief Count framebuffer pixels of an exact colour. @see fakes.h */
int fakeCountColour(uint16_t c) {
	const uint16_t *fb = gfxBuffer();
	int n = 0;
	for (int i = 0; i < GFX_W * GFX_H; i++) if (fb[i] == c) n++;
	return n;
}

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

// ---------------------------------------------------------------------------
// SD logging: a fake card the tests drive directly
// ---------------------------------------------------------------------------
//
// sd_log.cpp itself cannot be linked here -- it pulls in FatFs and the SPI
// driver -- so the UI is tested against this instead. It is not a simulation of
// a card: it is a way to put the logger into a given state and check the screen
// renders it.

static SdLogStatus g_log = {};

void fakeSdLogSet(const SdLogStatus *st) { g_log = *st; }

bool sdLogBegin() {
	g_log.state = SdLogState::Idle;
	return true;
}

static bool g_autoStarted = false;

bool sdLogStart() {
	if (g_log.state == SdLogState::NoCard) return false;
	g_log.state = SdLogState::Logging;
	g_autoStarted = false;
	if (!g_log.fileNumber) g_log.fileNumber = 1;
	return true;
}

void sdLogStop() {
	if (g_log.state == SdLogState::Logging) g_log.state = SdLogState::Idle;
	g_log.fileNumber = 0;
	g_autoStarted = false;
}

bool sdLogRemount() {
	// Models a card that appears on retry: NO CARD becomes READY. That is the
	// case the button exists for -- a card inserted after boot.
	if (g_log.state == SdLogState::NoCard) {
		g_log.state = SdLogState::Idle;
		g_log.mountResult = 0;
		g_log.cardType = 3;          // SDHC/XC
		g_log.cardSizeMB = 122000;   // a 128 GB card, as the card reports itself
		return true;
	}
	return g_log.state != SdLogState::Error;
}

bool sdLogActive() { return g_log.state == SdLogState::Logging; }
void sdLogTick(uint32_t, uint16_t) {}
void sdLogFlush() {}
// Uses the shipped decision function, not a copy of it, so the UI tests
// actually exercise the policy the firmware runs.
static bool g_sdArmed = false;
void sdLogSetArmed(bool armed) {
	SdLogArmAction act = sdLogArmAction(armed, g_sdArmed, sdLogActive(),
	                                    g_autoStarted);
	g_sdArmed = armed;
	switch (act) {
		case SdLogArmAction::Start: g_autoStarted = sdLogStart(); break;
		case SdLogArmAction::Stop:  sdLogStop();                  break;
		case SdLogArmAction::None:                                break;
	}
}
void sdLogStatus(SdLogStatus *out) { *out = g_log; }
