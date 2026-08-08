/**
 * @file bridge_wire.h
 * @brief The one-wire half of the USB bridge, running on core1.
 *
 * @section wire_why Why this is not on core0
 *
 * The AM32 link is a bit-banged half-duplex UART: there is no peripheral and no
 * FIFO behind it, so a byte is only received if the code happens to be sitting
 * inside the receive routine when its start bit arrives. Anything else core0 is
 * doing — and a full-panel SPI DMA blit is milliseconds of it — is time the
 * receiver is simply deaf. Those bytes are lost, not delayed.
 *
 * That is survivable for settings, because the bootloader answers a read inside
 * the same poll that sent it. It is not survivable for flashing: `PROG_FLASH`
 * erases and programs a page before acknowledging, so its reply lands tens of
 * milliseconds later — reliably inside a repaint. A firmware upload would run
 * for a while and then die on whichever screen update collided with an ACK.
 *
 * So the wire moves to core1, for the same reason the DShot pump lives there:
 * core1 has nothing else to do and cannot be interrupted by the display.
 *
 * @section wire_split Which core owns what
 *
 * - **core1** owns the pin and the bit timing. It transmits queued frames and
 *   otherwise sits in the receive routine, so it is listening essentially all
 *   the time.
 * - **core0** owns USB. It never touches the wire.
 *
 * USB stays on core0 deliberately: the TinyUSB background task runs there, and
 * calling into it from core1 is not safe. The two cores meet only at the
 * buffers below.
 *
 * @section wire_handoff Why TX is a frame slot and RX is a ring
 *
 * The directions are not symmetric.
 *
 * Transmit is frame-oriented. A bootloader command must go out as one
 * contiguous burst; if core1 drained a byte ring it could outrun core0 filling
 * it and split a command mid-frame, leaving a gap on the wire the bootloader
 * need not tolerate. So core0 assembles a whole frame and hands over one slot
 * at a time.
 *
 * Receive is a byte ring, and that ring is the entire point of this file: it is
 * what absorbs core0 being busy. Core1 keeps pushing bytes in while core0 is
 * repainting, and core0 drains them whenever it next gets round to it.
 *
 * Both are single-producer/single-consumer with one owner per index, so neither
 * needs a lock — only the release/acquire barriers noted at each access.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Largest frame handed to core1 in one go.
 *
 * A firmware payload is 256 bytes plus command and CRC overhead.
 */
#define WIRE_FRAME_MAX 320

/**
 * @brief Receive ring capacity, in bytes.
 *
 * Sized by how long core0 may credibly be away. At 19200 baud the ESC produces
 * roughly two bytes per millisecond, so this covers a stall of about half a
 * second — far beyond the worst repaint, with room for the reply itself.
 */
#define WIRE_RX_RING 1024

/**
 * @defgroup wire_core0 Core0 API
 * @{
 */

/**
 * @brief Claim the pin and start the core1 pump.
 *
 * @param pin GPIO wired to the ESC signal line.
 * @warning The DShot task must already have released the pin: call
 *          escTaskSuspend() and wait for escTaskSuspended() first.
 */
void bridgeWireBegin(uint8_t pin);

/**
 * @brief Stop the pump and release the pin.
 *
 * Blocks until core1 confirms it has left the wire alone, so the pin is not
 * torn down underneath a transfer in progress.
 */
void bridgeWireEnd();

/** @brief True while core1 owns the wire. */
bool bridgeWireActive();

/**
 * @brief Queue one complete frame for transmission.
 *
 * @param data Frame bytes.
 * @param len  Length, up to @ref WIRE_FRAME_MAX.
 * @return true if accepted; false if the previous frame has not gone out yet,
 *         in which case the caller should retry rather than drop it.
 */
bool bridgeWireSend(const uint8_t *data, uint16_t len);

/**
 * @brief Drain received bytes.
 *
 * @param[out] buf    Destination.
 * @param      maxLen Capacity.
 * @return Number of bytes copied.
 */
uint16_t bridgeWireRecv(uint8_t *buf, uint16_t maxLen);

/**
 * @brief Bytes dropped because the receive ring was full.
 *
 * Should never be anything but zero. If it is not, core0 stopped draining for
 * long enough to lose ESC traffic, which is the exact failure this file exists
 * to prevent — so it is surfaced rather than silently tolerated.
 */
uint32_t bridgeWireOverruns();

/** @} */

/**
 * @defgroup wire_core1 Core1 entry point
 * @{
 */

/**
 * @brief Send any queued frame, then listen. Call from loop1() as fast as
 *        possible.
 *
 * No-op unless the bridge is active, so it is safe to call unconditionally
 * alongside escTaskPoll().
 */
void bridgeWirePoll();

/** @} */
