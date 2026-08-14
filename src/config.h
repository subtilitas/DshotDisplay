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
 * @brief Default GPIO for the ESC signal wire. Board-dependent.
 *
 * Runtime-adjustable from **CFG -> SETUP**, which is the intended way to change
 * it; this is what a board with blank flash starts on. @see settings.h
 *
 * On the **RP2350-Touch-LCD-2** this is **GP4**: P2 header pin 11, two
 * positions from GND on P2 pin 13, so a 3-pin servo plug lands
 * SIGNAL / (skip) / GND. The skipped middle position is GP10 (P2 pin 12), an
 * unused camera pin — that is the whole reason GP4 was chosen over the other
 * candidates. See the "Plugging an ESC in directly" section of README.md for
 * why GP20 and GP29 are not usable there.
 *
 * On the **RP2350-Touch-LCD-2.8** this is **GP29**, J4 pin 12 — the last pin on
 * the connector, which is the easy one to find and the easy one to solder to.
 * That board has an RTC, a codec and an SD slot where the other one has a
 * camera header, so GP28 (J4 pin 11) and GP29 are the only two GPIOs left
 * unclaimed. GP28 works identically; build with `-DDSHOT_PIN=28` for it.
 *
 * Getting this wrong is quiet. Nothing errors, nothing warns — the ESC simply
 * never hears a frame, because the firmware is driving a pin no wire is on.
 *
 * Override with `-DDSHOT_PIN=n` or by editing the value here.
 *
 * @warning The middle wire of an ESC lead is the BEC +5 V output, and RP2350
 *          GPIO is **not** 5 V tolerant. Depin or cut that wire before
 *          plugging anything in.
 */
#ifndef DSHOT_PIN
  #if BOARD == BOARD_RP2350_TOUCH_LCD_2
    #define DSHOT_PIN          4
  #else
    #define DSHOT_PIN          29
  #endif
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
 * @brief Default GPIO for the ESC's telemetry pad. Receive only.
 *
 * Runtime-adjustable; this is the starting value. @see settings.h
 *
 * On the **2.0"** this is **GP5**: UART1 RX, P1 header pin 10, and a free
 * camera-bus pin. The other candidates there are GP9 (P1 pin 2), GP21 (P1
 * pin 5) and GP1 (P2 pin 8) — on RP2350 only a GPIO whose number is one more
 * than a multiple of four is a UART RX function at all.
 *
 * On the **2.8"** it is **GP29**, because that is the only free pin on the
 * board that can receive. Note that GP29 is also the default @ref DSHOT_PIN
 * there, which is why @ref DEFAULT_KISS_ENABLE is 0 on that board: the two
 * cannot share a pin, and moving the ESC to GP28 is a decision for whoever
 * soldered the pigtail, not for a default.
 *
 * @warning The KISS spec puts this line at 3.6 V, which is exactly the RP2350's
 *          absolute-maximum GPIO voltage — no margin at all. Most BLHeli_32 and
 *          AM32 ESCs actually drive 3.3 V and are fine, but measure yours
 *          before connecting it, and consider a 1 k series resistor.
 */
#ifndef DEFAULT_KISS_PIN
  #if BOARD == BOARD_RP2350_TOUCH_LCD_2
    #define DEFAULT_KISS_PIN   5
  #else
    #define DEFAULT_KISS_PIN   29
  #endif
#endif

/**
 * @brief Whether the KISS wire is expected by default.
 *
 * Off on the 2.8". That board's only free UART RX pin is GP29, which is also
 * where the ESC signal goes by default, so a default of "on" would have the
 * firmware claim a UART on the pin it is already driving DShot out of.
 *
 * This used to be worse and silent: @ref DEFAULT_KISS_PIN was fixed at GP5 for
 * both boards, and on the 2.8" GP5 is `PIN_RTC_INT` — the PCF85063 alarm
 * output. The firmware duly configured the RTC's interrupt line as a UART
 * receiver and fed whatever it did to the KISS decoder. Nothing errored,
 * because nothing could.
 */
#ifndef DEFAULT_KISS_ENABLE
  #if BOARD == BOARD_RP2350_TOUCH_LCD_2
    #define DEFAULT_KISS_ENABLE 1
  #else
    #define DEFAULT_KISS_ENABLE 0
  #endif
#endif

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
 * "an ESC is connected". Losing it is what re-arms the automatic EDT enable, so
 * that a replacement ESC gets its own.
 *
 * Shorter than @ref EDT_STALE_MS deliberately. eRPM arrives every frame at
 * 1 kHz, where EDT frame types are interleaved and any one of them is rarer.
 */
#define ESC_LINK_STALE_MS      500

/**
 * @brief Abandon a reply that has not completed this long after the request.
 *
 * Only matters for the timeout counter and for discarding a partial frame; the
 * next request resynchronises regardless.
 */
#define KISS_REPLY_TIMEOUT_MS  10

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
