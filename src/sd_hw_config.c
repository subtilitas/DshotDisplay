/**
 * @file sd_hw_config.c
 * @brief Board wiring for the FatFs SD driver.
 *
 * carlk3's no-OS-FatFS-SD library looks these two arrays up by name at link
 * time — there is no registration call. The pin numbers come from
 * @ref board_pins.h so there is exactly one place that knows the wiring.
 *
 * C rather than C++ because the library declares these with C linkage.
 */

#include "hw_config.h"

#include "board_pins.h"
#include "config.h"

/**
 * @brief The SPI instance the card hangs off.
 *
 * SPI1. The display is on SPI0, so a display flush and a log write never
 * contend for the same peripheral — only for core0's time.
 */
static spi_t spis[] = {
	{
		.hw_inst   = spi1,
		.miso_gpio = PIN_SD_MISO,
		.mosi_gpio = PIN_SD_MOSI,
		.sck_gpio  = PIN_SD_SCK,
		.baud_rate = SD_LOG_SPI_MHZ * 1000 * 1000,
	}
};

/**
 * @brief The SPI-attached card interface.
 *
 * Chip select lives here rather than in @ref spis because several cards can
 * share one SPI instance with different selects. This board has one slot.
 */
static sd_spi_if_t spiIfs[] = {
	{
		.spi     = &spis[0],
		.ss_gpio = PIN_SD_CS,
	}
};

/**
 * @brief The one card slot.
 *
 * No card-detect pin is brought out on this board, so `use_card_detect` is
 * false: the only way to know whether a card is fitted is to try to talk to it.
 * That is why sdLogBegin() treats a mount failure as "no card" rather than an
 * error — on a bench, running without a card is the normal case.
 */
static sd_card_t sd_cards[] = {
	{
		.type            = SD_IF_SPI,
		.spi_if_p        = &spiIfs[0],
		.use_card_detect = false,
	}
};

/** @brief Number of SPI instances. Called by the library. @return Count. */
size_t spi_get_num() { return count_of(spis); }

/**
 * @brief Look up an SPI instance.
 * @param num Index below spi_get_num().
 * @return    The instance, or NULL if out of range.
 */
spi_t *spi_get_by_num(size_t num) {
	return num < spi_get_num() ? &spis[num] : NULL;
}

/** @brief Number of card slots. Called by the library. @return Count. */
size_t sd_get_num() { return count_of(sd_cards); }

/**
 * @brief Look up a card slot.
 * @param num Index below sd_get_num().
 * @return    The slot, or NULL if out of range.
 */
sd_card_t *sd_get_by_num(size_t num) {
	return num < sd_get_num() ? &sd_cards[num] : NULL;
}
