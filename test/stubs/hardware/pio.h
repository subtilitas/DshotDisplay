// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
#pragma once
#include <stdint.h>
typedef struct pio_hw *PIO;
extern PIO pio0;
extern PIO pio1;
#define NUM_PIOS 3
