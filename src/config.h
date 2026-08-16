/**
 * @file config.h
 * @brief User-tunable settings for DshotDisplay.
 *
 * Everything in this file is safe to change without touching the rest of the
 * firmware. Anything that describes the board itself rather than a preference
 * lives in @ref board_pins.h instead, and which board you are building for is
 * @ref board.h.
 *
 * @section config_defaults Some of these are only defaults now
 *
 * The ESC pin, the DShot bitrate, the KISS wire, the pole count, the throttle
 * ceiling and the backlight are all changeable on the board itself and stored
 * in flash — see @ref settings.h and the **CFG -> SETUP** screen. What is
 * written here is the value a board starts with when its flash is blank, or
 * when a stored block fails to validate.
 *
 * The practical consequence: editing one of those values and reflashing will
 * appear to do nothing on a board that has saved settings, because the stored
 * value wins. The SETUP screen marks each row `SAVED` or `DEFAULT` so it is
 * visible which one you are looking at.
 */

#pragma once

#include "board.h"

/**
 * @defgroup cfg_esc ESC connection
 * @brief Which pin the ESC is on and how fast we talk to it.
 * @{
 */

/**
 * @defgroup cfg_pin_defaults Per-board pin defaults
 * @brief What each board starts its ESC and telemetry wires on.
 *
 * One set per board, defined unconditionally rather than selected by
 * @ref BOARD — and that is the entire point of this group.
 *
 * They used to be single macros chosen with `#if BOARD == ...`, which cannot
 * express two boards at once. A unified image had no way to say "GP4 on the
 * 2.0-inch board, GP29 on the 2.8-inch one", so the descriptors ended up
 * carrying literals and the macros quietly stopped being read by anything at
 * all: `-DDSHOT_PIN=28` configured cleanly, printed a status line saying it had
 * taken, and left the image driving GP29. CI even had a job asserting the
 * option was honoured, and it passed — it was grepping CMake's own output.
 *
 * So each descriptor reads its own set: board_desc_lcd2.cpp takes the `_LCD_2`
 * values, board_desc_lcd2_8.cpp the `_LCD_2_8` ones. **Which set a running
 * board uses is decided at boot**, not at build time — boardProbe() identifies
 * the hardware, boardSelect() points `g_board` at that descriptor, and
 * settingsDefaults() reads the defaults out of it. A unified image carries both
 * and picks between them; a single-board image never compiles the other.
 *
 * Override any of them from the build:
 *
 *     cmake -B build -DBOARD=BOARD_UNIFIED -DDSHOT_PIN_LCD_2_8=28 .
 *
 * @note An override outside its board's `BOARD_FREE_GPIO_MASK`, or one putting
 *       both wires on the same pin, is a `static_assert` in the descriptor. It
 *       fails the build rather than being repaired at run time by
 *       @ref settingsValidate(), which is quiet by design.
 * @{
 */

/**
 * @brief ESC signal pin the 2.0" board starts on. **GP4**.
 *
 * Runtime-adjustable from **CFG -> SETUP**, which is the intended way to change
 * it; this is what a board with blank flash starts on. @see settings.h
 *
 * P2 header pin 11, two positions from GND on P2 pin 13, so a 3-pin servo plug
 * lands SIGNAL / (skip) / GND. The skipped middle position is GP10 (P2 pin 12),
 * an unused camera pin — that is the whole reason GP4 was chosen over the other
 * candidates. See the "Plugging an ESC in directly" section of README.md for
 * why GP20 and GP29 are not usable there.
 *
 * @warning The middle wire of an ESC lead is the BEC +5 V output, and RP2350
 *          GPIO is **not** 5 V tolerant. Depin or cut that wire before
 *          plugging anything in.
 */
#ifndef DSHOT_PIN_LCD_2
  #define DSHOT_PIN_LCD_2      4
#endif

/**
 * @brief ESC signal pin the 2.8" board starts on. **GP29**.
 *
 * J4 pin 12 — the last pin on the connector, which is the easy one to find and
 * the easy one to solder to. That board has an RTC, a codec and an SD slot
 * where the other one has a camera header, so GP28 (J4 pin 11) and GP29 are the
 * only two GPIOs left unclaimed; the telemetry wire takes the other one.
 * @see KISS_PIN_LCD_2_8
 *
 * Getting this wrong is quiet. Nothing errors, nothing warns — the ESC simply
 * never hears a frame, because the firmware is driving a pin no wire is on.
 */
