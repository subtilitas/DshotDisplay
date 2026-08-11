/**
 * @file fakes.h
 * @brief Controls for the host-test fakes. @see fakes.cpp
 */

#pragma once

#include <stdint.h>

struct EscTelemetry;

/** @brief Advance the virtual clock. Time only moves when a test says so. */
void fakeAdvance(uint32_t ms);

struct SdLogStatus;

/** @brief Force the fake logger into a given state. @param st What to report. */
void fakeSdLogSet(const SdLogStatus *st);

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

/** @brief Write the current framebuffer to a binary PPM, for eyeballing. */
void fakeDumpFrame(const char *name);
