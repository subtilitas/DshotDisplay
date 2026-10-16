/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file sd_host.h
 * @brief The SD-mode card protocol over an abstract four-bit bus.
 *
 * Initialisation, reads, writes and status, with every timeout. Nothing here
 * touches hardware: the bus is a table of function pointers, which sd_pio.c
 * fills for the RP2350 and the host suite fills with a simulated card. See
 * docs/design/sdio-driver.md.
 */

#ifndef SD_HOST_H
#define SD_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "sd_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name Timeouts and limits */
/** @{ */
#define SDH_INIT_HZ              400000u   /**< Identification clock, Hz (4.2.1, 4.4). */
#define SDH_MAX_HZ               25000000u /**< Default-speed ceiling, Hz (5.3.2). */
#define SDH_POWERUP_US           1000u     /**< Clock before CMD0, us (6.4.1.1). */
#define SDH_CMD_TIMEOUT_US       5000u     /**< Response to a command, us. */
#define SDH_ACMD41_TIMEOUT_US    1100000u  /**< ACMD41 until ready, us (4.2.3). */
#define SDH_ACMD41_POLL_US       5000u     /**< Between ACMD41 polls, us (4.4). */
#define SDH_READ_TIMEOUT_US      250000u   /**< Per read block without progress, us (4.6.2.1). */
#define SDH_BUSY_TIMEOUT_US      1000000u  /**< Busy on DAT0, us (4.6.2.2). */
#define SDH_MAX_BLOCKS           32u       /**< Blocks per read or write command. */
#define SDH_ATTEMPTS             3u        /**< Tries of a read or write chunk. */
/** @} */

/**
 * @brief The bus a card hangs off.
 *
 * Every function gets the ctx field first. Waits inside the bus are bounded
 * by the timeouts passed in; no function blocks without one.
 */
typedef struct {
	void *ctx;  /**< Passed to every function. */

	/**
	 * Send one command and read its response. @p resp_bits is 0, 48 or 136;
	 * @p resp receives resp_bits / 8 canonical bytes (sd_proto.h). The clock
	 * keeps running after a response until stop_clock(). Returns SDE_OK or
	 * SDE_NO_RESPONSE after @ref SDH_CMD_TIMEOUT_US.
	 */
	sd_err_t (*command)(void *ctx, uint8_t index, uint32_t arg,
	                    unsigned resp_bits, uint8_t *resp);
	/** Set the clock to at most @p hz. Returns the rate set, in Hz. */
	uint32_t (*set_clock)(void *ctx, uint32_t hz);
	/** Run the clock for @p us microseconds with CMD high, then stop it. */
	void (*run_clock)(void *ctx, uint32_t us);
	/** Stop the clock, after at least 8 clocks since the last transaction. */
	void (*stop_clock)(void *ctx);
	/**
	 * Arm the receiver for @p blocks blocks of @p block_bytes bytes into
	 * @p buf (4-byte aligned), before the read command goes out. The two CRC
	 * words of block i land in crc[2i] and crc[2i + 1], FIFO order.
	 */
	sd_err_t (*rx_arm)(void *ctx, uint8_t *buf, uint32_t block_bytes,
	                   uint32_t blocks, uint32_t *crc);
	/** Wait for every armed block; SDE_TIMEOUT after @p block_timeout_us without one. */
	sd_err_t (*rx_wait)(void *ctx, uint32_t block_timeout_us);
	/** Disarm the receiver, whatever state it is in. */
	void (*rx_stop)(void *ctx);
	/**
	 * Send one transmit stream (sdp_tx_pack()) and capture the 16 nibbles
	 * after it into @p status. SDE_TIMEOUT after @p timeout_us.
	 */
	sd_err_t (*tx_block)(void *ctx, const uint32_t *words, uint32_t nwords,
	                     uint32_t status[2], uint32_t timeout_us);
	/** Level of DAT0; low is busy. The clock runs while the caller polls. */
	bool (*dat0)(void *ctx);
	/** Free-running microsecond count. */
	uint32_t (*now_us)(void *ctx);
	/** Sleep for @p us microseconds. */
	void (*sleep_us)(void *ctx, uint32_t us);
} sdh_bus_t;