#ifndef DSHOT_PIN_LCD_2_8
  #define DSHOT_PIN_LCD_2_8   29
#endif

/**
 * @brief Default DShot bitrate in kBaud. 150 / 300 / 600 / 1200 are legal.
 *
 * Runtime-adjustable from **CFG -> SETUP**. That matters more than it sounds:
 * bidirectional DShot is more timing-sensitive than normal DShot, and "drop to
 * 300 before suspecting the firmware" is advice you want to be able to take on
 * the bench with the flaky wire still connected, not after a toolchain
 * round-trip.
 */
#define DSHOT_SPEED_KBAUD      600

/**
 * @brief Interval between DShot frames on core1, in microseconds.
 *
 * The ESC times out and disarms if it stops hearing frames, so this must stay
 * well above 500 Hz. Default is 1 kHz.
 */
#define DSHOT_PERIOD_US        1000

/** @} */

/**
 * @defgroup cfg_motor Motor and telemetry
 * @{
 */

/**
 * @brief Magnet (pole) count of the motor under test.
 *
 * Used for the eRPM to RPM conversion, `RPM = eRPM / (poles / 2)`. A "2207"
 * style quad motor is almost always 14. Adjustable at runtime in the CFG
 * screen.
 */
#define DEFAULT_MOTOR_POLES    14

/** @brief Lower bound for the runtime pole-count adjuster. */
#define MIN_MOTOR_POLES        2

/** @brief Upper bound for the runtime pole-count adjuster. */
#define MAX_MOTOR_POLES        28

/** @} */

/**
 * @defgroup cfg_kiss KISS telemetry
 * @brief The separate telemetry wire. Optional; the firmware works without it.
 *
 * Extended DShot Telemetry gives voltage in 0.25 V steps and current in whole
 * amps, which is too coarse to see a 200 mV sag or measure idle draw. A KISS
 * ESC will send 0.01 V and 0.01 A over a dedicated wire if the DShot frame asks
 * for it. See docs/design/kiss-telemetry.md.
 * @{
 */

/**
 * @brief Compile the KISS telemetry path in at all.
 *
 * `#ifndef` so a build can override it with `-D`; the host tests compile
 * esc_task.cpp both ways to keep the disabled path from rotting.
 */
#ifndef KISS_TELEM_ENABLE
#define KISS_TELEM_ENABLE      1
#endif

/**
 * @brief Telemetry pin the 2.0" board starts on. **GP5**, P1 header pin 10.
 *
 * A free camera-bus pin. Runtime-adjustable, and any free GPIO will do: the
 * receiver is a PIO state machine rather than one of the two hardware UARTs,
 * so there is no "but only these eight pins can receive". @see pio_uart_rx.h
 *
 * @warning The KISS spec puts this line at 3.6 V, which is exactly the RP2350's
 *          absolute-maximum GPIO voltage — no margin at all. Most BLHeli_32 and
 *          AM32 ESCs actually drive 3.3 V and are fine, but measure yours
 *          before connecting it, and consider a 1 k series resistor.
 */
#ifndef KISS_PIN_LCD_2
  #define KISS_PIN_LCD_2       5
#endif

/**
 * @brief Telemetry pin the 2.8" board starts on. **GP28**, J4 pin 11.
 *
 * The other of that board's two free pins; @ref DSHOT_PIN_LCD_2_8 has GP29.
 * With only two to go round, one default settles the other, and the descriptor
 * static_asserts that they did not end up the same pin.
 */
#ifndef KISS_PIN_LCD_2_8
  #define KISS_PIN_LCD_2_8    28
#endif

/**
 * @brief Whether the 2.0" board expects a KISS wire. **On**.
 *
 * It has a camera header's worth of spare pins, so nothing is being spent to
 * listen on one.
 */
#ifndef KISS_ENABLE_LCD_2
  #define KISS_ENABLE_LCD_2    1
#endif

