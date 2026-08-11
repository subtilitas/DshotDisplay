#include "blackbox_encode.h"

#include <stdio.h>
#include <string.h>

/**
 * @file blackbox_encode.cpp
 * @brief Betaflight blackbox writer. Pure; host-testable against blackbox_decode.
 */

const char *const bbFieldNames[BB_FIELD_COUNT] = {
	"loopIteration",
	"time",
	"motor[0]",
	"eRPM[0]",
	"eRPMkiss[0]",
	"vbatLatest",
	"amperageLatest",
	"vbatEdt",
	"amperageEdt",
	"escTemperature[0]",
	"escConsumption",
	"escStress",
};

/**
 * @brief Predictors used for each field in an I frame.
 *
 * All zero: an I frame is the resynchronisation point, so nothing in it may
 * depend on history a joining decoder does not have.
 */
static const uint8_t BB_I_PREDICTOR[BB_FIELD_COUNT] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/** @brief Encodings for each field in an I frame. 1 = unsigned VB. */
static const uint8_t BB_I_ENCODING[BB_FIELD_COUNT] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1,
};

/**
 * @brief Predictors for each field in a P frame.
 *
 * 6 (increment) for the iteration counter, so it costs nothing at all. 2
 * (straight line) for time, because frames arrive on a fixed cadence and the
 * residual against a straight line is jitter rather than the interval itself.
 * 1 (previous) for the measured quantities.
 */
