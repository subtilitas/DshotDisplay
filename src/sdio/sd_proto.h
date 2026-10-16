/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file sd_proto.h
 * @brief SD-mode wire formats: CRCs, command frames, responses, data blocks.
 *
 * Pure functions with no hardware and no state, so the host suite tests every
 * one of them. Section numbers refer to the Physical Layer Simplified
 * Specification v9.10. See docs/design/sdio-driver.md.
 *
 * Two layouts recur:
 *
 * - A response in canonical form is its bits in transmission order, most
 *   significant first, packed into bytes: 6 bytes for a 48-bit response, 17
 *   for the 136-bit R2. Byte 0 holds the start bit in bit 7.
 * - A FIFO word is 32 bits as the PIO (programmable input/output) state
 *   machine shifts them, first bit or nibble in the most significant
 *   position.
 */

#ifndef SD_PROTO_H
#define SD_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of a driver operation. 0 is success. */
typedef enum {
	SDE_OK = 0,         /**< Success. */
	SDE_NO_RESPONSE,    /**< No response before the timeout: no card, or gone. */
	SDE_FRAME,          /**< A response whose fixed bits are wrong. */
	SDE_CRC,            /**< A CRC mismatch on a response or a data block. */
	SDE_TIMEOUT,        /**< Data, busy or card initialisation did not finish in time. */
	SDE_VOLTAGE,        /**< The card does not accept the 3.2 V to 3.4 V window. */
	SDE_UNUSABLE,       /**< Not a usable SD memory card (bad echo, CSD version). */
	SDE_STATUS,         /**< An error bit in the card status. */
	SDE_WRITE,          /**< The card rejected a written block. */
	SDE_PROTECTED,      /**< Write protection violated. */
	SDE_RANGE,          /**< Address or length out of range, as the card reports it. */
	SDE_PARAM,          /**< Bad argument from the caller. */
	SDE_NOT_READY,      /**< The card is not initialised. */
	SDE_HW,             /**< No PIO or DMA resources for the bus. */
} sd_err_t;

/** @brief Response formats, 4.9. */
typedef enum {
	SDP_RESP_NONE = 0,  /**< No response (CMD0). */
	SDP_RESP_R1,        /**< 48 bits, card status. */
	SDP_RESP_R1B,       /**< R1, then busy on DAT0. */
	SDP_RESP_R2,        /**< 136 bits, CID or CSD. */
	SDP_RESP_R3,        /**< 48 bits, OCR, no CRC. */
	SDP_RESP_R6,        /**< 48 bits, published RCA. */
	SDP_RESP_R7,        /**< 48 bits, interface condition. */
} sdp_resp_t;

/** @brief Bytes in one data block. */
#define SDP_BLOCK_BYTES        512u
/** @brief Clocks the card is given after a response, 4.4. */
#define SDP_TRAIL_CLOCKS       8u
/** @brief Words in one transmit stream: header, 128 data, 2 CRC, tail. */
#define SDP_TX_WORDS           132u
/** @brief Nibbles the transmit program sends for one block, less one: 3 idle, start, 1024 data, 16 CRC, end. */
#define SDP_TX_NIBBLES_M1      1044u

/** @name Card status bits, 4.10.1 Table 4-42 */
/** @{ */
#define SDP_ST_OUT_OF_RANGE    (1u << 31) /**< Argument out of range. */
#define SDP_ST_ADDRESS_ERROR   (1u << 30) /**< Misaligned address. */
#define SDP_ST_BLOCK_LEN_ERROR (1u << 29) /**< Block length not allowed. */
#define SDP_ST_WP_VIOLATION    (1u << 26) /**< Write to a protected block. */
#define SDP_ST_COM_CRC_ERROR   (1u << 23) /**< CRC of the previous command failed. */
#define SDP_ST_ILLEGAL_COMMAND (1u << 22) /**< The previous command was illegal. */
#define SDP_ST_ERROR           (1u << 19) /**< General error. */
#define SDP_ST_READY_FOR_DATA  (1u << 8)  /**< Buffer empty. */
#define SDP_ST_APP_CMD         (1u << 5)  /**< Next command is an ACMD. */
/**
 * @brief Error bits that fail the command whose response carries them.
 *
 * 31..26, 24, 21..19, 16 and 3. COM_CRC_ERROR and ILLEGAL_COMMAND are left
 * out: their clear condition is B, they describe the previous command, and
 * that command got no response at all.
 */