/**
 * @brief Whether the 2.8" board expects a KISS wire. **Off**.
 *
 * Not because it cannot — GP28 is free and the PIO receiver will take it — but
 * because that board brings out exactly two pins, and defaulting to on spends
 * the spare one on a wire most people have not soldered. One tap turns it on.
 *
 * This used to be worse and silent: the KISS pin was fixed at GP5 for both
 * boards, and on the 2.8" GP5 is `PIN_RTC_INT` — the PCF85063 alarm output. The
 * firmware duly configured the RTC's interrupt line as a UART receiver and fed
 * whatever it did to the KISS decoder. Nothing errored, because nothing could.
 */
#ifndef KISS_ENABLE_LCD_2_8
  #define KISS_ENABLE_LCD_2_8  0
#endif

/** @} */

/**
 * @brief Request telemetry on every Nth DShot frame.
 *
 * A KISS frame occupies ~870 us on the wire and the ESC begins sending it about
 * 900 us after the request, so at the 1 kHz @ref DSHOT_PERIOD_US frame rate
 * anything below about 2 would overlap replies. 20 gives 50 Hz, far faster than
 * the display repaints, and leaves the wire idle most of the time.
 */
#define KISS_REQUEST_EVERY_N   20
#if KISS_REQUEST_EVERY_N < 2
#error "KISS_REQUEST_EVERY_N must be >= 2 to avoid overlapping KISS replies"
#endif

/**
 * @brief How long a KISS frame stays authoritative, in milliseconds.
 *
 * Past this, the merged reading falls back to EDT. Unplugging the telemetry
 * wire mid-session should visibly drop back to coarse values rather than freeze
 * the last fine ones, which would look like a working sensor reporting a
 * perfectly steady pack.
 */
#define KISS_STALE_MS          500

/**
 * @brief How long an EDT field stays valid after its last frame, in ms.
 *
 * Past this the field blanks to `--` instead of holding its last value. The
 * same argument as @ref KISS_STALE_MS, one level down: unplug the ESC, or swap
 * it for a different one, and the old voltage, current, temperature and stress
 * would otherwise sit there indefinitely, indistinguishable from live readings.
 *
 * A second is many EDT frames. The ESC interleaves the frame types and the full
 * cycle completes in a handful of milliseconds at 1 kHz, so this only expires
 * when telemetry has genuinely stopped, not between two frames of the same
 * kind. It is deliberately longer than @ref KISS_STALE_MS — EDT is the fallback,
 * and a fallback that expires first is no fallback.
 */
#define EDT_STALE_MS          1000

/**
 * @brief How long without an eRPM frame before the ESC counts as gone, in ms.
 *
 * eRPM is plain bidirectional DShot — every ESC that works at all answers with
 * it, whether or not it supports EDT — so its presence is the definition of
 * "an ESC is connected". It is also half of the retry condition for the
 * automatic EDT enable: an ESC that is answering but sending no EDT is an ESC
 * that has not taken the enable yet. @see EDT_RETRY_MS
 *
 * Shorter than @ref EDT_STALE_MS deliberately. eRPM arrives every frame at
 * 1 kHz, where EDT frame types are interleaved and any one of them is rarer.
 */
#define ESC_LINK_STALE_MS      500

/**
 * @brief How long before an unanswered EDT enable is sent again, in ms.
 *
 * The enable is not a one-shot, and used to be. It went out on the very first
 * eRPM frame — the earliest possible moment, and the worst one: an ESC answers
 * eRPM within milliseconds of power-up but will not act on a DShot command
 * until it has seen a run of valid zero-throttle frames. Miss that window and
 * nothing tried again, so the ESC reported RPM and nothing else, which is
 * indistinguishable from an ESC with no EDT support. Changing any wiring
 * setting rebuilt the pump, cleared the flag and made EDT "start working",
 * which is how the bug was found rather than how it was meant to work.
 *
 * So the rule is now the obvious one: while an ESC is answering
 * (@ref ESC_LINK_STALE_MS) and no EDT frame is arriving (@ref EDT_STALE_MS),
 * ask again this often. Long enough that ten command frames are a rounding
 * error in the stream; short enough that an ESC plugged in mid-session has EDT
 * before anyone has finished looking at the screen.
 */
#define EDT_RETRY_MS          1000

