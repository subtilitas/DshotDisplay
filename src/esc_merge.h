/**
 * @file esc_merge.h
 * @brief Combining the two telemetry sources into one reading.
 *
 * The tester can hear about the same ESC twice: through Extended DShot
 * Telemetry riding inside the eRPM frame, and through a KISS frame on a
 * dedicated wire. Neither is a superset of the other.
 *
 * KISS is finer for the electrical quantities — 0.01 V against EDT's 0.25 V,
 * 0.01 A against 1 A — and carries a consumption figure EDT has no room for.
 * So voltage, current, temperature and mAh prefer KISS and fall back to EDT.
 *
 * The fallback is per-field, not a global switch. An ESC can perfectly well
 * send KISS frames whose consumption field is stuck at zero while its EDT
 * temperature works, and a whole-source switch would throw away the good half.
 *
 * RPM is the exception: it is **not** merged. See @ref escMerge.
 *
 * Kept apart from esc_task.cpp, which pulls in the PIO DShot library and the
 * SDK's UART and so cannot be linked into the host suite. This file is pure.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct EscTelemetry;

/** @brief Where a merged field's value came from. */
enum class EscSource : uint8_t {
	None = 0, /**< Neither source has ever supplied this field. */
	Edt,      /**< Extended DShot Telemetry, inside the eRPM frame. */
	Kiss,     /**< KISS frame on the telemetry wire. */
};

/**
 * @brief One telemetry reading with its provenance.
 *
 * The source tags are not decoration. 12.25 V from EDT and 12.25 V from KISS
 * are different claims — the first means "somewhere in a 0.25 V bucket", the
 * second "within 0.01 V" — and a display that silently switches between them
 * is showing a number whose meaning changed without saying so.
 */
struct EscReading {
	float    volts;
	float    amps;
	int16_t  tempC;
	uint16_t mah;         /**< Cumulative since ESC power-on. KISS only. */

	EscSource voltsFrom;
	EscSource ampsFrom;
	EscSource tempFrom;
	EscSource mahFrom;

	uint32_t rpm;         /**< Mechanical RPM from bidirectional DShot. */
	uint32_t erpm;        /**< Electrical RPM from bidirectional DShot. */
	uint32_t kissErpm;    /**< Electrical RPM as KISS reported it. Not merged. */
	bool     haveKissErpm;

	bool     kissFresh;   /**< A KISS frame arrived within KISS_STALE_MS. */
};

/**
 * @brief Fold a telemetry block into a single reading.
 *
 * @param t     Snapshot from escSnapshot().
 * @param nowMs Current millis(). Passed in rather than read here so the policy
 *              stays pure and the staleness edge is testable.
 * @param staleMs How long a KISS frame stays authoritative.
 * @param[out] out Merged reading.
 *
 * @note RPM always comes from the bidirectional DShot eRPM frame, and KISS eRPM
 *       is carried alongside rather than merged in. Not because KISS is coarser
 *       — above roughly 5000 RPM on a 12-16 pole motor it is actually finer,
 *       since DShot encodes a period with a 9-bit mantissa and so degrades in
 *       absolute terms as RPM rises, while KISS is a flat 100 eRPM. The reason
 *       is rate: DShot returns eRPM every frame at 1 kHz, KISS at the request
 *       cadence. Spin-up, oscillation and desync live in the fast stream.
 *       See docs/design/kiss-telemetry.md.
 */
void escMerge(const EscTelemetry *t, uint32_t nowMs, uint32_t staleMs,
              EscReading *out);

/** @brief Short label for a source, for the UI. @return "KISS", "EDT" or "--". */
const char *escSourceLabel(EscSource s);
