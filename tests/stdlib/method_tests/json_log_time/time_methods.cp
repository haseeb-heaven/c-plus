/* method_tests/json_log_time/time_methods.cp
 *
 * One deterministic case per public function declared in stdlib/time.hp.
 * Each case CALLS the function and verifies its result, then prints:
 *   time <method> PASS|FAIL
 * No exact wall-clock value is asserted.  Clock getters are verified for
 * non-zero/bounded sanity and internal consistency plus monotonic ordering;
 * formatting is verified against fixed epoch timestamps.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/time.hp"

static int failures = 0;

#define REPORT(method, cond)                                                   \
    do {                                                                       \
        int _ok = (cond) ? 1 : 0;                                              \
        print("time", method, _ok ? "PASS" : "FAIL");                          \
        if (!_ok)                                                              \
            failures++;                                                        \
    } while (0)

int main(void) {
    error err = error();

    /* time_now_ns: realtime clock returns a non-zero epoch-nanosecond value. */
    {
        unsigned long long ns_now = time_now_ns();
        unsigned long long ns_secs = ns_now / 1000000000ULL;
        REPORT("time_now_ns", ns_now > 0 && ns_secs > 0 &&
                                  ns_now % 1000000000ULL < 1000000000ULL);
    }

    /* time_now_ms: millisecond view is consistent with a nearby ns reading. */
    {
        unsigned long long mn_ns = time_now_ns();
        unsigned long long mn_ms = time_now_ms();
        unsigned long long mn_expected = mn_ns / 1000000ULL;
        REPORT("time_now_ms", mn_ms > 0 && mn_ms >= mn_expected &&
                                  mn_ms - mn_expected <= 1000ULL);
    }

    /* time_monotonic_ns: positive and never moves backwards. */
    {
        unsigned long long mo_first = time_monotonic_ns();
        unsigned long long mo_second = time_monotonic_ns();
        REPORT("time_monotonic_ns", mo_first > 0 && mo_second >= mo_first);
    }

    /* time_monotonic_ms: positive, ordered, consistent with monotonic ns. */
    {
        unsigned long long mm_ns = time_monotonic_ns();
        unsigned long long mm_first = time_monotonic_ms();
        unsigned long long mm_second = time_monotonic_ms();
        REPORT("time_monotonic_ms", mm_first > 0 && mm_second >= mm_first &&
                                        mm_first >= mm_ns / 1000000ULL);
    }

    /* time_sleep_ms: a bounded 3 ms sleep advances the monotonic clock. */
    {
        unsigned long long sl_before = time_monotonic_ns();
        time_sleep_ms(3);
        unsigned long long sl_after = time_monotonic_ns();
        unsigned long long sl_elapsed = sl_after - sl_before;
        REPORT("time_sleep_ms", sl_after >= sl_before && sl_elapsed >= 1000000ULL &&
                                    sl_elapsed <= 5000000000ULL);
    }

    /* time_format_iso8601: fixed epochs, truncation, NULL and range errors. */
    {
        char fm_buf[32];
        char fm_small[5];
        int fm_r1 = time_format_iso8601(0ULL, fm_buf, sizeof(fm_buf), &err);
        int fm_ok = fm_r1 == STDLIB_OK && err.ok() &&
                    strcmp(fm_buf, "1970-01-01T00:00:00Z") == 0;
        int fm_r2 = time_format_iso8601(1000000000ULL, fm_buf, sizeof(fm_buf), &err);
        fm_ok = fm_ok && fm_r2 == STDLIB_OK &&
                strcmp(fm_buf, "1970-01-01T00:00:01Z") == 0;
        int fm_r3 = time_format_iso8601(0ULL, fm_small, sizeof(fm_small), &err);
        fm_ok = fm_ok && fm_r3 == STDLIB_FAIL && fm_small[0] == '\0' &&
                err.category() == STDLIB_CATEGORY_BOUNDS;
        int fm_r4 = time_format_iso8601(0ULL, (char *)0, 32, &err);
        fm_ok = fm_ok && fm_r4 == STDLIB_FAIL &&
                err.category() == STDLIB_CATEGORY_INVALID;
        /* Sub-second precision is truncated, never rounded. */
        int fm_r5 = time_format_iso8601(999999999ULL, fm_buf, sizeof(fm_buf), &err);
        fm_ok = fm_ok && fm_r5 == STDLIB_OK && err.ok() &&
                strcmp(fm_buf, "1970-01-01T00:00:00Z") == 0;
        REPORT("time_format_iso8601", fm_ok);
    }

    print("RESULT", "failures", failures);
    return failures != 0;
}
