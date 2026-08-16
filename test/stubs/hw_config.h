// Host-test stub for the SD driver's board-configuration interface.
// Mirrors the v3 sd_card_t shape that src/sd_hw_config.c fills in.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/spi.h"
#include "hardware/gpio.h"

#ifndef count_of
#define count_of(a) (sizeof(a) / sizeof((a)[0]))
#endif

// spi0/spi1 come from hardware/spi.h, which the display driver already uses.
typedef struct spi_t {
	spi_inst_t *hw_inst;
	unsigned miso_gpio, mosi_gpio, sck_gpio;
	unsigned baud_rate;
} spi_t;

typedef struct sd_spi_if_t {
	spi_t   *spi;
	unsigned ss_gpio;
} sd_spi_if_t;

typedef enum { SD_IF_NONE, SD_IF_SPI, SD_IF_SDIO } sd_if_t;

// Only the success value is needed here; the driver's other codes are bit
// flags and nothing in this project distinguishes them. Named and valued as
// upstream does, so a rename upstream fails here rather than on a board.
typedef enum {
	SD_BLOCK_DEVICE_ERROR_NONE = 0,
} block_dev_err_t;

typedef struct sd_card_t sd_card_t;

/** Dynamically-assigned state the driver fills in during a mount attempt. */
typedef struct sd_card_state_t {
	int      card_type;
	uint32_t sectors;
} sd_card_state_t;

struct sd_card_t {
	sd_if_t      type;
	sd_spi_if_t *spi_if_p;
	bool         use_card_detect;
	unsigned     card_detect_gpio;
	unsigned     card_detected_true;
	sd_card_state_t state;
	// The block API the USB card reader sits on. Signatures copied from the
	// pinned library, sector numbers included: they are uint32_t there, not the
	// uint64_t an LBA is elsewhere, and passing the wrong one is the kind of
	// mistake that builds on a host and truncates on a big card.
	// @see usb_msc.cpp
	block_dev_err_t (*write_blocks)(sd_card_t *sd_card_p, const uint8_t *buffer,
	                                uint32_t ulSectorNumber, uint32_t blockCnt);
	block_dev_err_t (*read_blocks)(sd_card_t *sd_card_p, uint8_t *buffer,
	                               uint32_t ulSectorNumber, uint32_t ulSectorCount);
	uint32_t (*get_num_sectors)(sd_card_t *sd_card_p);
};

size_t      spi_get_num(void);
spi_t      *spi_get_by_num(size_t num);
size_t      sd_get_num(void);
sd_card_t  *sd_get_by_num(size_t num);