static const uint8_t BB_P_PREDICTOR[BB_FIELD_COUNT] = {
	6, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

/**
 * @brief Encodings for each field in a P frame.
 *
 * 9 (null) for the iteration counter: the increment predictor is always exact,
 * so no bytes are written at all. Everything else is signed VB, since a
 * residual against a prediction is as likely negative as positive.
 */
static const uint8_t BB_P_ENCODING[BB_FIELD_COUNT] = {
	9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

uint8_t bbWriteUnsignedVB(uint32_t v, uint8_t *out) {
	uint8_t n = 0;
	// Five 7-bit groups cover 35 bits, so a uint32 always terminates inside the
	// loop and the decoder's own 5-byte limit is never provoked.
	while (v > 127) {
		out[n++] = (uint8_t)((v & 0x7F) | 0x80);
		v >>= 7;
	}
	out[n++] = (uint8_t)v;
	return n;
}

uint32_t bbZigZag(int32_t v) {
	// Arithmetic shift of a negative value is implementation-defined before
	// C++20, so go via the unsigned domain: for v < 0 this is all-ones, for
	// v >= 0 it is zero, which is exactly the mask the mapping needs.
	uint32_t u = (uint32_t)v;
	return (u << 1) ^ (uint32_t)(v < 0 ? 0xFFFFFFFFu : 0u);
}

uint8_t bbWriteSignedVB(int32_t v, uint8_t *out) {
	return bbWriteUnsignedVB(bbZigZag(v), out);
}

/** @brief Push bytes at the sink and keep the running total. */
static void emit(BlackboxEncoder *e, const uint8_t *data, size_t len) {
	if (e->sink.write) e->sink.write(e->sink.ctx, data, len);
	e->bytesWritten += (uint32_t)len;
}

/** @brief Push a NUL-terminated string. */
static void emitStr(BlackboxEncoder *e, const char *s) {
	emit(e, (const uint8_t *)s, strlen(s));
}

/**
 * @brief Emit one `H Field <type> <what>:v,v,v` line from a byte array.
 */
static void emitFieldLine(BlackboxEncoder *e, char frame, const char *what,
                          const uint8_t *vals) {
	char buf[32];
	snprintf(buf, sizeof(buf), "H Field %c %s:", frame, what);
	emitStr(e, buf);
	for (int i = 0; i < BB_FIELD_COUNT; i++) {
		snprintf(buf, sizeof(buf), "%s%u", i ? "," : "", (unsigned)vals[i]);
		emitStr(e, buf);
	}
	emitStr(e, "\n");
}

/** @brief Emit the ASCII header. */
static void emitHeader(BlackboxEncoder *e) {
	// Must be byte-exact: the parser memmem()s for this string to find where a
	// log begins, so a single character out and the file is invisible.
	emitStr(e, "H Product:Blackbox flight data recorder by Nicholas Sherlock\n");
	emitStr(e, "H Data version:2\n");
	// Betaflight itself writes "Cleanflight" here for backwards compatibility,
	// and the parser only recognises that and "Betaflight". Not our lie to fix.
	emitStr(e, "H Firmware type:Cleanflight\n");
	emitStr(e, "H Firmware revision:Betaflight 4.5.0 (DshotDisplay)\n");
	emitStr(e, "H Craft name:DshotDisplay\n");

	char buf[48];
	snprintf(buf, sizeof(buf), "H I interval:%u\n", (unsigned)e->iInterval);
	emitStr(e, buf);
	emitStr(e, "H P interval:1/1\n");

	// vbatscale/vbatref exist so tools do not rescale values that are already
	// in real units. Current is logged in 0.01 A directly, matching KISS.
	emitStr(e, "H minthrottle:0\n");
	emitStr(e, "H maxthrottle:2000\n");
	emitStr(e, "H motorOutput:0,2000\n");
	emitStr(e, "H acc_1G:1\n");
	emitStr(e, "H vbatscale:110\n");
	emitStr(e, "H vbatref:0\n");
	emitStr(e, "H vbatcellvoltage:330,350,430\n");
	emitStr(e, "H currentMeter:0,1\n");
	emitStr(e, "H gyro.scale:0x3f800000\n");

	emitStr(e, "H Field I name:");
	for (int i = 0; i < BB_FIELD_COUNT; i++) {
		if (i) emitStr(e, ",");
		emitStr(e, bbFieldNames[i]);
	}
	emitStr(e, "\n");

	// Everything logged here is a non-negative measurement except temperature,
	// which can legitimately go below zero on a cold bench.
	static const uint8_t sign[BB_FIELD_COUNT] = {0,0,0,0,0,0,0,0,0,1,0,0};
	emitFieldLine(e, 'I', "signed",    sign);
	emitFieldLine(e, 'I', "predictor", BB_I_PREDICTOR);
	emitFieldLine(e, 'I', "encoding",  BB_I_ENCODING);
	// P inherits name and signedness from I, but predictor and encoding must be
	// stated for both.
	emitFieldLine(e, 'P', "predictor", BB_P_PREDICTOR);
	emitFieldLine(e, 'P', "encoding",  BB_P_ENCODING);
}

void bbBegin(BlackboxEncoder *e, const BlackboxSink *sink, uint32_t iInterval) {
	memset(e, 0, sizeof(*e));
	e->sink = *sink;
	e->iInterval = iInterval < 1 ? 1 : iInterval;
	emitHeader(e);
}

/** @brief Emit an I frame: every field absolute. */
static void writeIFrame(BlackboxEncoder *e, const int32_t *v) {
	uint8_t buf[1 + BB_FIELD_COUNT * 5];
	size_t n = 0;
	buf[n++] = 'I';

	for (int i = 0; i < BB_FIELD_COUNT; i++) {
		n += (BB_I_ENCODING[i] == 0) ? bbWriteSignedVB(v[i], &buf[n])
		                             : bbWriteUnsignedVB((uint32_t)v[i], &buf[n]);
	}
	emit(e, buf, n);
	e->iFrames++;
}

/** @brief Emit a P frame: every field as a residual against its predictor. */
static void writePFrame(BlackboxEncoder *e, const int32_t *v) {
	uint8_t buf[1 + BB_FIELD_COUNT * 5];
	size_t n = 0;
	buf[n++] = 'P';

	for (int i = 0; i < BB_FIELD_COUNT; i++) {
		if (BB_P_ENCODING[i] == 9) continue;   // null: predictor is exact

		int32_t predicted;
		switch (BB_P_PREDICTOR[i]) {
			case 2:
				// Straight line through the last two samples. Needs two frames
				// of history; with only one, fall back to "same as previous" so
				// the very first P frame after an I frame is still correct.
				predicted = e->havePrev2 ? (2 * e->prev[i] - e->prev2[i])
				                         : e->prev[i];
				break;
			case 1:
			default:
				predicted = e->prev[i];
				break;
		}
		n += bbWriteSignedVB(v[i] - predicted, &buf[n]);
	}
	emit(e, buf, n);
	e->pFrames++;
}

void bbWriteFrame(BlackboxEncoder *e, uint32_t timeUs, const int32_t *values) {
	int32_t v[BB_FIELD_COUNT];
	memcpy(v, values, sizeof(v));
	v[BB_F_ITERATION] = (int32_t)e->iteration;
	v[BB_F_TIME]      = (int32_t)timeUs;

	// An I frame first, then one every iInterval frames. Without them a decoder
	// joining mid-file has no absolute values to start predicting from.
	bool wantI = !e->havePrev || e->sinceIFrame >= e->iInterval - 1;

	if (wantI) {
		writeIFrame(e, v);
		e->sinceIFrame = 0;
	} else {
		writePFrame(e, v);
		e->sinceIFrame++;
	}

	if (wantI) {
		// After an I frame both history slots become the I frame itself. The
		// decoder does exactly this -- "we can't look further into the past
		// than the I-frame" -- and if the encoder instead kept the genuine
		// frame-before-last, every straight-line prediction after an I frame
		// would be computed against different history at each end. The symptom
		// is a log that parses cleanly with a time column that stalls at each
		// I frame, which is not obviously a predictor bug at all.
		memcpy(e->prev2, v, sizeof(e->prev2));
	} else {
		memcpy(e->prev2, e->prev, sizeof(e->prev2));
	}
	memcpy(e->prev, v, sizeof(e->prev));
	e->havePrev2 = true;
	e->havePrev = true;
	e->iteration++;
}

void bbWriteEvent(BlackboxEncoder *e, uint8_t id) {
	uint8_t buf[2] = { 'E', id };
	emit(e, buf, sizeof(buf));
}

void bbEnd(BlackboxEncoder *e) {
	bbWriteEvent(e, BB_EVENT_LOG_END);
	// The end-of-log event carries a fixed 11-byte sentinel, trailing NUL
	// included. The decoder compares it verbatim and, if it does not match,
	// concludes it was looking at bytes that merely resembled an event header
	// and carries on reading -- so omitting this does not truncate the log, it
	// just means the log never officially ends.
	static const char kEndMessage[11] = {
		'E','n','d',' ','o','f',' ','l','o','g','\0'
	};
	emit(e, (const uint8_t *)kEndMessage, sizeof(kEndMessage));
}
