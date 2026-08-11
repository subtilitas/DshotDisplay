/**
 * @file main.cpp
 * @brief Top level: core0 and core1 entry points.
 */

/**
 * @page architecture Architecture
 *
 * README.md is the front page (`USE_MDFILE_AS_MAINPAGE`), so this is a
 * \@page rather than a second \@mainpage — Doxygen treats two main pages as a
 * conflict and silently drops one. Note the backslashes: naming those commands
 * unescaped in prose makes Doxygen execute them instead of printing them,
 * which is itself how this block first went wrong.
 *
 * A self-contained bidirectional DShot ESC tester for the Waveshare
 * RP2350-Touch-LCD-2. Drag the on-screen throttle and the board sends
 * bidirectional DShot to a single ESC while decoding the eRPM and Extended
 * DShot Telemetry that comes back on the same wire.
 *
 * @section arch Two cores
 *
 * Bidirectional DShot is half-duplex: the ESC only answers a frame that *you*
 * sent, and it disarms if frames stop arriving. So a telemetry display cannot
 * be a passive sniffer — it has to be the throttle source.
 *
 * Frame timing is the whole game, and the display does multi-millisecond SPI
 * DMA bursts that would wreck it if the two shared a core:
 *
 * | | core0 | core1 |
 * |---|---|---|
 * | Job | touch, rendering, battery sense | DShot frame pump, telemetry decode |
 * | Rate | ~40 Hz | 1 kHz (@ref DSHOT_PERIOD_US) |
 * | Allowed to block | yes, freely | never |
 *
 * The two share one small critical-section-guarded struct. See @ref esc_task.h.
 *
 * @section modules Modules
 *
 * All of these live in `src/`:
 *
 * - @ref config.h — every tunable setting
 * - @ref board_pins.h — RP2350-Touch-LCD-2 pin map, from the schematic
 * - @ref esc_task.h — core1 DShot pump and EDT decode
 * - @ref ui.h — screens, touch handling, arm and throttle state machines
 * - @ref gfx.h — RGB565 framebuffer, dirty bands, fonts
 * - @ref st7789.h — panel init and DMA blitter
 * - @ref cst816.h — capacitive touch
 * - @ref sd_log.h — blackbox logging to the microSD slot (core0)
 *
 * @warning This drives a real motor. Read the safety section of README.md
 *          before connecting anything with a propeller on it.
 */

#include "plat.h"
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <stdio.h>

#include "config.h"
#include "board_pins.h"
#include "gfx.h"
#include "st7789.h"
#include "cst816.h"
#include "esc_task.h"
#include "ui.h"
#include "sd_log.h"

/** @brief UI frame interval in milliseconds (~40 fps). */
#define UI_PERIOD_MS 25

static uint32_t s_nextUiMs = 0;   /**< Deadline for the next UI frame. */
#if SERIAL_TELEMETRY
static uint32_t s_nextLogMs = 0;  /**< Deadline for the next serial dump. */
#endif

/**
 * @brief Core0 setup: bring up the display, touch and UI.
 *
 * Order matters — st7789Init() pulses the reset line the touch controller
 * shares, so touchInit() has to follow it.
 */
void setup() {
#if SERIAL_TELEMETRY
	stdio_init_all();
#endif

	gfxInit();
	st7789Init();       // also pulses the shared LCD/touch reset line
	touchInit();
	uiInit();

	// splash while core1 brings the PIO up and the ESC finishes booting
	uiDrawSplash();
	st7789FlushDirty();
	delay(SPLASH_MS);
	gfxFill(C_BG);
	st7789FlushDirty();

#if SD_LOG_ENABLE
	// A missing card is the normal bench case, not a fault: logging is
	// simply unavailable and the UI says so.
	sdLogBegin();
#endif

	s_nextUiMs = millis();
}

/**
 * @brief Core0 loop: UI, SD logging, and the optional serial dump.
 */
void loop() {
	uint32_t now = millis();
	if ((int32_t)(now - s_nextUiMs) >= 0) {
		s_nextUiMs = now + UI_PERIOD_MS;
		uiTick();
	}

#if SD_LOG_ENABLE
	// Encoding is cheap and only fills a RAM ring, so it runs every pass to
	// keep the log cadence even.
	sdLogSetArmed(uiArmed());
	sdLogTick(micros(), uiThrottle());

	// Writing is the expensive half -- a card can pause tens of milliseconds
	// for an internal erase -- so it is a separate call, made once per pass
	// after the UI has already had its turn. Never on core1: a stall there
	// would break DShot timing outright.
	sdLogFlush();
#endif

#if SERIAL_TELEMETRY 
	if ((int32_t)(millis() - s_nextLogMs) >= 0) {
		s_nextLogMs = millis() + 100;
		EscTelemetry t;
		escSnapshot(&t);
		// uint32_t is `unsigned long` on arm-none-eabi, so the packet counters
		// are cast rather than printed with %u -- the Arduino core's printf was
		// lenient about this and newlib's is not.
		printf("arm=%d thr=%4u rpm=%6lu erpm=%6lu %5.2fV %5.1fA %3dC "
		       "stress=%3u st=0x%02X ok=%lu bad=%lu none=%lu rate=%u err=%u%%\n",
		       uiArmed() ? 1 : 0, uiThrottle(),
		       (unsigned long)t.rpm, (unsigned long)t.erpm,
		       (double)t.volts, (double)t.amps, t.tempC, t.stress, t.statusRaw,
		       (unsigned long)t.goodPackets, (unsigned long)t.badPackets,
		       (unsigned long)t.noPackets,
		       t.packetRate, t.errPercent);
	}
#endif
}

/** @brief Core1 setup: claim the PIO state machine for DShot. */
void setup1() {
	escTaskBegin();
}

/**
 * @brief Core1 loop: nothing but the DShot frame pump.
 *
 * Deliberately kept free of anything that can block, so frame spacing stays
 * within tolerance regardless of what core0 is doing.
 */
void loop1() {
	escTaskPoll();
}

/**
 * @brief Core1 entry point.
 *
 * The Arduino core used to call setup1()/loop1() for us. Doing it explicitly is
 * the whole point of the port: nothing runs on core1 that is not written here.
 */
static void core1Main() {
	setup1();
	for (;;) loop1();
}

/**
 * @brief Core0 entry point.
 *
 * Order matters. Core1 is launched only after core0 has finished bringing up
 * the display and the UI, because escTaskBegin() claims a PIO state machine and
 * st7789Init() claims a DMA channel — starting them concurrently makes which
 * one wins a matter of timing.
 *
 * @return Never returns.
 */
int main(void) {
	setup();
	multicore_launch_core1(core1Main);
	for (;;) loop();
}