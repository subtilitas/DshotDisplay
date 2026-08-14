/**
 * @file touch_stubs.cpp
 * @brief Placeholder touch drivers, so sdtest can link the board descriptors.
 *
 * sdtest links board_desc*.cpp for one reason: sdHwConfigApply() reads the SD
 * wiring out of @ref g_board. But each descriptor also points at its board's
 * touch driver, and the real drivers would drag in the whole I2C touch stack
 * of a tool that is deliberately headless -- it prints to USB serial and
 * never polls the panel. These placeholders satisfy the linker instead; if
 * anything in sdtest ever *calls* them, init reporting failure is the honest
 * answer.
 */

#include "board_desc.h"

static bool stubInit(void) { return false; }
static void stubPoll(struct TouchState *) {}

// `extern` before the definition, as in the real drivers: a namespace-scope
// `const` has internal linkage in C++, and without it the descriptors'
// declarations would never find these objects.
extern const TouchDriver TOUCH_DRIVER_CST816D;
extern const TouchDriver TOUCH_DRIVER_CST816D = { "none", stubInit, stubPoll };
extern const TouchDriver TOUCH_DRIVER_CST328;
extern const TouchDriver TOUCH_DRIVER_CST328  = { "none", stubInit, stubPoll };
