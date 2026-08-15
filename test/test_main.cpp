/**
 * @file test_main.cpp
 * @brief Entry point for the host test suites.
 *
 * Exit status is 0 or 1, never the failure count. A process exit status is
 * eight bits: returning the count meant that 256 failures reported success, and
 * CI would have gone green on a suite that failed every check it ran.
 */

#include <stdio.h>

int g_failures = 0;

void runAm32Tests();
void runUiTests();
void runKissTests();
void runGfxTests();
void runBlackboxTests();
void runLogRingTests();
void runSettingsTests();

int main() {
	printf("DshotDisplay host tests\n");
	printf("=======================\n");

	runAm32Tests();
	runUiTests();
	runKissTests();
	runGfxTests();
	runBlackboxTests();
	runLogRingTests();
	runSettingsTests();

	printf("\n=======================\n");
	if (g_failures) printf("%d CHECK(S) FAILED\n", g_failures);
	else            printf("all checks passed\n");
	return g_failures ? 1 : 0;
}
