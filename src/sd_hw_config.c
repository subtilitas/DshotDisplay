/**
 * @file sd_hw_config.c
 * @brief Board wiring for the FatFs SD driver.
 *
 * carlk3's no-OS-FatFS-SD library looks these arrays up by name at link time —
 * there is no registration call. The pin numbers come from @ref board_pins.h so
 * there is exactly one place that knows the wiring.
 *
 * The two boards do not merely differ in pins; they use different *interfaces*:
 *
 * - **RP2350-Touch-LCD-2** wires four SD lines onto their SPI1 functions, so it
 *   uses hardware SPI, one bit wide.
 * - **RP2350-Touch-LCD-2.8** brings out all six SDIO lines and cannot use
 *   hardware SPI at all — its SD_SCK is GP19, which is SPI0 **TX** in the pin
 *   mux, not SCK. It uses the PIO SDIO driver, four bits wide, and is faster
 *   for it.
 *
 * C rather than C++ because the library declares these with C linkage.
 */

#include "hw_config.h"

#include "board_pins.h"
#include "config.h"

#if defined(SD_IFACE_SPI)

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
 * Chip select lives here rather than in @ref spis because several cards could
 * share one SPI instance with different selects. This board has one slot.
 */
static sd_spi_if_t spiIfs[] = {
	{
		.spi     = &spis[0],
		.ss_gpio = PIN_SD_CS,
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

#elif defined(SD_IFACE_SDIO)

/**
 * @brief The SDIO-attached card interface.
 *
 * The pin constraints here are not suggestions — the PIO program encodes them:
 * D1..D3 must directly follow D0, and CLK must sit at `(D0 + 30) % 32`. This
 * board satisfies both (D0=21 so D1..D3 are 22..24, and CLK=19 is (21+30)%32),
 * which is why SDIO is possible at all.
 *
 * PIO: the DShot driver claims a state machine on pio0, so the SD driver is put
 * on pio1 to keep them out of each other's way. Both would otherwise compete
 * for free state machines on the same block, and whichever initialised second
 * would fail depending on timing.
 */
static sd_sdio_if_t sdioIf = {
	.CLK_gpio = PIN_SD_SCK,
	.CMD_gpio = PIN_SD_CMD,
	.D0_gpio  = PIN_SD_D0,
	.D1_gpio  = PIN_SD_D1,
	.D2_gpio  = PIN_SD_D2,
	.D3_gpio  = PIN_SD_D3,
	.SDIO_PIO = pio1,
	.DMA_IRQ_num = DMA_IRQ_1,
	.baud_rate = SD_LOG_SDIO_HZ,
};

/* Compile-time guards for the two rules the PIO program cannot check itself.
   Getting either wrong produces a card that never answers, which looks exactly
   like a missing card. */
_Static_assert(PIN_SD_D1 == PIN_SD_D0 + 1, "SDIO D1 must be D0 + 1");
_Static_assert(PIN_SD_D2 == PIN_SD_D0 + 2, "SDIO D2 must be D0 + 2");
_Static_assert(PIN_SD_D3 == PIN_SD_D0 + 3, "SDIO D3 must be D0 + 3");
_Static_assert(PIN_SD_SCK == (PIN_SD_D0 + 30) % 32,
               "SDIO CLK must be (D0 + 30) % 32; see rp2040_sdio.pio");

#else
#error "board_pins.h defined neither SD_IFACE_SPI nor SD_IFACE_SDIO"
#endif

/**
 * @brief The one card slot.
 *
 * Neither board brings a card-detect line out, so `use_card_detect` is false:
 * the only way to know whether a card is fitted is to try to talk to it. That
 * is why sdLogBegin() treats a mount failure as "no card" rather than an
 * error — on a bench, running without a card is the normal case.
 */
static sd_card_t sd_cards[] = {
	{
#if defined(SD_IFACE_SPI)
		.type            = SD_IF_SPI,
		.spi_if_p        = &spiIfs[0],
#else
		.type            = SD_IF_SDIO,
		.sdio_if_p       = &sdioIf,
#endif
		.use_card_detect = false,
	}
};

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
