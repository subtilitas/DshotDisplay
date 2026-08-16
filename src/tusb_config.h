/**
 * @file tusb_config.h
 * @brief TinyUSB build configuration. Found by include path, not by reference.
 *
 * TinyUSB includes this by bare name from its own sources, so it has to exist,
 * has to be called exactly this, and has to be on the include path of every
 * target that links TinyUSB. Nothing in this project `#include`s it directly,
 * which makes it the kind of file that looks unused right up until it is
 * deleted. @see usb_dev.h
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Port
 * RP2350 has one full-speed device controller. Full speed, not high: the
 * ceiling on reading the card over USB is about 1 MB/s, which is what makes the
 * progress figure on the SD LOG screen worth showing.
 * @{
 */
#define CFG_TUSB_MCU          OPT_MCU_RP2040   /**< The RP2350 shares this port. */
#define CFG_TUSB_OS           OPT_OS_PICO
#define CFG_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED
/** @} */

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/**
 * @brief Alignment for USB DMA buffers.
 *
 * Not optional and not a style choice: the controller reads these buffers by
 * DMA, and an unaligned one fails as data corruption rather than as an error.
 */
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE 64

/**
 * @name Interfaces
 * One of each. Both are always present -- the drive reports "no medium" rather
 * than appearing and disappearing -- so these are constants, and
 * usb_descriptors.c derives the product ID from them.
 * @{
 */
#define CFG_TUD_CDC           1
#define CFG_TUD_MSC           1
#define CFG_TUD_HID           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_VENDOR        0
/** @} */

/**
 * @name Buffers
 * @{
 */
#define CFG_TUD_CDC_RX_BUFSIZE   64
#define CFG_TUD_CDC_TX_BUFSIZE   256

/**
 * @brief MSC transfer buffer.
 *
 * 4 KB rather than the 512 a single block needs. The host reads in multi-block
 * bursts, and a buffer that holds one block turns each burst into eight
 * round-trips on a bus whose round-trips are the slow part. Costs 4 KB of the
 * RP2350's 520, against a framebuffer that already spends 150.
 */
#define CFG_TUD_MSC_EP_BUFSIZE   4096
/** @} */

#ifdef __cplusplus
}
#endif
