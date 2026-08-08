/**
 * @file test_main.cpp
 * @brief Entry point for the host test suites.
 *
 * Exit status is the number of failed checks, so CI fails the job without any
 * extra plumbing.
 */

#include <stdio.h>

int g_failures = 0;

void runAm32Tests();
void runUiTests();
void runGfxTests();

int main() {
	printf("DshotDisplay host tests\n");
	printf("=======================\n");

	runAm32Tests();
	runUiTests();
	runGfxTests();

	printf("\n=======================\n");
	if (g_failures) printf("%d CHECK(S) FAILED\n", g_failures);
	else            printf("all checks passed\n");
	return g_failures;
}
