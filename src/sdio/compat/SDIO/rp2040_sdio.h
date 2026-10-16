/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file rp2040_sdio.h
 * @brief The state type the SD library's sd_card.h expects for an SDIO card.
 *
 * The library's public header includes `SDIO/rp2040_sdio.h` for one type,
 * `sd_sdio_if_state_t`, the dynamic state inside its `sd_sdio_if_t`. This
 * file has that path and defines that type for the driver in src/sdio/; the
 * build compiles nothing from the library's own SDIO directory. See
 * docs/design/sdio-driver.md.
 */

#ifndef SDIO_COMPAT_RP2040_SDIO_H
#define SDIO_COMPAT_RP2040_SDIO_H

#include "sd_host.h"
#include "sd_pio.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * sd_card.h includes this header before it defines sd_card_t, and the SPI
 * driver's header that it includes next declares functions taking one. The
 * declaration here is what lets that header compile; sd_card.h repeats it,
 * which C11 and C++ both allow.
 */
typedef struct sd_card_t sd_card_t;

/** @brief Dynamic state of one SDIO card interface. Zero before first use. */
typedef struct sd_sdio_if_state_t {
	sdpio_t    bus;   /**< PIO and DMA resources and pins. */
	sdh_card_t card;  /**< What initialisation learned about the card. */
} sd_sdio_if_state_t;

#ifdef __cplusplus
}
#endif

#endif /* SDIO_COMPAT_RP2040_SDIO_H */