/**
 * @brief How long an ESC must have been answering before the first enable, in ms.
 *
 * The third version of this rule, and the reason for it is the criticism the
 * second one wrote down about the first and then repeated. An ESC answers eRPM
 * within milliseconds of power-up and will not act on a DShot command until it
 * has seen a run of valid zero-throttle frames, so firing on the very first
 * eRPM frame spends the attempt at close to the least likely moment for it to
 * be taken — and the retry loop that was supposed to cover for that inherited
 * a `tried` flag saying an attempt had been made, so the next one was a whole
 * @ref EDT_RETRY_MS away.
 *
 * Waiting instead costs a third of a second on connect and makes the first
 * attempt the one most likely to work, which matters because every attempt
 * after it is one the user is already watching a blank tile through.
 */
#define EDT_SETTLE_MS          300

/**
 * @brief How many consecutive frames one EDT enable is sent for.
 *
 * The spec asks for the same command in ten successive frames. The number
 * matters less than the "successive" — a burst broken part-way through is a
 * burst no ESC counts, and this used to share one queue slot with the beacon
 * command, so pressing BEEP while an enable was going out replaced the tail of
 * it and neither command reached its repeat count. @see esc_cmd
 */
#define EDT_ENABLE_REPEATS      10

/** @brief How many consecutive frames one beacon command is sent for. */
#define BEEP_REPEATS             6

/**
 * @brief Abandon a reply that has not completed this long after the request.
 *
 * Only matters for the timeout counter and for discarding a partial frame; the
 * next request resynchronises regardless.
 */
#define KISS_REPLY_TIMEOUT_MS  10

/**
 * @brief How long a refused USB DRIVE press keeps its explanation on screen.
 *
 * Longer than @ref CMD_FLASH_MS, because this one is a sentence rather than a
 * colour: the reason has to be read, and the thing it tells you to do -- disarm,
 * or stop the recording -- takes a moment to act on. Short enough that it is
 * gone before you next look at the screen for something else.
 */
#define MSC_NOTE_MS          4000

/**
 * @brief How hard the RPM readout is smoothed, as a power of two.
 *
 * Each UI frame moves the displayed value `1/2^n` of the way to the reading.
 * At @ref UI_PERIOD_MS that makes 3 settle in roughly a fifth of a second:
 * slower than the quantisation flicker it exists to remove, faster than any
 * throttle change you would make by hand.
 *
 * Raise it if the number still dances, lower it if the display feels like it is
 * lagging the motor. 0 disables the filter entirely — every sample is shown as
 * it arrives, which is what every version before this one did.
 *
 * The **display only**. Nothing in the blackbox log is filtered. @see rpm_filter.h
 */
#define RPM_FILTER_SHIFT        3

/** @} */

/**
 * @defgroup cfg_log SD card logging
 * @brief Betaflight blackbox logs on the microSD slot.
 *
 * The card is on SPI1 with its own pins, so it does not contend with the
 * display on SPI0 for a peripheral — only for core0's time.
 * See docs/design/blackbox-logging.md.
 * @{
 */

/** @brief Compile the SD logging path in at all. */
#ifndef SD_LOG_ENABLE
#define SD_LOG_ENABLE          1
#endif

/**
 * @brief SDIO clock for the card, in Hz. Used only on boards wired for SDIO.
 *
 * The 2.8" board runs the card four bits wide over PIO rather than one bit over
 * SPI, so this is the rate that matters there and @ref SD_LOG_SPI_MHZ is
 * ignored. Conservative to start: SDIO is far more sensitive to signal
 * integrity than 12 MHz SPI, and a card that enumerates but corrupts is worse
 * than one that refuses.
 */
#define SD_LOG_SDIO_HZ         (10 * 1000 * 1000)

/**
 * @brief SPI clock for the card, in MHz. Used only on boards wired for SPI.
 *
 * Cards are specified to 25 MHz in SPI mode. Starting conservatively: a card
 * that misbehaves at speed fails in ways that look like corruption rather than
 * like a bus problem, which is a miserable thing to debug.
 */
#define SD_LOG_SPI_MHZ         12

/**
 * @brief Log frame rate, in Hz.
 *
 * Not the DShot rate. eRPM is the only genuinely 1 kHz signal here; KISS
 * arrives at 50 Hz and the display repaints far slower. At ~14.5 bytes/frame
 * measured, 500 Hz is about 7.3 kB/s or 26 MB/hour.
 */
#define SD_LOG_RATE_HZ         500

/**
 * @brief I frames every this many frames.
 *
 * I frames are the resynchronisation points: a decoder joining mid-file, or
 * recovering from a damaged region, cannot do anything until it reaches one.
 * 32 is Betaflight's usual value.
 */
