/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file sd_card_glue.c
 * @brief The SD library's sd_card_t callbacks for an SDIO card.
 *
 * The library's sd_init_driver() calls sd_sdio_ctor() for every card whose
 * type is SD_IF_SDIO; FatFs then reaches the card through the callbacks set
 * here. Each one takes the card's lock, runs sd_host.c over the bus from
 * sd_pio.c, and keeps the library's view of the card (m_Status, card_type,
 * CID, CSD, sectors) in step. A card that stops answering gets STA_NOINIT,
 * so the next mount initialises it from scratch.
 *
 * Error mapping onto block_dev_err_t, which the library's glue.c turns into
 * FatFs results:
 *
 * | sd_err_t | block_dev_err_t |
 * |---|---|
 * | SDE_NO_RESPONSE, SDE_TIMEOUT | NO_RESPONSE |
 * | SDE_CRC, SDE_FRAME | CRC |
 * | SDE_WRITE | WRITE |
 * | SDE_PROTECTED | WRITE_PROTECTED |
 * | SDE_RANGE, SDE_PARAM | PARAMETER |
 * | SDE_NOT_READY | NO_INIT |
 * | everything else | UNUSABLE |
 */

#include <string.h>

#include "hardware/gpio.h"
#include "pico/mutex.h"

#include "sd_card.h"
#include "SDIO/SdioCard.h"

#include "sd_host.h"
#include "sd_pio.h"

/** @brief The library's error code for one of ours. */
static block_dev_err_t mapErr(sd_err_t e) {
	switch (e) {
		case SDE_OK:          return SD_BLOCK_DEVICE_ERROR_NONE;
		case SDE_NO_RESPONSE:
		case SDE_TIMEOUT:     return SD_BLOCK_DEVICE_ERROR_NO_RESPONSE;
		case SDE_CRC:
		case SDE_FRAME:       return SD_BLOCK_DEVICE_ERROR_CRC;
		case SDE_WRITE:       return SD_BLOCK_DEVICE_ERROR_WRITE;
		case SDE_PROTECTED:   return SD_BLOCK_DEVICE_ERROR_WRITE_PROTECTED;
		case SDE_RANGE:
		case SDE_PARAM:       return SD_BLOCK_DEVICE_ERROR_PARAMETER;
		case SDE_NOT_READY:   return SD_BLOCK_DEVICE_ERROR_NO_INIT;
		default:              return SD_BLOCK_DEVICE_ERROR_UNUSABLE;
	}
}

/** @brief The bus table of a card. */
static sdh_bus_t busOf(sd_card_t *c) {
	sdh_bus_t b;
	sdpio_bus(&c->sdio_if_p->state.bus, &b);
	return b;
}

/** @brief Claim the PIO and DMA resources on first use. */
static bool claim(sd_card_t *c) {
	sd_sdio_if_t *i = c->sdio_if_p;
	if (i->state.bus.claimed) return true;
	if (sdpio_claim(&i->state.bus, i->SDIO_PIO, i->CLK_gpio, i->CMD_gpio, i->D0_gpio) != SDE_OK)
		return false;
	if (i->set_drive_strength) {
		gpio_set_drive_strength(i->CLK_gpio, i->CLK_gpio_drive_strength);
		gpio_set_drive_strength(i->CMD_gpio, i->CMD_gpio_drive_strength);
		gpio_set_drive_strength(i->D0_gpio, i->D0_gpio_drive_strength);
		gpio_set_drive_strength(i->D1_gpio, i->D1_gpio_drive_strength);
		gpio_set_drive_strength(i->D2_gpio, i->D2_gpio_drive_strength);
		gpio_set_drive_strength(i->D3_gpio, i->D3_gpio_drive_strength);
	}
	return true;
}

/** @brief Mirror the driver's ready flag into the library's status. */
static void syncStatus(sd_card_t *c) {
	if (!c->sdio_if_p->state.card.ready) c->state.m_Status |= STA_NOINIT;
}

