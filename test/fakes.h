/**
 * @file fakes.h
 * @brief Controls for the host-test fakes. @see fakes.cpp
 */

#pragma once

#include <stdint.h>

struct EscTelemetry;

/** @brief Advance the virtual clock. Time only moves when a test says so. */
void fakeAdvance(uint32_t ms);

/** @brief Read the virtual clock. */
uint32_t fakeNow();

/** @brief Begin a touch at (@p x, @p y); sets the one-poll `pressed` edge. */
void fakePress(int x, int y);

/** @brief Continue a touch at (@p x, @p y) without a new `pressed` edge. */
void fakeHold(int x, int y);

/** @brief End the current touch; sets the one-poll `released` edge. */
void fakeRelease();

/** @brief Read a byte of the fake ESC's settings page, as actually stored. */
uint8_t fakeEscByte(int offset);

/** @brief Throttle most recently commanded to the fake DShot task, 0..2000. */
uint16_t fakeThrottle();

/** @brief Arm state of the fake DShot task. */
bool fakeArmed();

/** @brief True when the DShot task has been resumed and owns the pin again. */
bool fakePinReturned();

/** @brief Set the telemetry the fake ESC reports. */
void fakeSetTelemetry(const EscTelemetry *t);

/**
 * @brief Count framebuffer pixels of an exact colour.
 *
 * Used to prove a screen left nothing of the previous one behind: the cyan of
 * the AM32 button is unique to the settings screen, so any of it surviving on
 * the tester screen is a leftover.
 */
int fakeCountColour(uint16_t c);

/** @brief Write the current framebuffer to a binary PPM, for eyeballing. */
void fakeDumpFrame(const char *name);

/** @brief Queue bytes as if the desktop host had sent them over USB. */
void fakeHostSend(const uint8_t *d, int n);

/** @brief Read back everything the board has written to USB. @return count. */
int fakeHostReceived(uint8_t *out, int maxLen);

/**
 * @brief Read and consume, the way pyserial's read_all() behaves.
 *
 * fakeHostReceived() only peeks, which is fine for asserting on a finished
 * exchange but wrong for modelling a host that polls: a peek makes every poll
 * return the same bytes for ever.
 *
 * @param[out] out    Destination.
 * @param      maxLen Capacity.
 * @return Bytes copied and removed from the queue.
 */
int fakeHostDrain(uint8_t *out, int maxLen);

/** @brief Let Serial.printf reach stdout, so a dump can be inspected. */
void fakeSerialPrint(bool on);

/** @brief Reset both USB directions. */
void fakeHostClear();

