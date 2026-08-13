#include "esc_merge.h"
#include "esc_task.h"

/**
 * @file esc_merge.cpp
 * @brief Per-field preference between KISS and EDT. Pure; host-testable.
 */

bool escFieldFresh(uint32_t stampMs, uint32_t nowMs, uint32_t staleMs) {
	// Unsigned wraparound is fine here: millis() rolls over every 49 days and
	// the subtraction stays correct across the wrap as long as it is done in
	// unsigned arithmetic and never widened to a signed type first.
	//
	// The zero check is not redundant with it. A field that never arrived has
	// stamp 0, and at boot nowMs is also near 0, so the subtraction alone would
	// call it fresh and put a fabricated reading on screen for the first
	// second after power-on.
	return stampMs != 0 && (uint32_t)(nowMs - stampMs) < staleMs;
}

void escMerge(const EscTelemetry *t, uint32_t nowMs, uint32_t staleMs,
              uint32_t edtStaleMs, EscReading *out) {
	bool fresh = t->haveKiss && (uint32_t)(nowMs - t->kissLastMs) < staleMs;

	out->kissFresh = fresh;

	// EDT expiry is per field, and separate from the KISS decision: the two
	// sources fail independently, and an ESC can stop answering on one wire
	// while the other keeps talking.
	bool eVolts  = escFieldFresh(t->edtVoltsMs,  nowMs, edtStaleMs);
	bool eAmps   = escFieldFresh(t->edtAmpsMs,   nowMs, edtStaleMs);
	bool eTemp   = escFieldFresh(t->edtTempMs,   nowMs, edtStaleMs);
	bool eStress = escFieldFresh(t->edtStressMs, nowMs, edtStaleMs);
	bool eStatus = escFieldFresh(t->edtStatusMs, nowMs, edtStaleMs);

	out->edtFresh = eVolts || eAmps || eTemp || eStress || eStatus;

	if (fresh) {
		out->volts     = t->kissVolts;
		out->voltsFrom = EscSource::Kiss;
		out->amps      = t->kissAmps;
		out->ampsFrom  = EscSource::Kiss;
		out->tempC     = t->kissTempC;
		out->tempFrom  = EscSource::Kiss;
	} else {
		out->volts     = eVolts ? t->volts : 0.0f;
		out->voltsFrom = eVolts ? EscSource::Edt : EscSource::None;
		out->amps      = eAmps ? t->amps : 0.0f;
		out->ampsFrom  = eAmps ? EscSource::Edt : EscSource::None;
		out->tempC     = eTemp ? t->tempC : 0;
		out->tempFrom  = eTemp ? EscSource::Edt : EscSource::None;
	}

	// Stress and status are EDT-only, so they expire on their own timestamps
	// whatever KISS is doing.
	out->stress     = eStress ? t->stress : 0;
	out->stressFrom = eStress ? EscSource::Edt : EscSource::None;

	out->alert      = eStatus && t->alert;
	out->warning    = eStatus && t->warning;
	out->error      = eStatus && t->error;
	out->maxStress  = eStatus ? t->maxStress : 0;
	out->statusFrom = eStatus ? EscSource::Edt : EscSource::None;

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
