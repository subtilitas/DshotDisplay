// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1
#define A0 26
#define A1 27
#define A2 28
#define A3 29
extern "C" {
uint32_t millis();
uint32_t time_us_32();
void delay(uint32_t);
void pinMode(uint32_t, uint32_t);
void digitalWrite(uint32_t, uint32_t);
void analogWrite(uint32_t, int);
void analogWriteFreq(uint32_t);
void analogWriteRange(uint32_t);
int  analogRead(uint32_t);
void analogReadResolution(int);
float cosf(float);
float sinf(float);
static inline void tight_loop_contents() {}
}
struct SerialStub {
  void begin(unsigned long);
  int printf(const char*, ...);
};
extern SerialStub Serial;