#define SD_LOG_I_INTERVAL      32

/**
 * @brief Ring buffer size, in bytes.
 *
 * Has to cover the worst write stall the card produces, times the byte rate.
 * At 7.3 kB/s an 8 kB buffer absorbs roughly a second, which should be ample
 * for an erase pause — but this is a guess until measured. @ref LogRing tracks
 * a high-water mark; the UI shows it, and it is the number to size this from.
 */
#define SD_LOG_BUFFER_BYTES    8192

/**
 * @brief Flush granularity, in bytes. Should be a multiple of 512.
 *
 * Cards erase in blocks and write whole sectors regardless; handing over
 * sector-aligned chunks avoids read-modify-write cycles inside the card.
 */
#define SD_LOG_CHUNK_BYTES     512

/**
 * @brief Contiguous space reserved per log file, in bytes.
 *
 * Pre-allocating means the FAT is not updated mid-log, which is where a long
 * unpredictable stall would otherwise come from. 16 MB is about ten minutes at
 * the default rate. Unused space is released when the file is closed.
 */
#define SD_LOG_PREALLOC_BYTES  (16u * 1024u * 1024u)

/**
 * @brief Commit the log file's directory entry at most this often, in ms.
 *
 * A log is only readable up to its last f_sync(). Without a periodic one the
 * only sync is at sdLogStop(), so pulling the battery mid-run -- which the
 * safety section of README.md recommends having a way to do -- loses the entire
 * file rather than the last couple of seconds.
 *
 * Cheap because the file is pre-allocated contiguously: the FAT is not being
 * extended, only the directory entry rewritten. Taken right after a chunk goes
 * out, when the ring is at its emptiest.
 */
#define SD_LOG_SYNC_MS         2000

/**
 * @brief Start logging automatically when the tester arms.
 *
 * Manual start/stop is always available. With this on, arming also starts a log
 * and disarming closes it, so a run cannot be missed by forgetting to press
 * record.
 */
#define SD_LOG_AUTO_ON_ARM     1

/** @} */

/**
 * @defgroup cfg_safety Safety
 * @brief Interlocks. Read these before raising any of them.
 * @{
 */

/**
 * @brief Throttle ceiling, out of the 0..2000 DShot range.
 *
 * Starts deliberately low — a motor on the bench with a prop fitted is
 * genuinely dangerous. Adjustable at runtime in the CFG screen.
 */
#define DEFAULT_MAX_THROTTLE   400

/** @brief Step size of the runtime throttle-ceiling adjuster. */
#define MAX_THROTTLE_STEP      100

/** @brief Largest ceiling the runtime adjuster will allow. */
#define MAX_THROTTLE_CEILING   2000

/** @brief How long the ARM button must be held before the tester arms. */
#define ARM_HOLD_MS            1000

/**
 * @brief Drag sensitivity on the throttle gauge in HOLD mode, as a percentage.
 *
 * HOLD mode moves the throttle *relatively* — by how far your finger
 * travelled, not to where it landed — so touching the bar never makes the
 * motor jump. 100 means one track width of travel covers the full ceiling.
 * Lower values give finer control and let you build throttle up over several
 * swipes.
 *
 * @see relativeThrottle()
 */
#define HOLD_DRAG_SENSITIVITY_PCT 100

/**
 * @brief Sensitivity of the large throttle pad, as a percentage.
 *
 * The whole number-display area above the gauge (RPM readout plus telemetry
 * tiles) doubles as a relative throttle pad: swipe up for more, down for less.
 * It is *always* relative regardless of HOLD, because a big blank area has no
 * positional meaning — a tap on it must never move the motor.
 *
 * 100 means one full-height swipe covers the whole ceiling. Lower is finer;
 * swipes accumulate, so nothing is out of reach.
 */
#define PAD_DRAG_SENSITIVITY_PCT  60

/**
 * @brief Travel in pixels before a pad touch counts as a drag.
 *
 * Stops taps, thumb roll and panel jitter from creeping the throttle. The
 * deadzone is consumed before the anchor is set, so the first few pixels of a
 * swipe produce no movement by design.
 */
#define PAD_DEADZONE_PX           6

/** @brief Arming is refused unless the throttle has been at zero this long. */
#define ARM_ZERO_THROTTLE_MS   250

