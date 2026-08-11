// Host-test stub. Mirrors just enough of greiman/SdFat's signatures for
// sd_log.cpp to typecheck against; nothing here does any I/O.
//
// Not an emulation. The point is that `make syntax` catches a wrong argument
// order or a renamed method without needing a card, an SPI bus, or the real
// library vendored into the repo.
#pragma once

#include <stdint.h>
#include <stddef.h>

// Open-mode flags. Values are irrelevant here, only that the names resolve.
#define O_WRONLY 0x01
#define O_CREAT  0x02
#define O_TRUNC  0x04
#define O_RDONLY 0x00

// SdSpiConfig options.
#define SHARED_SPI    0
#define DEDICATED_SPI 1

/** Clock helper: SdFat spells its SPI speed in MHz through this macro. */
#define SD_SCK_MHZ(x) ((uint32_t)(x) * 1000000UL)

class SPIClassStub;

struct SdSpiConfig {
	SdSpiConfig(uint8_t cs, uint8_t opt, uint32_t maxSck, void *port);
};

class FsFile {
public:
	bool   open(const char *path, int flags);
	bool   preAllocate(uint64_t bytes);
	bool   truncate();
	bool   sync();
	void   close();
	size_t write(const void *buf, size_t len);
	explicit operator bool() const;
};

class SdFs {
public:
	bool begin(const SdSpiConfig &cfg);
	bool exists(const char *path);
};
