/**
 * @file rpm_filter.h
 * @brief Smoothing for the RPM readout. Pure; compiled into the host suite.
 *
 * @section rpmf_why What is jumpy, and why
 *
 * Bidirectional DShot does not send RPM. It sends the *period* between
 * commutations, in a 9-bit mantissa and a 3-bit exponent, and RPM is what you
 * get after dividing by it. Division by a quantised period is coarse at exactly
 * the end you care about: at 20 000 eRPM one least-significant bit of period is
 * worth a few hundred eRPM, so a perfectly steady motor produces a readout that
 * flickers between two or three neighbouring values. Add the real
 * commutation-to-commutation jitter of a motor under load and the last two
 * digits become unreadable.
 *
 * So the number moves for two reasons, and neither of them is the motor's speed
 * changing. Averaging over a couple of hundred milliseconds removes both.
 *
 * @section rpmf_where Where this is and is not applied
 *
 * The **display only**. The blackbox log records what the ESC actually said,
 * unfiltered, because a log is a measurement and a measurement that has been
 * quietly smoothed is worse than a noisy one — you cannot un-filter it later,
 * and logwiju can filter it any way you like. The same goes for the packet
 * counters and everything else derived from the raw frame.
 *
 * @section rpmf_honest What it must not do
 *
 * A filter is a small lie about the present in exchange for a readable number,
 * and there are two lies it must not tell:
 *
 * - **It must not ease in from zero.** A spin-up that starts the average at 0
 *   would show a number climbing through values the motor never turned at, for
 *   as long as the time constant. So the first sample is taken whole.
 * - **It must not survive the link.** If telemetry stops, the filter is reset
 *   rather than left holding an average of data that is no longer arriving —
 *   which is the same rule the telemetry tiles follow when they blank to `--`.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief State for one smoothed value. Zero-initialised is idle. */
struct RpmFilter {
	uint32_t acc;     /**< Current average. */
	bool     primed;  /**< False until the first sample has been taken whole. */
};

/** @brief Forget everything. @param f Filter state. */
static inline void rpmFilterReset(RpmFilter *f) {
	f->acc = 0;
	f->primed = false;
}

/**
 * @brief Fold one sample in and return the value to display.
 *
 * An exponential moving average, in integers, with a shift for the rate: each
 * call moves the average `1 / 2^shift` of the way to the sample. At the UI's
 * frame rate a shift of 3 settles in about a fifth of a second, which is slower
 * than the flicker and faster than a throttle change.
 *
 * The small-delta snap at the end is not an optimisation. Plain integer
 * division truncates, so once the difference is below `2^shift` the average
 * stops moving and parks a few RPM short of the truth — permanently, and
 * differently depending on which side it approached from. Snapping guarantees
 * it converges on a steady input.
 *
 * @param[in,out] f      Filter state.
 * @param sample         This frame's reading.
 * @param alive          False when telemetry has gone stale; resets the filter.
 * @param shift          Rate, as a power of two. @see RPM_FILTER_SHIFT
 * @return What to display: 0 when not alive, the sample when first primed,
 *         otherwise the running average.
 */
static inline uint32_t rpmFilterStep(RpmFilter *f, uint32_t sample, bool alive,
                                     uint8_t shift) {
	if (!alive) {
		rpmFilterReset(f);
		return 0;
	}
	if (!f->primed) {
		// Whole, not averaged in. @see rpmf_honest
		f->acc = sample;
		f->primed = true;
		return f->acc;
	}

	int32_t d = (int32_t)sample - (int32_t)f->acc;
	int32_t mag = d < 0 ? -d : d;

	// The shift on a negative delta is an arithmetic shift -- GCC guarantees it,
	// and C++20 made it standard -- so falling values move one step further per
	// frame than rising ones. Under a hundredth of a percent at these
	// magnitudes, and the snap below removes the residue either way.
	if (mag < ((int32_t)1 << shift)) f->acc = sample;
	else                             f->acc = (uint32_t)((int32_t)f->acc + (d >> shift));

	return f->acc;
}
