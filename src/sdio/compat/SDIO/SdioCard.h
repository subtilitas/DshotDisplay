/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file SdioCard.h
 * @brief The two functions the SD library's sd_card.c calls for SDIO cards.
 *
 * The library's sd_card.c includes `SDIO/SdioCard.h` and calls these by
 * name. They are defined in src/sdio/sd_card_glue.c. See
 * docs/design/sdio-driver.md.
 */

#ifndef SDIO_COMPAT_SDIOCARD_H
#define SDIO_COMPAT_SDIOCARD_H

#include <stdbool.h>
#include <stdint.h>

#include "sd_card.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fill in the callbacks of an SDIO card. Touches no hardware.
 * @param sd_card_p A card whose type is SD_IF_SDIO.
 */
void sd_sdio_ctor(sd_card_t *sd_card_p);

/**
 * @brief Read the 64-byte SD Status (ACMD13, 4.10.2).
 * @param sd_card_p An initialised SDIO card.
 * @param status    64 bytes, bit 511 first.
 * @return True on success.
 */
bool rp2040_sdio_get_sd_status(sd_card_t *sd_card_p, uint8_t status[64]);

#ifdef __cplusplus
}
#endif

#endif /* SDIO_COMPAT_SDIOCARD_H */
