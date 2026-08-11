#include "esc_merge.h"
#include "esc_task.h"

/**
 * @file esc_merge.cpp
 * @brief Per-field preference between KISS and EDT. Pure; host-testable.
 */

void escMerge(const EscTelemetry *t, uint32_t nowMs, uint32_t staleMs,
              EscReading *out) {
	// Unsigned wraparound is fine here: millis() rolls over every 49 days and
	// the subtraction stays correct across the wrap as long as it is done in
	// unsigned arithmetic and never widened to a signed type first.
	bool fresh = t->haveKiss && (uint32_t)(nowMs - t->kissLastMs) < staleMs;

	out->kissFresh = fresh;

	if (fresh) {
		out->volts     = t->kissVolts;
		out->voltsFrom = EscSource::Kiss;
		out->amps      = t->kissAmps;
		out->ampsFrom  = EscSource::Kiss;
		out->tempC     = t->kissTempC;
		out->tempFrom  = EscSource::Kiss;
	} else {
		out->volts     = t->haveVolts ? t->volts : 0.0f;
		out->voltsFrom = t->haveVolts ? EscSource::Edt : EscSource::None;
		out->amps      = t->haveAmps ? t->amps : 0.0f;
		out->ampsFrom  = t->haveAmps ? EscSource::Edt : EscSource::None;
		out->tempC     = t->haveTemp ? t->tempC : 0;
		out->tempFrom  = t->haveTemp ? EscSource::Edt : EscSource::None;
	}

	// Consumption has no EDT equivalent, so it goes stale rather than falling
	// back. Showing the last known mAh next to live voltage would suggest the
	// motor had stopped drawing, which is exactly backwards.
	out->mah     = fresh ? t->kissMah : 0;
	out->mahFrom = fresh ? EscSource::Kiss : EscSource::None;

	// RPM is not merged. Both sources are carried; see the note in esc_merge.h.
	out->rpm          = t->rpm;
	out->erpm         = t->erpm;
	out->kissErpm     = fresh ? t->kissErpm : 0;
	out->haveKissErpm = fresh;
}

const char *escSourceLabel(EscSource s) {
	switch (s) {
		case EscSource::Kiss: return "KISS";
		case EscSource::Edt:  return "EDT";
		case EscSource::None: break;
	}
	return "--";
}