/** @brief What initialisation learned about the card. */
typedef struct {
	bool     ready;       /**< Initialised and in the transfer state. */
	bool     v2;          /**< Answered CMD8: physical layer 2.00 or later. */
	bool     block_addr;  /**< SDHC or SDXC: block addresses, not bytes. */
	uint16_t rca;         /**< Relative card address. */
	uint32_t ocr;         /**< Last OCR from ACMD41. */
	uint8_t  cid[16];     /**< Card identification register. */
	uint8_t  csd[16];     /**< Card-specific data register. */
	uint32_t sectors;     /**< Capacity in 512-byte sectors. */
	uint32_t clock_hz;    /**< Data clock in use, Hz. */
} sdh_card_t;

/**
 * @brief Initialise the card, 4.2, and raise the clock.
 * @param bus     The bus.
 * @param card    Filled in; ready is true only on success.
 * @param data_hz Requested data clock, Hz; capped at the card's TRAN_SPEED
 *                and at @ref SDH_MAX_HZ.
 * @return SDE_OK; SDE_NO_RESPONSE for no card; SDE_VOLTAGE, SDE_UNUSABLE,
 *         SDE_TIMEOUT or a response error otherwise.
 */
sd_err_t sdh_init(const sdh_bus_t *bus, sdh_card_t *card, uint32_t data_hz);

/**
 * @brief Read sectors.
 * @param bus   The bus.
 * @param card  An initialised card.
 * @param lba   First sector.
 * @param count Sectors, at least 1.
 * @param buf   count * 512 bytes, any alignment.
 * @return SDE_OK or the error of the last attempt.
 */
sd_err_t sdh_read(const sdh_bus_t *bus, sdh_card_t *card, uint32_t lba,
                  uint32_t count, uint8_t *buf);

/**
 * @brief Write sectors. Returns once the card has finished programming.
 * @param bus   The bus.
 * @param card  An initialised card.
 * @param lba   First sector.
 * @param count Sectors, at least 1.
 * @param buf   count * 512 bytes, any alignment.
 * @return SDE_OK or the error of the last attempt.
 */
sd_err_t sdh_write(const sdh_bus_t *bus, sdh_card_t *card, uint32_t lba,
                   uint32_t count, const uint8_t *buf);

/**
 * @brief CMD13: the card status.
 * @param bus    The bus.
 * @param card   An initialised card.
 * @param status Card status, 4.10.1.
 * @return SDE_OK or a response error.
 */
sd_err_t sdh_status(const sdh_bus_t *bus, sdh_card_t *card, uint32_t *status);

/**
 * @brief ACMD13: the 64-byte SD Status, 4.10.2.
 * @param bus  The bus.
 * @param card An initialised card.
 * @param out  64 bytes, bit 511 first.
 * @return SDE_OK or an error.
 */
sd_err_t sdh_sd_status(const sdh_bus_t *bus, sdh_card_t *card, uint8_t out[64]);

/**
 * @brief Is a card on the bus?
 *
 * For an initialised card, CMD13. Otherwise CMD0, then CMD8 or CMD55: any
 * response counts. An initialised card that does not answer is marked not
 * ready.
 *
 * @param bus  The bus.
 * @param card The card state.
 * @return True when something answered.
 */
bool sdh_probe(const sdh_bus_t *bus, sdh_card_t *card);

/**
 * @brief Does this error mean the card is gone or lost its state?
 * @param e An error.
 * @return True for no response and timeouts, after which only a full
 *         initialisation brings the card back.
 */
bool sdh_err_is_fatal(sd_err_t e);

#ifdef __cplusplus
}
#endif

#endif /* SD_HOST_H */
