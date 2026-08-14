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
 * RP2350-Touch-LCD-2 and RP2350-Touch-LCD-2.8. Drag the on-screen throttle and
 * the board sends bidirectional DShot to a single ESC while decoding the eRPM
 * and Extended DShot Telemetry that comes back on the same wire.
 *
 * Built either per board with `-DBOARD=`, or as one unified image that carries
 * both board descriptions and picks at run time — from the stored settings, or
 * on a first boot by probing for the board's always-on I2C devices (see
 * board_probe.h). The boards are genuinely different: different SPI instance
 * for the panel, different I2C, a different touch controller, and a different
 * SD interface — hardware SPI on the 2.0", PIO SDIO on the 2.8".
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
 * - @ref board_pins.h — pin map for the selected board, from the schematic
 * - @ref esc_task.h — core1 DShot pump and EDT decode
 * - @ref ui.h — screens, touch handling, arm and throttle state machines
 * - @ref gfx.h — RGB565 framebuffer, dirty bands, fonts
 * - @ref st7789.h — panel init and DMA blitter
 * - @ref touch.h — capacitive touch, CST816D or CST328 by board
 * - @ref sd_log.h — blackbox logging to the microSD slot (core0)
 *
 * @warning This drives a real motor. Read the safety section of README.md
 *          before connecting anything with a propeller on it.
 */

#include "plat.h"
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <hardware/gpio.h>
#include <stdio.h>

#include "config.h"
#include "board_desc.h"
#include "board_probe.h"
#include "gfx.h"
#include "st7789.h"
#include "touch.h"
#include "esc_task.h"
#include "ui.h"
#include "sd_log.h"
#include "settings.h"

/** @brief UI frame interval in milliseconds (~40 fps). */
#define UI_PERIOD_MS 25

/**
 * @brief The power latch, asserted before the board is known.
 *
 * GP26 on the 2.8". SD_SCK on the 2.0", where it is released again as soon as
 * the stored board id has been read. @see setup()
 */
#define BAT_EN_EARLY_PIN 26

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
	// First thing, before anything that could take a millisecond, and before we
	// know which board this is.
	//
	// On the 2.8" GP26 is the latch that keeps VBAT connected once the power
	// button is released; miss it and the board dies mid-boot on battery. That
	// cannot wait for the stored board id, because reading it means reading
	// flash, which takes longer than the button is held.
	//
	// On the 2.0" GP26 is SD_SCK. Driving an idle SD clock high with the card
	// deselected is harmless -- no transaction is in progress and nothing has
	// been initialised -- and it is released again below once the board is
	// known. Asserting it unconditionally is the one thing a unified image
	// cannot defer.
	//
	// @warning Validate this on a 2.0" board before relying on it. It is the
	//          single step in the unified image that touches a pin whose
	//          function differs between the two, and it is reasoned rather than
	//          measured.
	gpio_init(BAT_EN_EARLY_PIN);
	gpio_set_dir(BAT_EN_EARLY_PIN, GPIO_OUT);
	gpio_put(BAT_EN_EARLY_PIN, 1);

#if SERIAL_TELEMETRY
	stdio_init_all();
#endif

	// Before escTaskInit(), which seeds core1's wiring from these, and before
	// uiInit(), which reads the palette and backlight preference. A board with
	// blank flash gets the compiled defaults and never knows the difference.
	//
	// settingsLoad() applies a stored board id itself; after it returns,
	// boardId() is the truth for every configured boot, single-board or
	// unified.
	settingsLoad();

	// The one boot where the board is *not* yet known: a unified image with
	// nothing stored. Identify it by asking its always-on I2C devices — see
	// board_probe.h for why this is safe on either board — then reseed the
	// defaults, which are pin choices and therefore per-board. The answer is
	// deliberately not saved: the SETUP screen shows it and the user's HOLD
	// SAVE is what persists it.
	if (settings()->boardId == BOARD_ID_UNSET) {
		uint8_t probed = boardProbe();
		if (probed != BOARD_ID_UNSET && boardSelect(probed)) {
			settingsDefaults(settings());
		} else {
			// Unknown hardware. Every option from here is a guess, and a
			// guessed pin map drives outputs into other chips' pins — so
			// hold what is safe and do nothing at all: latch kept asserted
			// (releasing it powers off a battery-fed 2.8" mid-boot), no
			// display, no touch, and above all no DShot. Recovery is a
			// reflash over USB, which BOOTSEL provides regardless of how
			// wedged the firmware is.
			for (;;) tight_loop_contents();
		}
	}

	// Hand GP26 back if this board does not want it held. On the 2.0" it is
	// SD_SCK and the card driver claims it shortly afterwards.
	if (g_board->batEnPin != (int8_t)BAT_EN_EARLY_PIN) {
		gpio_set_dir(BAT_EN_EARLY_PIN, GPIO_IN);
		gpio_disable_pulls(BAT_EN_EARLY_PIN);
	}

	// Before anything can call escSnapshot(), and before core1 is launched.
	escTaskInit();

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

	// Paint one real frame before touching the card. Mounting talks to hardware
	// that may not be there, and however well it behaves it is the last thing
	// that runs before the screen would otherwise sit blank. If it ever does
	// stall, a frozen UI says far more than a black panel does.
	uiTick();

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
	// keep the log cadence even. uiTick() reports arm changes itself.
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

/**
 * @brief Core1 setup: opt in to being parked, then start the frame pump.
 *
 * `multicore_lockout_victim_init()` is what lets core0 write flash at all.
 * Erasing a sector turns XIP off, and core1 executes from XIP, so core0 has to
 * be able to hold it still first -- without this the settings save would fault
 * core1 rather than fail, and it would do so only on the boards where somebody
 * had actually pressed the button.
 */
void setup1() {
	multicore_lockout_victim_init();
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
 * Order matters, in two directions.
 *
 * Core1 is launched only after core0 has brought up the display, because
 * escTaskBegin() claims a PIO state machine and st7789Init() claims a DMA
 * channel; starting them concurrently makes which one wins a matter of timing.
 *
 * But core0 must not *use* anything core1 initialises before core1 has run, and
 * it very nearly does: the first loop() calls uiTick(), which calls
 * escSnapshot(), which takes a critical section. So that critical section is
 * initialised by escTaskInit() on core0 inside setup(), not by escTaskBegin()
 * on core1. Getting this wrong does not race intermittently — core0 wins every
 * time, and the board shows the splash and then a black screen forever.
 *
 * @return Never returns.
 */
int main(void) {
	setup();
	multicore_launch_core1(core1Main);
	for (;;) loop();
}