/**
 * @file board_probe.h
 * @brief I2C identification of the board, on every boot of a unified image.
 *
 * board_desc.h used to argue against probing on the grounds that a probe must
 * guess a pin map first, and a wrong guess drives an LCD reset line out of an
 * SD clock pin. That argument is right about *pin-map guessing* and does not
 * apply here, which is why this probe exists and how it is allowed to:
 *
 * - It only ever drives two pins per candidate board, as **open-drain I2C
 *   with pull-ups** — never push-pull, never against a chip output.
 * - The pins it touches are benign on the wrong board by inspection of both
 *   schematics: the 2.8" bus (GP6/GP7) lands on free camera-header pins on
 *   the 2.0"; the 2.0" bus (GP12/GP13) lands on the 2.8"'s idle, pulled-up
 *   LCD MISO/CS lines before the panel is initialised.
 * - It asks for an **address ACK from devices that are always powered and
 *   never sleep** — each board's IMU and, on the 2.8", the RTC. The touch
 *   controllers are probed too, but nothing relies on them: the CST816D is
 *   known to NACK when it autosleeps.
 * - It demands **exactly one** board answers, and asks again a couple of
 *   times before giving up. Anything else — nothing found, or (say) a fitted
 *   camera module back-driving GP6/GP7 into nonsense — is reported as
 *   @ref BOARD_ID_UNSET, and the caller stays in its safe state: power latch
 *   held, no display, no DShot output.
 *
 * This runs on **every** boot, and its answer is the only one there is:
 * nothing stored may override it, and the SETUP screen shows it read-only.
 * A remembered board id that outranked the hardware is what made one wrong tap
 * on a picker cost a reflash — a bad probe fails before a pin is driven and
 * says so, where a bad stored choice is applied by the boot that reads it.
 *
 * @see setup() in main.cpp for the only call site.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Identify the board by probing each candidate's I2C bus.
 *
 * Safe to call once, early in boot, before any peripheral is initialised.
 * Restores every pin it touched to high-impedance before returning.
 *
 * @return The detected board id, or @ref BOARD_ID_UNSET when zero or both
 *         candidates answered.
 */
uint8_t boardProbe(void);

#ifdef __cplusplus
}
#endif
