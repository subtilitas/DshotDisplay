// Host-test stub for arduino-pico's SPI objects.
//
// Only the pin-assignment calls sd_log.cpp makes. The display driver talks to
// the Pico SDK's spi_* functions directly rather than going through this, so
// there is deliberately no transfer API here.
#pragma once

#include <stdint.h>

class SPIClassStub {
public:
	void setRX(unsigned pin);
	void setTX(unsigned pin);
	void setSCK(unsigned pin);
	void setCS(unsigned pin);
	void begin();
	void end();
};

extern SPIClassStub SPI;  /**< SPI0. */
extern SPIClassStub SPI1; /**< SPI1 — the microSD slot. */