/**
 * @brief Core1 forces throttle to zero if core0 stops checking in this long.
 *
 * Protects against a hung or crashed UI leaving a motor spinning.
 * @see escHeartbeat()
 */
#define UI_HEARTBEAT_TIMEOUT_MS 250

/** @brief Auto-disarm after this long with no touch input at all. 0 disables. */
#define IDLE_DISARM_MS         30000

/** @} */

/**
 * @defgroup cfg_display Display
 * @{
 */

/**
 * @brief Panel orientation.
 *
 * - 0 — portrait, USB port at the bottom (default)
 * - 1 — landscape *(not supported, see below)*
 * - 2 — portrait, flipped
 * - 3 — landscape, flipped *(not supported)*
 *
 * Changing this rotates both the framebuffer geometry (@ref GFX_W / @ref GFX_H)
 * and the touch coordinate mapping.
 *
 * @warning Only the portrait values work. The screen layout is a fixed set of
 *          pixel rows in a 240x320 frame, so landscape puts the buttons off the
 *          bottom of the panel. A static_assert in ui.cpp enforces this rather
 *          than letting it fail silently on hardware; laying the UI out
 *          responsively would be the fix if landscape is ever wanted.
 */
#define LCD_ROTATION           0

/**
 * @brief SPI clock for the panel, in Hz.
 *
 * ST7789 is specified to 62.5 MHz for writes. The RP2350 SPI divides sysclk by
 * an even integer, so the achieved rate is the nearest one at or below this.
 */
#define LCD_SPI_HZ             62500000u

/** @brief Startup backlight brightness, 0..255 (PWM duty). */
#define LCD_BACKLIGHT_DEFAULT  200

/**
 * @brief How long the splash screen stays up, in milliseconds.
 *
 * This also covers core1 claiming its PIO state machine and the ESC finishing
 * its own boot, so shortening it much below a second is not advisable.
 */
#define SPLASH_MS              3000

/** @} */

/**
 * @defgroup cfg_debug Debug
 * @{
 */

/** @brief Set to 1 to mirror telemetry to USB serial at 10 Hz. */
#define SERIAL_TELEMETRY       1

/** @} */

/**
 * @defgroup cfg_am32 AM32 configuration mode
 * @brief Timings for reaching an ESC's bootloader. Tune these first if the
 *        config screen never connects.
 * @{
 */

/**
 * @brief Set to 1 to try forcing the bootloader by holding the signal low.
 *
 * Off by default. The reference configurator does no such thing — it simply
 * repeats the init string and relies on the window the bootloader opens at
 * power-up. Holding the line low also makes us deaf for the duration, which
 * costs more windows than it creates.
 */
#define AM32_FORCE_LOW_JUMP    0

/** @brief How long to hold the signal low when AM32_FORCE_LOW_JUMP is set. */
#define AM32_BOOT_LOW_MS       200

/** @brief Settling time after releasing the line, before the greeting is sent. */
#define AM32_BOOT_SETTLE_MS    50

/**
 * @brief Timeout for the first reply byte of a handshake, in milliseconds.
 *
 * Kept short so a failed attempt costs little: catching the power-up window
 * depends on how often we can retry, not on how patiently we wait.
 */
#define AM32_GREET_TMO_MS      30

/**
 * @brief Pause after a settings write before reading it back.
 *
 * The bootloader is erasing and programming a flash page and will not answer
 * until it finishes. Reading too soon is what makes a good write look failed.
 */
#define AM32_WRITE_SETTLE_MS   300

/**
 * @brief Set to 1 to drive the line push-pull, 0 for open-drain plus pull-up.
 *
 * Push-pull gives clean fast edges. Open-drain is the safer choice if you are
 * unsure whether the ESC might drive the line at the same time, and is worth
 * trying if the link is unreliable on a long signal wire.
 */
#define AM32_PUSH_PULL_TX      1

/**
 * @brief Settling gap between bootloader commands, in milliseconds.
 *
 * The reference configurator waits 25 ms after each write. The bootloader is
 * doing flash operations between commands and will drop anything that arrives
 * while it is busy.
 */
#define AM32_CMD_GAP_MS        25

/** @brief Extra gap for settings-page operations, which are slower still. */
#define AM32_EEPROM_GAP_MS     50

/** @brief Log every AM32 transport step to USB serial. */
#define AM32_DEBUG             1

/** @} */
