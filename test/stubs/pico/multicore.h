// Host-test stub for pico/multicore.h.
//
// The launch is a no-op here: the host suite drives core0's logic directly and
// there is no second core to start. Nothing in the tests depends on core1
// running, which is deliberate -- escSnapshot() is the only way across, so the
// UI is testable with core1 absent.
#pragma once
void multicore_launch_core1(void (*entry)(void));
void multicore_reset_core1(void);
