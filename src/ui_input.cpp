/**
 * @file ui_input.cpp
 * @brief The held-repeat rule. Pure; compiled into the host suite.
 */

#include "ui_input.h"
#include "touch.h"

static bool inRect(int px, int py, int x, int y, int w, int h) {
	return px >= x && px < x + w && py >= y && py < y + h;
}

bool inputPressing(const TouchState *t, int x, int y, int w, int h) {
	return t->down && inRect(t->x, t->y, x, y, w, h)
	               && inRect(t->downX, t->downY, x, y, w, h);
}

bool inputTapped(const TouchState *t, int x, int y, int w, int h) {
	return t->released && inRect(t->x, t->y, x, y, w, h)
	                   && inRect(t->downX, t->downY, x, y, w, h);
}

uint32_t repeatInterval(uint32_t heldMs) {
	// Three tiers rather than a curve. A curve reads better in source and is
	// indistinguishable under a thumb, and these numbers were chosen against a
	// real panel: 120 ms is "stepping", 60 ms is "moving", 25 ms is "spinning".
	if (heldMs > 2500) return 25;
	if (heldMs > 1200) return 60;
	return 120;
}

bool repeatFires(Repeat *r, int dir, uint32_t nowMs) {
	if (dir == 0) {
		r->dir = 0;
		return false;
	}

	// A new press, or a finger that slid from one stepper onto its opposite.
	// Both restart the acceleration: continuing it would make the value fly off
	// in the new direction at whatever rate the old one had built up.
	if (dir != r->dir) {
		r->dir = dir;
		r->startMs = nowMs;
		r->lastMs = nowMs;
		return true;
	}

	if ((uint32_t)(nowMs - r->startMs) <= REPEAT_DELAY_MS) return false;
	if ((uint32_t)(nowMs - r->lastMs) < repeatInterval(nowMs - r->startMs)) return false;

	r->lastMs = nowMs;
	return true;
}
