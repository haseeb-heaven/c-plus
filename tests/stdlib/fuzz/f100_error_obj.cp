/* F-099 -- deterministic fuzz configuration: error.hp objects and callbacks.
 *
 * Bounded, fixed-seed exercise of error construction, set/set_category,
 * clear/reset, the free helpers (including NULL targets), exact message
 * truncation at STDLIB_ERROR_SIZE-1, category mapping for every exit code,
 * telemetry counters and the diagnostics callback (including its re-entrancy
 * guard).  The callback records only category/code/count; no buffer content
 * is transmitted.  SIGALRM watchdog + external `timeout 30s`.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

#define CYCLES 600
#define FNV_PRIME 1099511628211ULL

static unsigned long long rng_state = 0xF0991234ULL;
static unsigned long long digest = 0xcbf29ce484222325ULL;
static int failures = 0;
static int cb_calls = 0;
static int cb_category = 0;
static int cb_code = 0;
static error *cb_target = (error *)0;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

static void mix(unsigned long long v) {
    digest = (digest ^ v) * FNV_PRIME;
}

static void invariant(const char *tag) {
    failures++;
    print("INVARIANT", tag);
}

static void on_alarm(int sig) {
    static const char msg[] = "F099 watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

/* Re-entrant callback: stdlib_note() must drop nested reports, leaving the
 * guard unlatched for the next top-level failure. */
static void counting_cb(int category, cplus_exit_code code,
                        const char *message) {
    (void)message;
    cb_calls++;
    cb_category = category;
    cb_code = (int)code;
    if (cb_target != (error *)0)
        error_fail(cb_target, CPLUS_EXIT_RESOURCE, "nested");
}

static int expected_category(cplus_exit_code code) {
    switch (code) {
    case CPLUS_EXIT_IO:
        return STDLIB_CATEGORY_IO;
    case CPLUS_EXIT_RESOURCE:
        return STDLIB_CATEGORY_MEMORY;
    case CPLUS_EXIT_BOUNDS:
        return STDLIB_CATEGORY_BOUNDS;
    case CPLUS_EXIT_OWNERSHIP:
        return STDLIB_CATEGORY_OWNERSHIP;
    case CPLUS_EXIT_USE_AFTER_FREE:
    case CPLUS_EXIT_DOUBLE_FREE:
        return STDLIB_CATEGORY_LIFECYCLE;
    default:
        return STDLIB_CATEGORY_INVALID;
    }
}

/* Exact truncation boundary for a message of n bytes. */
static void boundary_case(int n) {
    char payload[STDLIB_ERROR_SIZE + 128];
    error boundary = error();
    int i;
    int expect = n < (int)STDLIB_ERROR_SIZE - 1
                     ? n : (int)STDLIB_ERROR_SIZE - 1;
    for (i = 0; i < n; i++)
        payload[i] = (char)('A' + (i % 26));
    payload[n] = '\0';
    boundary.set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, payload);
    if (boundary.failed_now() != 1 ||
        boundary.category() != STDLIB_CATEGORY_RANGE ||
        boundary.code() != (int)CPLUS_EXIT_BOUNDS ||
        (int)strlen(boundary.what()) != expect ||
        boundary.what()[STDLIB_ERROR_SIZE - 1] != '\0' ||
        memcmp(boundary.what(), payload, (size_t)expect) != 0)
        invariant("f099_truncation");
    mix((unsigned long long)n);
    mix((unsigned long long)expect);
    mix((unsigned long long)strlen(boundary.what()));
    mix((unsigned char)boundary.what()[0]);
    mix((unsigned char)boundary.what()[expect ? expect - 1 : 0]);
    boundary.clear();
    if (!boundary.ok() || boundary.category() != STDLIB_CATEGORY_OK ||
        boundary.code() != (int)CPLUS_EXIT_OK || boundary.what()[0] != '\0')
        invariant("f099_clear");
}