#define SDP_ST_ERROR_MASK      0xFD390008u
/** @brief CURRENT_STATE value of the transfer state. */
#define SDP_STATE_TRAN         4u
/** @} */

/** @name OCR bits, 5.1 Table 5-1 */
/** @{ */
#define SDP_OCR_BUSY           (1u << 31) /**< Power-up done (set when ready). */
#define SDP_OCR_CCS            (1u << 30) /**< Card capacity status; HCS in ACMD41. */
#define SDP_OCR_WINDOW         0x00300000u /**< 3.2 V to 3.4 V. */
/** @} */

/** @brief CMD8 argument: 2.7 V to 3.6 V, check pattern 0xAA, 4.3.13. */
#define SDP_CMD8_ARG           0x000001AAu

/**
 * @brief CRC7 over bytes, most significant bit first, 4.5.
 * @param data Bytes.
 * @param len  Byte count.
 * @return The 7-bit remainder, in bits 6..0.
 */
uint8_t sdp_crc7(const uint8_t *data, size_t len);

/**
 * @brief CRC16 of one data line over bytes, 4.5. Initial value 0.
 * @param data Bytes in transmission order.
 * @param len  Byte count.
 * @return The 16-bit remainder.
 */
uint16_t sdp_crc16(const uint8_t *data, size_t len);

/**
 * @brief The four per-line CRC16s of a four-bit block, interleaved.
 *
 * The result is the 16 CRC nibbles in transmission order, first nibble in
 * bits 63..60: exactly the 64 bits that follow the data on the wire.
 *
 * @param data Block bytes.
 * @param len  Byte count.
 * @return The interleaved remainder.
 */
uint64_t sdp_crc16x4(const uint8_t *data, size_t len);

/**
 * @brief One line's CRC16 out of an interleaved remainder.
 * @param crc  From sdp_crc16x4().
 * @param line 0 for DAT0 to 3 for DAT3.
 * @return That line's CRC16.
 */
uint16_t sdp_crc16x4_line(uint64_t crc, unsigned line);

/**
 * @brief A 48-bit command, 4.7.2: start 0, transmission 1, index, argument,
 *        CRC7, end 1.
 * @param index Command index, 0..63.
 * @param arg   Argument.
 * @return The frame in bits 47..0.
 */
uint64_t sdp_cmd_frame(uint8_t index, uint32_t arg);

/**
 * @brief The two FIFO words the command program takes.
 *
 * Word 0: bit count less one (47) in bits 31..24, frame bits 47..24 below.
 * Word 1: frame bits 23..0 in bits 31..8, then the count of bits to read
 * after the response start bit: response length less one plus
 * @ref SDP_TRAIL_CLOCKS.
 *
 * @param index     Command index.
 * @param arg       Argument.
 * @param resp_bits Response length, 48 or 136; 0 for none.
 * @param w         The two words.
 */
void sdp_cmd_words(uint8_t index, uint32_t arg, unsigned resp_bits, uint32_t w[2]);

/**
 * @brief Response length in bits for a response type.
 * @param kind Response type.
 * @return 0, 48 or 136.
 */
unsigned sdp_resp_bits(sdp_resp_t kind);

/**
 * @brief FIFO words the command program pushes for a response.
 * @param resp_bits 48 or 136.
 * @return 2 or 5.
 */
unsigned sdp_resp_words(unsigned resp_bits);

/**
 * @brief Captured FIFO words to a canonical response.
 *
 * The program does not capture the start bit, so the stream starts with the
 * transmission bit. Full words come first, most significant bit first; the
 * last word holds the remaining bits right-aligned.
 *
 * @param w         The words, sdp_resp_words() of them.
 * @param resp_bits 48 or 136.
 * @param out       resp_bits / 8 bytes.
 */
void sdp_resp_unpack(const uint32_t *w, unsigned resp_bits, uint8_t *out);

/**
 * @brief Check a 48-bit response, 4.9.
 *
 * Start and transmission bits 0, end bit 1. R3 carries 111111 for the index
 * and 1111111 for the CRC; every other type echoes @p index and carries a
 * valid CRC7.
 *
 * @param r     Six bytes, canonical.
 * @param kind  R1, R1b, R3, R6 or R7.
 * @param index Command index the response answers.
 * @return SDE_OK, SDE_FRAME or SDE_CRC.
 */
