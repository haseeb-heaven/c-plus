/* extra_time_boundaries — deterministic boundary coverage for stdlib/time.hp
 * (agent 2, "close remaining UNTESTED time.hp boundary paths").
 *
 * The single program is self-checking.  Every check prints
 * "<tag> <0|1>" and the final line is
 * "EXTRA_TIME_BOUNDARIES <checks> <failures>".  The exit code is 0 only when
 * there are zero failures.
 *
 * Coverage (all inputs are fixed constants; the only wall-clock-dependent
 * expectations are clock non-decrease and generous sleep bounds):
 *   - time_now_ns/ms and time_monotonic_ns/ms positivity and 100 consecutive
 *     non-decreasing samples for each of the four clock helpers;
 *   - time_sleep_ms(0) (prompt) and time_sleep_ms(1) (~1 ms, bounded);
 *   - time_format_iso8601 exact outputs for 0, 1, 86400 (ns), 1e9 (ns),
 *     one full day, and UINT64_MAX (the unsigned-nanosecond wrap is recorded
 *     as a well-formed future date, NOT forced to fail);
 *   - NULL output buffer, zero size, and the INVALID-vs-BOUNDS asymmetry for
 *     truncated sizes 1/4/20; err == NULL tolerated;
 *   - exact-fit 21-byte buffer with adjacent guard bytes and 20-char output;
 *   - a roomy buffer guard, an unsigned wrapped-negative caller value, and
 *     repeated conversions after a failure (error record reset at call entry).
 *
 * No header, existing test, or other agent file is modified.
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/time.hp"

static int et_checks = 0;
static int et_failures = 0;

static void et_check(const char *tag, int condition) {
    et_checks++;
    if (!condition)
        et_failures++;
    printf("%s %d\n", tag, condition ? 1 : 0);
}

/* Exact ISO-8601 conversion into a roomy stack buffer. */
static void et_exact(const char *tag, unsigned long long ns, const char *want) {
    char b[64];
    error e = error();
    int rc = time_format_iso8601(ns, b, sizeof(b), &e);
    et_check(tag, rc == STDLIB_OK && e.ok() && strlen(b) == 20 &&
                  strcmp(b, want) == 0);
}

