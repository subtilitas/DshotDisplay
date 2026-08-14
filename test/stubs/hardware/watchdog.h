// Host-test stub for hardware/watchdog.h.
//
// platReboot() (plat.h) is how a saved board change takes effect, and the
// tests assert both directions: saving a *new* board reboots, saving anything
// else does not. So unlike the flash stub this one is genuinely called — the
// implementation lives in fakes.cpp and counts into fakeRebootCount.
#pragma once
#include <stdint.h>

extern "C" {
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);
}