sd_err_t sdp_resp48_check(const uint8_t r[6], sdp_resp_t kind, uint8_t index);

/**
 * @brief The 32 bits between index and CRC of a 48-bit response.
 * @param r Six bytes, canonical.
 * @return Card status, OCR, RCA and status, or interface condition.
 */
uint32_t sdp_resp48_arg(const uint8_t r[6]);

/**
 * @brief Check an R2 response and take out its register, 4.9.3.
 *
 * The register's bit 0 is the response end bit, which must be 1. Its CRC7
 * covers bits 127..8.
 *
 * @param r   17 bytes, canonical.
 * @param reg The CID or CSD, 16 bytes, bits 127..0.
 * @return SDE_OK, SDE_FRAME or SDE_CRC.
 */
sd_err_t sdp_r2_check(const uint8_t r[17], uint8_t reg[16]);

/**
 * @brief Classify the error bits of a card status.
 * @param status Card status from an R1.
 * @param ignore Error bits to overlook, such as OUT_OF_RANGE after a read
 *               that ends on the last block (4.3.3).
 * @return SDE_OK, SDE_PROTECTED, SDE_RANGE or SDE_STATUS.
 */
sd_err_t sdp_status_error(uint32_t status, uint32_t ignore);

/**
 * @brief CURRENT_STATE of a card status.
 * @param status Card status.
 * @return 0 idle to 8 disconnect; 4 is transfer.
 */
unsigned sdp_status_state(uint32_t status);

/**
 * @brief The status bits of an R6 in card status positions, 4.9.5.
 * @param arg R6 argument field.
 * @return Bits 23, 22, 19 and 12..0 of a card status.
 */
uint32_t sdp_r6_status(uint32_t arg);

/**
 * @brief Bits @p msb down to @p lsb of a 128-bit register.
 * @param reg 16 bytes, bit 127 first.
 * @param msb Highest bit, at most 127.
 * @param lsb Lowest bit, at most @p msb, and @p msb - @p lsb below 32.
 * @return The field, right-aligned.
 */
uint32_t sdp_reg_bits(const uint8_t reg[16], unsigned msb, unsigned lsb);

/**
 * @brief Card capacity in 512-byte sectors from the CSD, 5.3.2 and 5.3.3.
 * @param csd 16 bytes.
 * @return Sectors, or 0 for a CSD version or block length this driver does
 *         not handle.
 */
uint32_t sdp_csd_sectors(const uint8_t csd[16]);

/**
 * @brief Maximum data clock from CSD TRAN_SPEED, 5.3.2 Table 5-6.
 * @param csd 16 bytes.
 * @return Hz, or 0 for a reserved code.
 */
uint32_t sdp_csd_max_hz(const uint8_t csd[16]);

/**
 * @brief Build the transmit stream of one block for the data program.
 *
 * Header (nibble count less one in bits 31..16, then nibbles F, F, F, 0),
 * 128 data words, two CRC words, tail (end bit nibble F). Bytes go in address
 * order, high nibble first, so byte n's bits 7..4 are on DAT3..DAT0 for one
 * clock and bits 3..0 for the next (3.6.1, Figure 3-9).
 *
 * @param data 512 bytes, any alignment.
 * @param out  @ref SDP_TX_WORDS words.
 */
void sdp_tx_pack(const uint8_t *data, uint32_t out[SDP_TX_WORDS]);

/**
 * @brief Compare a received block with its two CRC words.
 * @param data Block bytes in address order.
 * @param len  Byte count, 512 or 64.
 * @param crc  The two CRC words in FIFO order.
 * @return True when all four lines match.
 */
bool sdp_rx_check(const uint8_t *data, size_t len, const uint32_t crc[2]);

/**
 * @brief Find the write CRC status token in the 16 nibbles after a block.
 *
 * The token is 0, three status bits, 1 on DAT0 (Figure 5-8). DAT0 is bit 0 of
 * each nibble.
 *
 * @param w The two words the data program captured.
 * @return The three status bits (2, binary 010, is accepted), or -1 when no
 *         complete token is in the window.
 */
int sdp_crc_status(const uint32_t w[2]);

#ifdef __cplusplus
}
#endif

#endif /* SD_PROTO_H */