static DSTATUS sdioInit(sd_card_t *c) {
	sd_lock(c);
	if (c->state.m_Status & STA_NOINIT) {
		sdh_card_t *k = &c->sdio_if_p->state.card;
		c->state.card_type = SDCARD_NONE;
		c->state.sectors = 0;
		uint32_t hz = c->sdio_if_p->baud_rate ? c->sdio_if_p->baud_rate : SDH_MAX_HZ;
		if (claim(c)) {
			sdh_bus_t b = busOf(c);
			if (sdh_init(&b, k, hz) == SDE_OK) {
				memcpy(c->state.CID, k->cid, sizeof c->state.CID);
				memcpy(c->state.CSD, k->csd, sizeof c->state.CSD);
				c->state.sectors = k->sectors;
				c->state.card_type = k->block_addr ? SDCARD_V2HC : k->v2 ? SDCARD_V2 : SDCARD_V1;
				c->state.m_Status &= ~STA_NOINIT;
			}
		}
	}
	DSTATUS s = c->state.m_Status;
	sd_unlock(c);
	return s;
}

static void sdioDeinit(sd_card_t *c) {
	sd_lock(c);
	c->state.m_Status |= STA_NOINIT;
	c->state.card_type = SDCARD_NONE;
	c->state.sectors = 0;
	c->sdio_if_p->state.card.ready = false;
	sdpio_release(&c->sdio_if_p->state.bus);
	sd_unlock(c);
}

static block_dev_err_t sdioRead(sd_card_t *c, uint8_t *buf, uint32_t lba, uint32_t n) {
	sd_lock(c);
	block_dev_err_t r = SD_BLOCK_DEVICE_ERROR_NO_INIT;
	if (!(c->state.m_Status & STA_NOINIT)) {
		sdh_bus_t b = busOf(c);
		r = mapErr(sdh_read(&b, &c->sdio_if_p->state.card, lba, n, buf));
		syncStatus(c);
	}
	sd_unlock(c);
	return r;
}

static block_dev_err_t sdioWrite(sd_card_t *c, const uint8_t *buf, uint32_t lba, uint32_t n) {
	sd_lock(c);
	block_dev_err_t r = SD_BLOCK_DEVICE_ERROR_NO_INIT;
	if (!(c->state.m_Status & STA_NOINIT)) {
		sdh_bus_t b = busOf(c);
		r = mapErr(sdh_write(&b, &c->sdio_if_p->state.card, lba, n, buf));
		syncStatus(c);
	}
	sd_unlock(c);
	return r;
}

/** @brief Nothing is ever left in flight: every write returns after programming. */
static block_dev_err_t sdioSync(sd_card_t *c) {
	sd_lock(c);
	block_dev_err_t r = (c->state.m_Status & STA_NOINIT) ? SD_BLOCK_DEVICE_ERROR_NO_INIT
	                                                     : SD_BLOCK_DEVICE_ERROR_NONE;
	sd_unlock(c);
	return r;
}

/** @brief The capacity read at init. CMD9 needs the standby state, so it is not re-read. */
static uint32_t sdioSectors(sd_card_t *c) {
	return (c->state.m_Status & STA_NOINIT) ? 0u : c->state.sectors;
}

static bool sdioTestCom(sd_card_t *c) {
	/* Allowed before sd_init_driver(), so the lock may not exist yet. */
	if (!mutex_is_initialized(&c->state.mutex)) mutex_init(&c->state.mutex);
	sd_lock(c);
	bool seen = false;
	if (claim(c)) {
		sdh_bus_t b = busOf(c);
		seen = sdh_probe(&b, &c->sdio_if_p->state.card);
		syncStatus(c);
	}
	sd_unlock(c);
	return seen;
}

void sd_sdio_ctor(sd_card_t *c) {
	c->init = sdioInit;
	c->deinit = sdioDeinit;
	c->read_blocks = sdioRead;
	c->write_blocks = sdioWrite;
	c->sync = sdioSync;
	c->get_num_sectors = sdioSectors;
	c->sd_test_com = sdioTestCom;
}

bool rp2040_sdio_get_sd_status(sd_card_t *c, uint8_t status[64]) {
	sd_lock(c);
	sd_err_t e = SDE_NOT_READY;
	if (!(c->state.m_Status & STA_NOINIT)) {
		sdh_bus_t b = busOf(c);
		e = sdh_sd_status(&b, &c->sdio_if_p->state.card, status);
		syncStatus(c);
	}
	sd_unlock(c);
	return e == SDE_OK;
}
