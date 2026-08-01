// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
#pragma once
#include <stdint.h>
extern "C" {
uint32_t save_and_disable_interrupts();
void restore_interrupts(uint32_t);
}
