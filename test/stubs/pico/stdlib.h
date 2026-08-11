// Host-test stub. The SDK's umbrella header; the tests only need the timing
// calls plat.h is built on.
#pragma once
#include <stdint.h>
#include "pico/time.h"
static inline void tight_loop_contents() {}

// stdio over USB CDC on the device; a no-op here. printf() itself comes from
// the host libc, so the SERIAL_TELEMETRY path still compiles and typechecks.
void stdio_init_all();
