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
 * - 1 — landscape
 * - 2 — portrait, flipped
 * - 3 — landscape, flipped
 *
 * Changing this rotates both the framebuffer geometry (@ref GFX_W / @ref GFX_H)
 * and the touch coordinate mapping.
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

/** @} */

/**
 * @defgroup cfg_debug Debug
 * @{
 */

/** @brief Set to 1 to mirror telemetry to USB serial at 10 Hz. */
#define SERIAL_TELEMETRY       1

/** @} */
