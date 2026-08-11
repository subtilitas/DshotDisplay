/**
 * @file gen_blackbox_log.cpp
 * @brief Writes a synthetic blackbox log to stdout, for `blackbox_decode`.
 *
 * Not part of the test binary. `make bbcheck` builds this, pipes the output
 * into the real decoder and asserts the result is clean.
 *
 * The signal is deliberately awkward rather than smooth: a decoder will happily
 * accept a log full of zeros, so the values here exercise negative residuals,
 * multi-byte varints, a field that never changes and a field that changes every
 * frame.
 */

#include "../src/blackbox_encode.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief Frames to emit. */
#define GEN_FRAMES 2000

/** @brief Frame interval in microseconds. 500 Hz, the proposed log rate. */
#define GEN_PERIOD_US 2000

static void fileWrite(void *ctx, const uint8_t *data, size_t len) {
	fwrite(data, 1, len, (FILE *)ctx);
}

/**
 * @brief The synthetic signal.
 *
 * Kept in one place so the checker can recompute the expected values and
 * compare them against what the decoder read back.
 */
static void sample(int i, int32_t *v) {
	double t = (double)i / 100.0;

	// Stays inside 0..2000: motor[0] is declared unsigned, and a negative
	// value there does not error, it wraps to ~4.29e9 and decodes as garbage.
	v[BB_F_THROTTLE]      = 1000 + (int32_t)(900.0 * sin(t));
	v[BB_F_ERPM]          = 40000 + (int32_t)(20000.0 * sin(t));
	v[BB_F_ERPM_KISS]     = ((40000 + (int32_t)(20000.0 * sin(t))) / 100) * 100;
	v[BB_F_VBAT]          = 1650 - (int32_t)(120.0 * sin(t));  // 0.01 V
	v[BB_F_AMPERAGE]      = (int32_t)(800.0 + 700.0 * sin(t)); // 0.01 A
	v[BB_F_VBAT_EDT]      = ((1650 - (int32_t)(120.0 * sin(t))) / 25) * 25;
	v[BB_F_AMPERAGE_EDT]  = ((int32_t)(800.0 + 700.0 * sin(t)) / 100) * 100;
	v[BB_F_TEMP]          = -5 + i / 100;     // starts below zero on purpose
	v[BB_F_MAH]           = i / 4;            // monotonic, small increments
	v[BB_F_STRESS]        = (i / 37) % 256;   // stair-steps, occasional wrap
}

int main(int argc, char **argv) {
	FILE *out = stdout;
	if (argc > 1) {
		out = fopen(argv[1], "wb");
		if (!out) { perror("open"); return 1; }
	}

	BlackboxSink sink = { fileWrite, out };
	BlackboxEncoder e;
	bbBegin(&e, &sink, 32);

	for (int i = 0; i < GEN_FRAMES; i++) {
		int32_t v[BB_FIELD_COUNT] = {0};
		sample(i, v);
		bbWriteFrame(&e, (uint32_t)i * GEN_PERIOD_US, v);
	}
	bbEnd(&e);

	if (out != stdout) fclose(out);
	fprintf(stderr, "%u frames, %u bytes (%u I, %u P), %.1f bytes/frame\n",
	        (unsigned)GEN_FRAMES, (unsigned)e.bytesWritten,
	        (unsigned)e.iFrames, (unsigned)e.pFrames,
	        (double)e.bytesWritten / GEN_FRAMES);
	return 0;
}
