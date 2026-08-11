// Host-test stub for hardware/clocks.h.
#pragma once
#include <stdint.h>
enum clock_index { clk_sys = 0 };
uint32_t clock_get_hz(enum clock_index i);
