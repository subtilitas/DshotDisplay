/**
 * @file DshotDisplay.ino
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
 *
 * @warning This drives a real motor. Read the safety section of README.md
 *          before connecting anything with a propeller on it.
 */

#include <Arduino.h>

// Implementation lives in src/. The Arduino builder compiles that subdirectory
// recursively, but only adds the sketch root to the include path, so the paths
// are explicit here. Files inside src/ include each other as plain siblings.
#include "src/config.h"
#include "src/board_pins.h"
#include "src/gfx.h"
#include "src/st7789.h"
#include "src/cst816.h"
#include "src/esc_task.h"
#include "src/bridge_wire.h"
#include "src/ui.h"
#include "src/usb_bridge.h"

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
	// Unconditional: the USB bridge and its traffic dump both need this port,
	// and they are useful with telemetry logging switched off. Tying it to
	// SERIAL_TELEMETRY would silently disable the bridge.
	Serial.begin(115200);

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

	s_nextUiMs = millis();
}

/**
 * @brief Core0 loop: run the UI on a fixed cadence, optionally log to serial.
 */
void loop() {
	uint32_t now = millis();
	if ((int32_t)(now - s_nextUiMs) >= 0) {
		s_nextUiMs = now + UI_PERIOD_MS;
		uiTick();
	}

#if SERIAL_TELEMETRY
	// The USB bridge owns the serial port while it runs; telemetry text
	// interleaved with forwarded bytes would corrupt traffic both ways.
	if (!usbBridgeActive() && (int32_t)(millis() - s_nextLogMs) >= 0) {
		s_nextLogMs = millis() + 100;
		EscTelemetry t;
		escSnapshot(&t);
		Serial.printf("arm=%d thr=%4u rpm=%6lu erpm=%6lu %5.2fV %5.1fA %3dC "
		              "stress=%3u st=0x%02X ok=%u bad=%u none=%u rate=%u err=%u%%\n",
		              uiArmed() ? 1 : 0, uiThrottle(),
		              (unsigned long)t.rpm, (unsigned long)t.erpm,
		              t.volts, t.amps, t.tempC, t.stress, t.statusRaw,
		              t.goodPackets, t.badPackets, t.noPackets,
		              t.packetRate, t.errPercent);
	}
#endif
}

/** @brief Core1 setup: claim the PIO state machine for DShot. */
void setup1() {
	escTaskBegin();
}

/**
 * @brief Core1 loop: the DShot frame pump, and the AM32 wire when bridging.
 *
 * Deliberately kept free of anything that can block, so frame spacing stays
 * within tolerance regardless of what core0 is doing.
 *
 * The two are mutually exclusive and both no-op when inactive, so they can be
 * called unconditionally: the bridge only runs once the DShot driver has been
 * suspended and handed over the pin. Both are here for the same reason -- they
 * are bit-timed, and core0 is busy driving a display.
 */
void loop1() {
	escTaskPoll();
	bridgeWirePoll();
}