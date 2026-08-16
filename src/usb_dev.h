/**
 * @file usb_dev.h
 * @brief The USB device: a serial port and a card reader on one cable.
 *
 * @section usbdev_why Why this file exists at all
 *
 * The board used to get USB serial for free from `pico_enable_stdio_usb(1)`.
 * That is not a library you add things to — it owns the USB descriptors and
 * runs its own polling loop, and the SDK says so: pico_stdio_usb requires that
 * the user not be using TinyUSB directly. Adding a mass storage interface means
 * taking the descriptors over, and taking the descriptors over means providing
 * the serial port ourselves.
 *
 * So this file is the price of the USB drive feature, and it is worth naming
 * plainly: about a hundred lines of descriptor tables and a forty-line stdio
 * driver, replacing one line of CMake. In exchange the device is ours, and a
 * second interface costs a descriptor entry rather than a rewrite.
 *
 * @section usbdev_layout What the host sees
 *
 * One composite device, two functions:
 *
 * - **CDC ACM** on interfaces 0 and 1 — the serial port, exactly as before, so
 *   `SD_DEBUG` and `AM32_DEBUG` output still arrives and picocom still works.
 * - **MSC** on interface 2 — a removable drive that reports *no medium* until
 *   the user asks for it. @see usb_msc.h
 *
 * The IAD descriptor in front of the CDC pair is what stops Windows treating a
 * composite device as one broken function; without it the serial port
 * enumerates and the drive does not, or the other way round depending on the
 * host's mood.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Bring USB up: TinyUSB, the descriptors, and stdio over CDC.
 *
 * Call instead of `stdio_init_all()`, before anything printf()s. Returns
 * immediately; enumeration happens in the background and usbDevTask() drives
 * it.
 */
void usbDevInit();

/**
 * @brief Service USB. Call from the core0 loop, as often as convenient.
 *
 * Must be called from core0 only, and must keep being called while a host is
 * reading the card — this is what moves the data. It is bounded work: TinyUSB
 * processes what has arrived and returns.
 */
void usbDevTask();

/** @brief True once a host has configured the device. @return Enumerated. */
bool usbDevMounted();
