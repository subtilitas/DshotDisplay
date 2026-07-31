/**
 * @file ui.h
 * @brief Touchscreen UI for the DShot motor tester.
 *
 * Owns the arm/throttle state machine and all rendering. Runs entirely on
 * core0 and pushes commands to core1 through the @ref esc_core0 API.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Reset UI state, seed the ADC, and clear the screen.
 * @pre st7789Init() and touchInit() must already have run.
 */
void uiInit();

/**
 * @brief Run one UI frame.
 *
 * Sends the heartbeat, samples touch and battery, updates the arm and throttle
 * state machines, repaints whatever changed, and flushes it to the panel. Call
 * at a steady rate; the firmware uses ~40 Hz.
 */
void uiTick();

/** @brief Current commanded throttle, 0..2000. @return Throttle value. */
uint16_t uiThrottle();

/** @brief Current arm state. @return true when armed. */
bool     uiArmed();
