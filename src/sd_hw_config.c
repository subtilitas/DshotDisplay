/**
 * @file sd_hw_config.c
 * @brief Board wiring for the FatFs SD driver, chosen at run time.
 *
 * carlk3's no-OS-FatFS-SD library looks these arrays up by name at link time --
 * there is no registration call -- so this file has to exist and has to be
 * named exactly this. What it no longer does is decide, at compile time, which
 * of the two interfaces the board uses.
 *
 * The two boards do not merely differ in pins; they use different *interfaces*:
 *
 * - **RP2350-Touch-LCD-2** wires four SD lines onto their SPI1 functions, so it
 *   uses hardware SPI, one bit wide.
 * - **RP2350-Touch-LCD-2.8** brings out all six SDIO lines and cannot use
 *   hardware SPI at all -- its SD_SCK is GP19, which is SPI0 **TX** in the pin
 *   mux, not SCK. It uses the PIO SDIO driver, four bits wide, and is faster
 *   for it.
 *
 * Both are configured here and `sd_card_t.type` selects between them, which the
 * library already reads at run time. That field is why a unified image is
 * possible at all: the piece that looks hardest is the one the library had
 * already made easy. sdHwConfigApply() fills both structs from @ref g_board
 * before the card is mounted.
 *
 * C rather than C++ because the library declares these with C linkage.
 */

#include "hw_config.h"

#include "board_desc.h"
#include "config.h"

/** @brief The SPI instance an SPI-attached card hangs off. */
static spi_t spis[] = {
	{
		.hw_inst   = spi1,
		.miso_gpio = 0,   /* filled by sdHwConfigApply() */
		.mosi_gpio = 0,
		.sck_gpio  = 0,
		.baud_rate = SD_LOG_SPI_MHZ * 1000 * 1000,
	}
};

/**
 * @brief The SPI-attached card interface.
 *
 * Chip select lives here rather than in @ref spis because several cards could
 * share one SPI instance with different selects. Both boards have one slot.
 */
static sd_spi_if_t spiIfs[] = {
	{
		.spi     = &spis[0],
		.ss_gpio = 0,     /* filled by sdHwConfigApply() */
	}
};

/**
 * @brief The SDIO-attached card interface.
 *
 * PIO: the DShot driver claims a state machine on pio0, so the SD driver is put
 * on pio1 to keep them out of each other's way. Both would otherwise compete
 * for free state machines on the same block, and whichever initialised second
 * would fail depending on timing.
 *
 * The pin constraints the PIO program encodes -- D1..D3 directly following D0,
 * and CLK at `(D0 + 30) % 32` -- are asserted in board_desc_lcd2_8.cpp, where
 * they are still compile-time constants. They cannot be asserted here any more:
 * these values arrive from a descriptor.
 */
static sd_sdio_if_t sdioIf = {
	.CLK_gpio = 0,        /* filled by sdHwConfigApply() */
	.CMD_gpio = 0,
	.D0_gpio  = 0,
	.D1_gpio  = 0,
	.D2_gpio  = 0,
	.D3_gpio  = 0,
	.SDIO_PIO = pio1,
	.DMA_IRQ_num = DMA_IRQ_1,
	.baud_rate = SD_LOG_SDIO_HZ,
};

/**
 * @brief The one card slot.
 *
 * Neither board brings a card-detect line out, so `use_card_detect` is false:
 * the only way to know whether a card is fitted is to try to talk to it. That
 * is why sdLogBegin() treats a mount failure as "no card" rather than an
 * error -- on a bench, running without a card is the normal case.
 */
static sd_card_t sd_cards[] = {
	{
		.type            = SD_IF_SPI,   /* set by sdHwConfigApply() */
		.spi_if_p        = &spiIfs[0],
		.use_card_detect = false,
	}
};

void sdHwConfigApply(void) {
	if (g_board->sdIface == SD_IFACE_DESC_SDIO) {
		sdioIf.CLK_gpio = g_board->sdSck;
		sdioIf.CMD_gpio = g_board->sdCmd;
		sdioIf.D0_gpio  = g_board->sdD0;
		sdioIf.D1_gpio  = g_board->sdD1;
		sdioIf.D2_gpio  = g_board->sdD2;
		sdioIf.D3_gpio  = g_board->sdD3;
		sd_cards[0].type      = SD_IF_SDIO;
		sd_cards[0].sdio_if_p = &sdioIf;
	} else {
		spis[0].sck_gpio  = g_board->sdSck;
		spis[0].mosi_gpio = g_board->sdCmd;   /* CMD is MOSI in SPI mode */
		spis[0].miso_gpio = g_board->sdD0;    /* D0 is MISO in SPI mode  */
		spiIfs[0].ss_gpio = g_board->sdD3;    /* D3 is CS in SPI mode    */
		sd_cards[0].type     = SD_IF_SPI;
		sd_cards[0].spi_if_p = &spiIfs[0];
	}
}

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
