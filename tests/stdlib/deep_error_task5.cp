/* Deep task-5 gap test for stdlib/error.hp.
 *
 * Complements deep_error_vector.cp (which already covers 255/256/288-byte
 * truncation, reset/set, NULL free helpers, category mapping for every exit
 * code, status mapping and category names). This file covers only NEW gaps:
 *   E1  exact boundary lengths 0/1/254/255/256/257 and NUL termination,
 *   E2  NULL callback + reentrant callback calling error_fail (latch check),
 *   E3  1000 reset/fail cycles with telemetry balance,
 *   E4  error object copy and move (both supported by this plain class),
 *   E5  invalid categories/codes and the "unknown" category-name fallback.
 *
 * Self-checking: each check prints "<tag> <0|1>". The final line is
 * "ERROR_TASK5 <checks> <failures>". Exit 0 only when failures == 0.
 * No fatal is executed. Documented SKIPs print "<tag> 1" plus SKIP_REASON.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int t5_checks = 0;
static int t5_failures = 0;
static int t5_reentrant_calls = 0;
static error *t5_reentrant_target = (error *)0;

static void t5_check(const char *tag, int cond) {
    t5_checks++;
    if (!cond)
        t5_failures++;
    print(tag, cond ? 1 : 0);
}

static void t5_skip(const char *tag, const char *reason) {
    t5_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
}

/* A callback that re-enters the error API. stdlib_note() has a per-thread
 * reporting guard, so the nested error_fail must note counters but must NOT
 * re-invoke the callback (no recursion) and must not leave the guard latched.
 * The nested failure targets its own record: error_fail_cat() captures fields
 * before the callback runs, so reusing the outer slot would intentionally
 * overwrite it with the nested state (RED-20 E3) and test recursion, not the
 * outer record. */
static void t5_reentrant_cb(int category, cplus_exit_code code,
                            const char *message) {
    (void)category;
    (void)code;
    (void)message;
    t5_reentrant_calls++;
    if (t5_reentrant_target != (error *)0) {
        error nested = error();
        error_fail(&nested, CPLUS_EXIT_RESOURCE, "nested");
    }
}

/* One message-length boundary. Payload capacity is STDLIB_ERROR_SIZE-1 (255);
 * text[255] is always the NUL terminator. */
static void t5_boundary(const char *tag, int n) {
    error slot = error();
    char payload[STDLIB_ERROR_SIZE + 16];
    int m;
    int expected;
    int ok;
    for (m = 0; m < n; m++)
        payload[m] = 'Z';
    payload[n] = '\0';
    error_fail_cat(&slot, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, payload);
    expected = n < (int)STDLIB_ERROR_SIZE - 1 ? n : (int)STDLIB_ERROR_SIZE - 1;
    ok = slot.failed_now() == 1;
    ok = ok && slot.category() == STDLIB_CATEGORY_RANGE;
    ok = ok && slot.code() == (int)CPLUS_EXIT_BOUNDS;
    ok = ok && (int)strlen(slot.what()) == expected;
    ok = ok && slot.what()[(int)STDLIB_ERROR_SIZE - 1] == '\0';
    ok = ok && memcmp(slot.what(), payload, (size_t)expected) == 0;
    t5_check(tag, ok);
}

