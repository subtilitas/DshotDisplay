// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
#pragma once
#include <stdint.h>
struct TwoWire {
  void begin();
  void setSDA(uint8_t);
  void setSCL(uint8_t);
  void setClock(uint32_t);
  void beginTransmission(uint8_t);
  size_t write(uint8_t);
  uint8_t endTransmission();
  uint8_t endTransmission(bool);
  uint8_t requestFrom(uint8_t, uint8_t);
  int read();
};
// Wire is i2c0, Wire1 is i2c1. Which one a board uses is BOARD_I2C.
extern TwoWire Wire;
extern TwoWire Wire1;
