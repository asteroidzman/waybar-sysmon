// Unit tests for sysmon.c's pure logic (cpu_delta) -- no GTK init, no
// Wayland, no waybar process. #includes the plugin source directly to reach
// its `static` functions without changing their visibility for production
// code; this file supplies its own main() (sysmon.c has none), so nothing
// conflicts.
#include "../src/sysmon.c"
#include <assert.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
	if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
	else { printf("ok - %s\n", msg); } \
} while (0)
#define CHECK_DOUBLE_EQ(a, b, msg) CHECK(((a) - (b) < 0.01 && (b) - (a) < 0.01), msg)

int main(void) {
	CpuT prev = {0, 0};

	// first sample just primes prev, but the RETURNED percentage for THAT
	// call still reflects prev's (zero) starting point vs the new totals
	double pct = cpu_delta(&prev, 100, 50);
	CHECK_DOUBLE_EQ(pct, 50.0, "first sample: 100 total/50 idle -> 50% busy");
	CHECK(prev.total == 100 && prev.idle == 50, "first sample updates prev to the new totals");

	// second sample: delta-based, not absolute -- 100 more total, 75 more
	// idle since last sample (125-50) -> 25% busy over that window
	pct = cpu_delta(&prev, 200, 125);
	CHECK_DOUBLE_EQ(pct, 25.0, "second sample: delta-based 25% busy");

	// no time elapsed (dt==0): must return 0, not divide by zero
	pct = cpu_delta(&prev, 200, 125);
	CHECK_DOUBLE_EQ(pct, 0.0, "unchanged totals (dt==0) returns 0, no divide-by-zero");

	// fully idle window: 0% busy
	CpuT idle_prev = {0, 0};
	pct = cpu_delta(&idle_prev, 100, 100);
	CHECK_DOUBLE_EQ(pct, 0.0, "all-idle window is 0% busy");

	// fully busy window: 100% busy
	CpuT busy_prev = {0, 0};
	pct = cpu_delta(&busy_prev, 100, 0);
	CHECK_DOUBLE_EQ(pct, 100.0, "zero-idle window is 100% busy");

	printf("----\n%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
