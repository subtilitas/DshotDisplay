/**
 * @file ui_am32.h
 * @brief AM32 ESC configuration screen.
 *
 * A separate mode from the throttle tester: entering it disarms, tears down the
 * DShot driver and hands the signal pin to the bootloader transport. Leaving it
 * gives the pin back.
 *
 * @see am32_bl.h for the transport, am32_eeprom.h for the settings layout.
 */

#pragma once

#include <stdint.h>

struct TouchState;

/** @brief Enter config mode. Disarms and begins releasing the DShot pin. */
void uiAm32Enter();

/** @brief Leave config mode and hand the pin back to the DShot task. */
void uiAm32Exit();

/** @brief True while config mode owns the screen. */
bool uiAm32Active();

/**
 * @brief Run one frame of the config screen.
 * @param t Touch state for this frame.
 * @return false once the user has asked to leave.
 */
bool uiAm32Tick(const TouchState *t);
