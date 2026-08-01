/**
 * @file check.h
 * @brief Minimal assertion helpers shared by the host test suites.
 *
 * Deliberately tiny: no framework to install, so CI needs nothing but a C++
 * compiler and the tests stay runnable from a bare checkout.
 */

#pragma once

#include <stdio.h>
#include <string.h>

/** @brief Count of failed checks across every suite in this run. */
extern int g_failures;

/** @brief Print a section heading. */
inline void section(const char *name) { printf("\n%s\n", name); }

/**
 * @brief Assert an integer is within @p tol of @p want.
 * @param what Description printed in the result line.
 */
inline void checkInt(const char *what, long got, long want, long tol = 0) {
	bool ok = (got >= want - tol && got <= want + tol);
	if (!ok) g_failures++;
	printf("  %-44s %6ld  want %6ld  %s\n", what, got, want, ok ? "ok" : "FAIL");
}

/** @brief Assert a condition holds. */
inline void checkTrue(const char *what, bool ok) {
	if (!ok) g_failures++;
	printf("  %-44s %6s  %s\n", what, ok ? "yes" : "no", ok ? "ok" : "FAIL");
}

/** @brief Assert two strings match exactly. */
inline void checkStr(const char *what, const char *got, const char *want) {
	bool ok = (strcmp(got, want) == 0);
	if (!ok) g_failures++;
	printf("  %-24s %-14s want %-14s %s\n", what, got, want, ok ? "ok" : "FAIL");
}
