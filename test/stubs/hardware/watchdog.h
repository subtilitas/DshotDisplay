// Host-test stub for hardware/watchdog.h.
//
// platReboot() (plat.h) has no caller: saving used to reboot when it changed
// the stored board, and the board is detected rather than stored now. The stub
// stays because plat.h includes this header, and fakes.cpp still defines the
// symbol so the suite can assert that no save reboots.
#pragma once
#include <stdint.h>

extern "C" {
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);
}
