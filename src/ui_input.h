/**
 * @file ui_input.h
 * @brief Touch idioms shared by every screen: taps, held repeats, pressed state.
 *
 * Three rules live here rather than in each screen, because they were
 * inconsistent between them and the inconsistency was the bug:
 *
 * - **A tap fires on release, not on touch-down.** A capacitive panel has no
 *   travel, so a press that commits immediately gives you no way to notice a
 *   mis-tap and slide off. Every screen except the ARM button used to commit on
 *   touch-down.
 * - **A button under a finger has to look like it.** Only BEEP ever changed
 *   appearance when touched, out of about twenty controls. On glass, a control
 *   that does not react reads as one that did not register.
 * - **A stepper repeats while held, and accelerates.** The AM32 editor did
 *   this and the settings screen did not, so raising the throttle ceiling from
 *   20 % to 100 % was eight separate taps.
 *
 * @see TouchState, which supplies the press position these rules test against.
 */

#pragma once

#include <stdint.h>

struct TouchState;

/**
 * @brief State for one held-to-repeat control. Zero-initialised is idle.
 *
 * One of these per control, not per screen: two steppers on the same screen are
 * independent, and sharing state between them makes a finger sliding from one
 * to the other continue the first one's acceleration.
 */
struct Repeat {
	int      dir;      /**< Direction currently held: -1, 0 or +1. */
	uint32_t startMs;  /**< When the hold began. Drives acceleration. */
	uint32_t lastMs;   /**< When the last step fired. */
};

/**
 * @brief Whether a held stepper should apply a step this frame.
 *
 * Fires once immediately on press, then pauses, then repeats at an accelerating
 * rate. The pause matters: without it a deliberate single tap on a fast-
 * repeating control moves the value two or three steps.
 *
 * @param[in,out] r   State for this control.
 * @param dir         Direction being held: -1, +1, or 0 when the finger is off
 *                    the control, which resets it.
 * @param nowMs       Current millis().
 * @return true if the caller should apply one step of @p dir.
 */
bool repeatFires(Repeat *r, int dir, uint32_t nowMs);

/**
 * @brief Interval between repeats after holding for @p heldMs.
 *
 * Exposed for the tests, which assert the acceleration rather than trusting it:
 * the useful ranges here are wide — a throttle ceiling spans twenty steps and
 * an AM32 motor KV spans 255 — and a repeat that never speeds up is only
 * marginally better than tapping.
 *
 * @param heldMs How long the control has been held.
 * @return Milliseconds until the next step.
 */
uint32_t repeatInterval(uint32_t heldMs);

/**
 * @brief Delay between the first step and the start of repeating, in ms.
 *
 * Long enough that a normal tap produces exactly one step.
 */
#define REPEAT_DELAY_MS 400