int main(void) {
    stdlib_telemetry_stats tm;

    /* ---- E1: exact message-length boundaries ---------------------------- */
    t5_boundary("err_bound_0", 0);
    t5_boundary("err_bound_1", 1);
    t5_boundary("err_bound_254", 254);
    t5_boundary("err_bound_255", 255);
    t5_boundary("err_bound_256", 256);
    t5_boundary("err_bound_257", 257);

    /* A long message followed by a short one must leave no stale tail. */
    {
        error slot = error();
        char long_msg[STDLIB_ERROR_SIZE + 8];
        int m;
        for (m = 0; m < STDLIB_ERROR_SIZE + 7; m++)
            long_msg[m] = 'Q';
        long_msg[STDLIB_ERROR_SIZE + 7] = '\0';
        error_fail_cat(&slot, STDLIB_CATEGORY_IO, CPLUS_EXIT_IO, long_msg);
        error_fail_cat(&slot, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "s");
        t5_check("err_no_stale_tail",
                 slot.failed_now() == 1 && strcmp(slot.what(), "s") == 0 &&
                     slot.what()[1] == '\0' &&
                     slot.category() == STDLIB_CATEGORY_INVALID);
    }

    /* ---- E2: NULL callback, then a reentrant callback ------------------- */
    {
        error slot = error();
        stdlib_set_error_callback((stdlib_error_callback)0);
        error_fail(&slot, CPLUS_EXIT_IO, "no-callback");
        t5_check("err_null_callback",
                 slot.failed_now() == 1 && slot.code() == (int)CPLUS_EXIT_IO &&
                     strcmp(slot.what(), "no-callback") == 0);

        t5_reentrant_target = &slot;
        t5_reentrant_calls = 0;
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(t5_reentrant_cb);

        error_fail(&slot, CPLUS_EXIT_IO, "outer");
        /* The outer error_fail_cat writes its fields AFTER stdlib_note returns,
         * so the nested failure is counted but the outer record wins. */
        t5_check("err_reentrant_no_recursion",
                 t5_reentrant_calls == 1 && slot.failed_now() == 1 &&
                     slot.category() == STDLIB_CATEGORY_IO &&
                     slot.code() == (int)CPLUS_EXIT_IO &&
                     strcmp(slot.what(), "outer") == 0);

        t5_reentrant_calls = 0;
        error_fail(&slot, CPLUS_EXIT_BOUNDS, "second");
        /* A second failure must invoke the callback again: no stuck latch. */
        t5_check("err_reentrant_no_latch",
                 t5_reentrant_calls == 1 &&
                     strcmp(slot.what(), "second") == 0 &&
                     slot.code() == (int)CPLUS_EXIT_BOUNDS);

        stdlib_telemetry_snapshot(&tm);
        /* outer IO (invalid_ops), nested MEMORY, outer BOUNDS, nested MEMORY
         * -> 4 api_failures; 2 alloc_failures; 1 invalid_ops. */
        t5_check("err_reentrant_telemetry",
                 tm.api_failures == 4ULL && tm.alloc_failures == 2ULL &&
                     tm.invalid_ops == 1ULL);

        stdlib_set_error_callback((stdlib_error_callback)0);
        t5_reentrant_target = (error *)0;
        stdlib_telemetry_set_enabled(0);
    }

    /* ---- E3: 1000 reset/fail cycles with telemetry balance ------------- */
    {
        error slot = error();
        int i;
        int ok;
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback((stdlib_error_callback)0);
        for (i = 0; i < 1000; i++) {
            error_reset(&slot);
            error_fail(&slot, CPLUS_EXIT_IO, "cycle");
        }
        stdlib_telemetry_snapshot(&tm);
        ok = tm.api_failures == 1000ULL && tm.invalid_ops == 1000ULL;
        ok = ok && slot.failed_now() == 1 &&
                   slot.category() == STDLIB_CATEGORY_IO &&
                   slot.code() == (int)CPLUS_EXIT_IO &&
                   strcmp(slot.what(), "cycle") == 0;
        t5_check("err_1000_cycles", ok);
        error_reset(&slot);
        t5_check("err_1000_reset_clean",
                 slot.ok() == 1 && slot.failed_now() == 0 &&
                     slot.category() == STDLIB_CATEGORY_OK &&
                     slot.code() == (int)CPLUS_EXIT_OK &&
                     strcmp(slot.what(), "") == 0);
        stdlib_telemetry_set_enabled(0);
    }

    /* ---- E4: copy / move of the plain error value ----------------------- */
    {
        error t5_orig = error();
        t5_orig.set(CPLUS_EXIT_IO, "payload");
        error t5_copy = t5_orig;
        t5_check("err_copy_state",
                 t5_copy.failed_now() == 1 &&
                     t5_copy.category() == STDLIB_CATEGORY_IO &&
                     t5_copy.code() == (int)CPLUS_EXIT_IO &&
                     strcmp(t5_copy.what(), "payload") == 0);
        t5_copy.set(CPLUS_EXIT_BOUNDS, "changed");
        t5_check("err_copy_independent",
                 strcmp(t5_orig.what(), "payload") == 0 &&
                     t5_orig.code() == (int)CPLUS_EXIT_IO &&
                     strcmp(t5_copy.what(), "changed") == 0 &&
                     t5_copy.code() == (int)CPLUS_EXIT_BOUNDS);
        error t5_moved = cplus_move(t5_orig);
        t5_check("err_move_state",
                 t5_moved.failed_now() == 1 &&
                     t5_moved.code() == (int)CPLUS_EXIT_IO &&
                     strcmp(t5_moved.what(), "payload") == 0 &&
                     t5_orig.failed_now() == 0 &&
                     t5_orig.ok() == 1);
    }

    /* ---- E5: invalid categories/codes and unknown-name fallback -------- */
    {
        error slot = error();
        t5_check("err_fresh_category_ok",
                 slot.ok() == 1 && slot.category() == STDLIB_CATEGORY_OK);
        error_fail_cat(&slot, 999, (cplus_exit_code)7777, "weird");
        t5_check("err_invalid_category_code",
                 slot.failed_now() == 1 && slot.category() == 999 &&
                     slot.code() == 7777 &&
                     strcmp(slot.what(), "weird") == 0);
        t5_check("err_unknown_name",
                 strcmp(stdlib_category_name(999), "unknown") == 0 &&
                     strcmp(stdlib_category_name(-5), "unknown") == 0);
        error_fail_cat(&slot, -5, (cplus_exit_code)-1, "neg");
        t5_check("err_negative_category",
                 slot.category() == -5 && slot.code() == -1 &&
                     strcmp(slot.what(), "neg") == 0);
        error_reset(&slot);
        error_fail(&slot, (cplus_exit_code)4242, "badcode");
        t5_check("err_invalid_exit_code",
                 slot.failed_now() == 1 &&
                     slot.category() == STDLIB_CATEGORY_INVALID &&
                     slot.code() == 4242 &&
                     strcmp(slot.what(), "badcode") == 0);
        /* There is no exit-code-to-name API; only stdlib_category_name(). */
        t5_skip("err_code_name_mapping",
                "stdlib/error.hp exposes no exit-code-to-name function; only "
                "stdlib_category_name() with an \"unknown\" fallback, covered "
                "by err_unknown_name and by DEEP_ERROR_VECTOR_CONTRACT.md E4.");
    }

    print("ERROR_TASK5", t5_checks, t5_failures);
    return t5_failures == 0 ? 0 : 1;
}
