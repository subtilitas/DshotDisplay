// Host-test stub for the SDK timebase.
//
// Time is virtual: these are backed by a counter in fakes.cpp that only moves
// when a test moves it. Real elapsed time would make the UI tests
// non-deterministic, which is the whole reason they are not wall-clock based.
#pragma once
#include <stdint.h>
uint64_t time_us_64();
uint32_t time_us_32();
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);