int main(void) {
    error err = error();
    char buf[64];
    int rc;
    int i;

    /* ---------------------------------------------------------- clocks */
    et_check("now_ns_positive", time_now_ns() > 0ULL);
    et_check("now_ms_positive", time_now_ms() > 0ULL);
    et_check("mono_ns_positive", time_monotonic_ns() > 0ULL);
    et_check("mono_ms_positive", time_monotonic_ms() > 0ULL);

    /* 100 consecutive samples of each clock helper must never decrease.
     * CLOCK_REALTIME can be stepped by NTP; over this short window it is not,
     * so the non-decrease property holds for both realtime and monotonic. */
    {
        unsigned long long p_ns = time_now_ns();
        unsigned long long p_ms = time_now_ms();
        unsigned long long m_ns = time_monotonic_ns();
        unsigned long long m_ms = time_monotonic_ms();
        int ok_ns = 1, ok_ms = 1, ok_mns = 1, ok_mms = 1;
        unsigned long long v;
        for (i = 0; i < 100; i++) {
            v = time_now_ns();
            if (v < p_ns) ok_ns = 0;
            p_ns = v;
            v = time_now_ms();
            if (v < p_ms) ok_ms = 0;
            p_ms = v;
            v = time_monotonic_ns();
            if (v < m_ns) ok_mns = 0;
            m_ns = v;
            v = time_monotonic_ms();
            if (v < m_ms) ok_mms = 0;
            m_ms = v;
        }
        et_check("now_ns_100_nondec", ok_ns && p_ns > 0ULL);
        et_check("now_ms_100_nondec", ok_ms && p_ms > 0ULL);
        et_check("mono_ns_100_nondec", ok_mns && m_ns > 0ULL);
        et_check("mono_ms_100_nondec", ok_mms && m_ms > 0ULL);
    }

    /* ----------------------------------------------------------- sleep */
    {
        unsigned long long t0 = time_monotonic_ns();
        time_sleep_ms(0);
        unsigned long long t1 = time_monotonic_ns();
        et_check("sleep_zero_prompt",
                 t1 >= t0 && (t1 - t0) < 100000000ULL); /* < 100 ms */
        t0 = time_monotonic_ns();
        time_sleep_ms(1);
        t1 = time_monotonic_ns();
        et_check("sleep_one_ms",
                 t1 >= t0 && (t1 - t0) >= 500000ULL &&
                 (t1 - t0) < 100000000ULL); /* 0.5..100 ms */
    }

    /* ------------------------------------------- exact ISO-8601 outputs */
    et_exact("iso_epoch_0", 0ULL, "1970-01-01T00:00:00Z");
    et_exact("iso_one_ns", 1ULL, "1970-01-01T00:00:00Z");
    et_exact("iso_86400_ns", 86400ULL, "1970-01-01T00:00:00Z");
    et_exact("iso_one_sec_1e9", 1000000000ULL, "1970-01-01T00:00:01Z");
    et_exact("iso_one_day", 86400000000000ULL, "1970-01-02T00:00:00Z");
    /* Unsigned-nanosecond wrap: ULLONG_MAX is formatted as a well-formed
     * future date (documented non-defect; never forced to fail). */
    et_exact("iso_uint64_max", 18446744073709551615ULL,
             "2554-07-21T23:34:33Z");

    /* ----------------------------------------- NULL / zero / error props */
    memset(buf, 'X', sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    rc = time_format_iso8601(1000000000ULL, NULL, 64, &err);
    et_check("iso_null_buf", rc == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_INVALID &&
             err.code() == CPLUS_EXIT_UNSUPPORTED && err.what()[0] != '\0');

    rc = time_format_iso8601(1000000000ULL, buf, 0, &err);
    et_check("iso_zero_size", rc == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_INVALID &&
             err.code() == CPLUS_EXIT_UNSUPPORTED && buf[0] == 'X');

    /* A NULL error pointer is tolerated even on the failure path. */
    rc = time_format_iso8601(1000000000ULL, NULL, 64, NULL);
    et_check("iso_null_buf_null_err", rc == STDLIB_FAIL);

    /* ---------------------------------------- truncated buffers (BOUNDS) */
    memset(buf, 'X', sizeof(buf));
    rc = time_format_iso8601(1000000000ULL, buf, 1, &err);
    et_check("iso_size_one", rc == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_BOUNDS &&
             err.code() == CPLUS_EXIT_BOUNDS && buf[0] == '\0');
    rc = time_format_iso8601(1000000000ULL, buf, 4, &err);
    et_check("iso_size_four", rc == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_BOUNDS &&
             err.code() == CPLUS_EXIT_BOUNDS && buf[0] == '\0');
    rc = time_format_iso8601(1000000000ULL, buf, 20, &err);
    et_check("iso_size_twenty", rc == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_BOUNDS &&
             err.code() == CPLUS_EXIT_BOUNDS && buf[0] == '\0');

    /* ---------------------- exact-fit 21-byte buffer with guard sentinels */
    {
        char guarded[23];
        memset(guarded, 'G', sizeof(guarded));
        rc = time_format_iso8601(1000000000ULL, guarded + 1, 21, &err);
        et_check("iso_exact21_guards", rc == STDLIB_OK && err.ok() &&
                 strlen(guarded + 1) == 20 &&
                 (guarded + 1)[19] == 'Z' && (guarded + 1)[20] == '\0' &&
                 strcmp(guarded + 1, "1970-01-01T00:00:01Z") == 0 &&
                 guarded[0] == 'G' && guarded[22] == 'G');
    }

    /* Roomy buffer must leave the byte just past the 21-byte output intact. */
    memset(buf, 'X', sizeof(buf));
    rc = time_format_iso8601(1000000000ULL, buf, sizeof(buf), &err);
    et_check("iso_roomy_guard", rc == STDLIB_OK &&
             strcmp(buf, "1970-01-01T00:00:01Z") == 0 && buf[21] == 'X');

    /* A negative caller value wraps into the unsigned API; the result is a
     * well-formed future date, not a rejection (documented non-defect). */
    rc = time_format_iso8601((unsigned long long)(-86400000000000LL),
                             buf, sizeof(buf), &err);
    et_check("iso_wrapped_negative", rc == STDLIB_OK && err.ok() &&
             strlen(buf) == 20 && buf[4] == '-' && buf[7] == '-' &&
             buf[10] == 'T' && buf[13] == ':' && buf[16] == ':' &&
             buf[19] == 'Z');

    /* ------------------------ repeated conversion after a failure/reset */
    rc = time_format_iso8601(1000000000ULL, NULL, 64, &err);
    {
        int had_failed = (rc == STDLIB_FAIL && !err.ok());
        rc = time_format_iso8601(1000000000ULL, buf, sizeof(buf), &err);
        et_check("iso_recovery_after_failure", had_failed && rc == STDLIB_OK &&
                 err.ok() && strcmp(buf, "1970-01-01T00:00:01Z") == 0);
    }
    /* The helper is stateless: repeated identical calls give identical bytes. */
    for (i = 0; i < 5; i++) {
        rc = time_format_iso8601(1700000000000000000ULL, buf, sizeof(buf), &err);
        if (rc != STDLIB_OK || !err.ok() ||
            strcmp(buf, "2023-11-14T22:13:20Z") != 0)
            break;
    }
    et_check("iso_repeated_valid", i == 5);

    printf("EXTRA_TIME_BOUNDARIES %d %d\n", et_checks, et_failures);
    return et_failures ? 1 : 0;
}