int main(void) {
    char hex[32];
    char msg[STDLIB_ERROR_SIZE + 32];
    stdlib_telemetry_stats after;
    error slot = error();
    error nested = error();
    error last = error();
    int i;
    int n;
    int code;
    int cat;
    int before_calls;
    int ok;

    (void)!signal(SIGALRM, on_alarm);
    (void)!alarm(20);

    /* ---- exact message-length boundaries ------------------------------- */
    boundary_case(0);
    boundary_case(1);
    boundary_case(2);
    boundary_case(127);
    boundary_case(254);
    boundary_case(255);
    boundary_case(256);
    boundary_case(257);
    boundary_case(300);
    boundary_case((int)STDLIB_ERROR_SIZE + 64);

    /* A long message followed by a short one must not leave a stale tail. */
    slot.set_category(STDLIB_CATEGORY_IO, CPLUS_EXIT_IO,
                      "0123456789012345678901234567890123456789"
                      "0123456789012345678901234567890123456789"
                      "0123456789012345678901234567890123456789"
                      "0123456789012345678901234567890123456789"
                      "0123456789012345678901234567890123456789"
                      "0123456789012345678901234567890123456789"
                      "0123456789012345678901234567890123456789");
    slot.set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, "short");
    if (strcmp(slot.what(), "short") != 0)
        invariant("f099_stale_tail");
    mix((unsigned long long)strlen(slot.what()));

    /* ---- category mapping for every exit code --------------------------- */
    for (i = 0; i <= 9; i++) {
        code = (int)CPLUS_EXIT_OK + i;
        slot.set((cplus_exit_code)code, "mapped");
        cat = (code == (int)CPLUS_EXIT_OK)
                  ? STDLIB_CATEGORY_INVALID
                  : expected_category((cplus_exit_code)code);
        if (slot.category() != cat || slot.code() != code)
            invariant("f099_category_map");
        mix((unsigned long long)code);
        mix((unsigned long long)slot.category());
    }
    slot.set_category(99, CPLUS_EXIT_UNSUPPORTED, "unknown");
    if (slot.category() != 99 || strcmp(stdlib_category_name(99), "unknown"))
        invariant("f099_unknown_category");
    mix((unsigned long long)slot.category());

    /* ---- NULL targets: free helpers must not crash ---------------------- */
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_set_error_callback(counting_cb);
    before_calls = cb_calls;
    error_reset((error *)0);
    error_fail((error *)0, CPLUS_EXIT_IO, "null-target-io");
    error_fail_cat((error *)0, 99, CPLUS_EXIT_UNSUPPORTED, "null-target-cat");
    if (cb_calls != before_calls + 2)
        invariant("f099_null_callback_count");
    mix((unsigned long long)(cb_calls - before_calls));

    /* ---- re-entrancy guard ---------------------------------------------- */
    nested.clear();
    cb_target = &nested;
    slot.clear();
    before_calls = cb_calls;
    slot.set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, "outer");
    if (cb_calls != before_calls + 1 ||
        strcmp(slot.what(), "outer") != 0 ||
        slot.category() != STDLIB_CATEGORY_RANGE)
        invariant("f099_reentrant_guard");
    /* A second top-level failure must still reach the callback. */
    last.clear();
    slot.clear();
    before_calls = cb_calls;
    slot.set(CPLUS_EXIT_IO, "second");
    cb_target = (error *)0;
    mix((unsigned long long)(cb_calls - before_calls));

    /* ---- seeded reset/fail cycles with telemetry accounting ------------ */
    stdlib_telemetry_reset();
    before_calls = cb_calls;
    for (i = 0; i < CYCLES; i++) {
        n = (int)(rnd() % (unsigned int)STDLIB_ERROR_SIZE);
        code = (int)CPLUS_EXIT_IO + (int)(rnd() % 9u);
        cat = expected_category((cplus_exit_code)code);
        memset(msg, 'x', (size_t)n);
        msg[n] = '\0';
        slot.clear();
        slot.set((cplus_exit_code)code, msg);
        if (!slot.failed_now() || slot.code() != code ||
            slot.category() != cat || (int)strlen(slot.what()) != n)
            invariant("f099_cycle_state");
        mix((unsigned long long)i);
        mix((unsigned long long)code);
        mix((unsigned long long)n);
        mix((unsigned long long)slot.category());
    }
    stdlib_telemetry_snapshot(&after);
    if (after.api_failures != (unsigned long long)CYCLES)
        invariant("f099_telemetry_api_failures");
    if (cb_calls != before_calls + CYCLES)
        invariant("f099_cycle_callback_count");
    mix(after.api_failures);
    mix(after.invalid_ops);
    mix((unsigned long long)(cb_calls - before_calls));

    /* Callback uninstalled: no further increments. */
    stdlib_set_error_callback((stdlib_error_callback)0);
    before_calls = cb_calls;
    slot.set(CPLUS_EXIT_IO, "no-callback");
    if (cb_calls != before_calls)
        invariant("f099_callback_uninstall");
    mix((unsigned long long)(cb_calls - before_calls));
    stdlib_telemetry_set_enabled(0);

    (void)!alarm(0);
    mix((unsigned long long)failures);
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f099_digest", hex);
    if (failures == 0)
        print("f099:", CYCLES, "cases ok");
    else
        print("f099: FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
