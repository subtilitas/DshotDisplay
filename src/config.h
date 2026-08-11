/**
 * @file config.h
 * @brief User-tunable settings for DshotDisplay.
 *
 * Everything in this file is safe to change without touching the rest of the
 * firmware. Anything that describes the board itself rather than a preference
 * lives in @ref board_pins.h instead.
 */

#pragma once

/**
 * @defgroup cfg_esc ESC connection
 * @brief Which pin the ESC is on and how fast we talk to it.
 * @{
 */

/**
 * @brief GPIO the ESC signal wire goes to.
 *
 * GP4 is P2 header pin 11, two positions from GND on P2 pin 13, so a 3-pin
 * servo plug lands SIGNAL / (skip) / GND. The skipped middle position is GP10
 * (P2 pin 12), an unused camera pin — that is the whole reason GP4 was chosen
 * over the other candidates. See the "Plugging an ESC in directly" section of
 * README.md for why GP20 and GP29 are not usable.
 *
 * @warning The middle wire of an ESC lead is the BEC +5 V output, and RP2350
 *          GPIO is **not** 5 V tolerant. Depin or cut that wire before
 *          plugging anything in.
 */
#define DSHOT_PIN              4

/**
 * @brief DShot bitrate in kBaud. 300 / 600 / 1200 are the common ones.
 *
 * Bidirectional DShot is more timing-sensitive than normal DShot. If telemetry
 * is flaky on a long or unshielded signal wire, drop to 300 before suspecting
 * the firmware.
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
 * @brief GPIO the ESC's telemetry pad connects to. Receive only.
 *
 * GP5 is UART1 RX and lands on P1 header pin 10. UART1 is chosen over UART0 so
 * the USB-serial debug path is left alone. In arduino-pico, UART1 is `Serial2`.
 *
 * Other candidates, all free and broken out: GP9 (UART1 RX, P1 pin 2), GP21
 * (UART1 RX, P1 pin 5), GP1 (UART0 RX, P2 pin 8).
 *
 * @warning The KISS spec puts this line at 3.6 V, which is exactly the RP2350's
 *          absolute-maximum GPIO voltage — no margin at all. Most BLHeli_32 and
 *          AM32 ESCs actually drive 3.3 V and are fine, but measure yours
 *          before connecting it, and consider a 1 k series resistor.
 */
#define KISS_TELEM_PIN         5

/**
 * @brief Arduino serial object for @ref KISS_TELEM_PIN.
 *
 * arduino-pico maps `Serial1` to UART0 and `Serial2` to UART1. This must agree
 * with the pin: GP5 is a UART1 RX pin, so `Serial2`. Changing @ref
 * KISS_TELEM_PIN to a UART0 pin such as GP1 means changing this to `Serial1`.
 */
#define KISS_SERIAL            Serial2

/**
 * @brief Request telemetry on every Nth DShot frame.
 *
 * A KISS frame occupies ~870 us on the wire and the ESC begins sending it about
 * 900 us after the request, so at the 1 kHz @ref DSHOT_PERIOD_US frame rate
 * anything below about 2 would overlap replies. 20 gives 50 Hz, far faster than
 * the display repaints, and leaves the wire idle most of the time.
 */
#define KISS_REQUEST_EVERY_N   20

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
 * @brief Abandon a reply that has not completed this long after the request.
 *
 * Only matters for the timeout counter and for discarding a partial frame; the
 * next request resynchronises regardless.
 */
#define KISS_REPLY_TIMEOUT_MS  10

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
