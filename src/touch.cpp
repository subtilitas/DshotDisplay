/**
 * @file touch.cpp
 * @brief Dispatch to whichever touch driver the active board names.
 *
 * Three lines of indirection that buy a unified image. Every caller still says
 * touchInit() and touchPoll(); which chip answers is a property of
 * @ref g_board rather than of which file the preprocessor left standing.
 */

#include "touch.h"
#include "board_desc.h"

const TouchDriver *touchActiveDriver() {
	return g_board ? g_board->touch : nullptr;
}

bool touchInit() {
	const TouchDriver *d = touchActiveDriver();
	return d && d->init ? d->init() : false;
}

void touchPoll(TouchState *t) {
	const TouchDriver *d = touchActiveDriver();
	if (d && d->poll) d->poll(t);
}
