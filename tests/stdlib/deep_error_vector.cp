/* Deep, independent contract test for stdlib/error.hp and stdlib/vector.hp.
 *
 * Covers the scenarios in docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md:
 *   error : message truncation boundaries, repeated reset/set, NULL handles,
 *           category correctness across every exit code, status mapping.
 *   vector: growth boundaries, empty/stale slices, deterministic reserve and
 *           push_back allocation failure with recovery, empty get/pop, and
 *           allocation-overflow shapes.
 *
 * Every line is a deterministic PASS/FAIL record; the process must exit 0 with
 * no runtime fatal.  Classification tags:
 *   S = success path, R = recoverable failure (-1 / NULL), F = fatal (must NOT
 *       fire; a fatal here means a contract violation and the test fails).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static int g_failures = 0;
static int g_cb_calls = 0;
static int g_cb_cat = -1;
static int g_cb_code = -1;

static void deep_cb(int category, cplus_exit_code code, const char *message) {
    (void)message;
    g_cb_calls++;
    g_cb_cat = category;
    g_cb_code = (int)code;
}

static void expect(int cond, const char *tag, const char *name) {
    if (!cond) {
        g_failures++;
        print("FAIL", tag, name);
    }
}

static void report(const char *name, int ok, const char *tag) {
    print(name, ok ? 1 : 0, tag);
    if (!ok)
        g_failures++;
}

int main(void) {
    stdlib_telemetry_stats tm;

    /* ---------------------------------------------------------------- error */

    /* E1: truncation at STDLIB_ERROR_SIZE-1 (fits) and at/over the boundary. */
    {
        error err = error();
        char msg[STDLIB_ERROR_SIZE + 64];
        int n;
        int all_ok = 1;
        stdlib_set_error_callback(NULL);
        for (n = 0; n < STDLIB_ERROR_SIZE + 64 - 1; n++)
            msg[n] = 'A';
        msg[STDLIB_ERROR_SIZE + 64 - 1] = '\0';

        memset(msg, 'A', STDLIB_ERROR_SIZE - 1);
        msg[STDLIB_ERROR_SIZE - 1] = '\0';
        err.set(CPLUS_EXIT_BOUNDS, msg);
        if (strlen(err.what()) != STDLIB_ERROR_SIZE - 1 ||
            memcmp(err.what(), msg, STDLIB_ERROR_SIZE - 1) != 0)
            all_ok = 0;

        memset(msg, 'A', STDLIB_ERROR_SIZE);
        msg[STDLIB_ERROR_SIZE] = '\0';
        err.set(CPLUS_EXIT_BOUNDS, msg);
        if (strlen(err.what()) != STDLIB_ERROR_SIZE - 1 ||
            memcmp(err.what(), msg, STDLIB_ERROR_SIZE - 1) != 0)
            all_ok = 0;

        memset(msg, 'A', STDLIB_ERROR_SIZE + 32);
        msg[STDLIB_ERROR_SIZE + 32] = '\0';
        err.set(CPLUS_EXIT_BOUNDS, msg);
        if (strlen(err.what()) != STDLIB_ERROR_SIZE - 1 ||
            memcmp(err.what(), msg, STDLIB_ERROR_SIZE - 1) != 0)
            all_ok = 0;

        err.set(CPLUS_EXIT_BOUNDS, NULL);
        if (strlen(err.what()) != 0)
            all_ok = 0;
        report("E1_truncation", all_ok, "S");
    }

    /* E2: repeated reset/set, long-then-short tail, clean category/code. */
    {
        error err = error();
        char msg_e2[STDLIB_ERROR_SIZE];
        int all_ok = 1;
        int i;
        memset(msg_e2, 'B', STDLIB_ERROR_SIZE - 1);
        msg_e2[STDLIB_ERROR_SIZE - 1] = '\0';

        err.set(CPLUS_EXIT_IO, "first");
        if (!err.failed_now() || err.ok() || err.code() != (int)CPLUS_EXIT_IO ||
            err.category() != STDLIB_CATEGORY_IO || strcmp(err.what(), "first") != 0)
            all_ok = 0;
        err.clear();
        if (!err.ok() || err.failed_now() || err.code() != (int)CPLUS_EXIT_OK ||
            err.category() != STDLIB_CATEGORY_OK || strcmp(err.what(), "") != 0)
            all_ok = 0;

        err.set(CPLUS_EXIT_BOUNDS, msg_e2);
        err.set(CPLUS_EXIT_OWNERSHIP, "short");
        if (strcmp(err.what(), "short") != 0 ||
            err.category() != STDLIB_CATEGORY_OWNERSHIP ||
            err.code() != (int)CPLUS_EXIT_OWNERSHIP)
            all_ok = 0;

        for (i = 0; i < 4; i++) {
            error_reset(&err);
            error_fail(&err, CPLUS_EXIT_RESOURCE, "again");
        }
        if (!err.failed_now() || err.code() != (int)CPLUS_EXIT_RESOURCE ||
            err.category() != STDLIB_CATEGORY_MEMORY || strcmp(err.what(), "again") != 0)
            all_ok = 0;
        error_reset(&err);
        if (!err.ok() || strcmp(err.what(), "") != 0)
            all_ok = 0;
        report("E2_reset_set", all_ok, "S");
    }

    /* E3: NULL error pointer on all three free helpers must be harmless. */
    {
        int all_ok = 1;
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(NULL);
        error_reset(NULL);
        error_fail(NULL, CPLUS_EXIT_BOUNDS, "null-slot");
        error_fail_cat(NULL, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, "null-cat");
        stdlib_telemetry_snapshot(&tm);
        if (tm.api_failures < 2)
            all_ok = 0;
        stdlib_telemetry_set_enabled(0);
        report("E3_null_handle", all_ok, "R");
    }

    /* E4: category mapping for every exit code + explicit override + names. */
    {
        error err = error();
        int all_ok = 1;
        g_cb_calls = 0;
        g_cb_cat = -1;
        stdlib_set_error_callback(deep_cb);

        error_fail(&err, CPLUS_EXIT_IO, "io");
        if (err.category() != STDLIB_CATEGORY_IO || g_cb_cat != STDLIB_CATEGORY_IO ||
            g_cb_code != (int)CPLUS_EXIT_IO)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_RESOURCE, "mem");
        if (err.category() != STDLIB_CATEGORY_MEMORY || g_cb_cat != STDLIB_CATEGORY_MEMORY)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_BOUNDS, "bounds");
        if (err.category() != STDLIB_CATEGORY_BOUNDS || g_cb_cat != STDLIB_CATEGORY_BOUNDS)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_OWNERSHIP, "own");
        if (err.category() != STDLIB_CATEGORY_OWNERSHIP || g_cb_cat != STDLIB_CATEGORY_OWNERSHIP)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_USE_AFTER_FREE, "uaf");
        if (err.category() != STDLIB_CATEGORY_LIFECYCLE || g_cb_cat != STDLIB_CATEGORY_LIFECYCLE)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_DOUBLE_FREE, "double");
        if (err.category() != STDLIB_CATEGORY_LIFECYCLE || g_cb_cat != STDLIB_CATEGORY_LIFECYCLE)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_NULL_POINTER, "np");
        if (err.category() != STDLIB_CATEGORY_INVALID || g_cb_cat != STDLIB_CATEGORY_INVALID)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_TYPE_CONFUSION, "tc");
        if (err.category() != STDLIB_CATEGORY_INVALID || g_cb_cat != STDLIB_CATEGORY_INVALID)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_UNSUPPORTED, "uns");
        if (err.category() != STDLIB_CATEGORY_INVALID || g_cb_cat != STDLIB_CATEGORY_INVALID)
            all_ok = 0;

        error_fail_cat(&err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, "range");
        if (err.category() != STDLIB_CATEGORY_RANGE || g_cb_cat != STDLIB_CATEGORY_RANGE ||
            err.code() != (int)CPLUS_EXIT_BOUNDS)
            all_ok = 0;
        error_fail_cat(&err, STDLIB_CATEGORY_UTF8, CPLUS_EXIT_IO, "utf8");
        if (err.category() != STDLIB_CATEGORY_UTF8 || g_cb_cat != STDLIB_CATEGORY_UTF8)
            all_ok = 0;
        error_reset(&err);
        if (err.category() != STDLIB_CATEGORY_OK)
            all_ok = 0;

        if (strcmp(stdlib_category_name(STDLIB_CATEGORY_OK), "ok") != 0 ||
            strcmp(stdlib_category_name(STDLIB_CATEGORY_INVALID), "invalid") != 0 ||
            strcmp(stdlib_category_name(STDLIB_CATEGORY_RANGE), "range") != 0 ||
            strcmp(stdlib_category_name(STDLIB_CATEGORY_IO), "io") != 0 ||
            strcmp(stdlib_category_name(STDLIB_CATEGORY_MEMORY), "memory") != 0 ||
            strcmp(stdlib_category_name(STDLIB_CATEGORY_BOUNDS), "bounds") != 0 ||
            strcmp(stdlib_category_name(STDLIB_CATEGORY_OWNERSHIP), "ownership") != 0 ||
            strcmp(stdlib_category_name(STDLIB_CATEGORY_LIFECYCLE), "lifecycle") != 0 ||
            strcmp(stdlib_category_name(STDLIB_CATEGORY_UTF8), "utf8") != 0 ||
            strcmp(stdlib_category_name(999), "unknown") != 0)
            all_ok = 0;
        if (g_cb_calls != 11)
            all_ok = 0;
        stdlib_set_error_callback(NULL);
        report("E4_categories", all_ok, "S");
    }

    /* E5: status/exit-code mapping survives category overrides and reset. */
    {
        error err = error();
        int all_ok = 1;
        err.set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_IO, "mapped");
        if (err.code() != (int)CPLUS_EXIT_IO ||
            err.category() != STDLIB_CATEGORY_RANGE ||
            err.failed_now() != 1)
            all_ok = 0;
        error_reset(&err);
        if (err.code() != (int)CPLUS_EXIT_OK || err.failed_now() != 0)
            all_ok = 0;
        error_fail(&err, CPLUS_EXIT_DOUBLE_FREE, "df");
        if (err.code() != (int)CPLUS_EXIT_DOUBLE_FREE ||
            err.category() != STDLIB_CATEGORY_LIFECYCLE)
            all_ok = 0;
        report("E5_status_map", all_ok, "S");
    }

    /* --------------------------------------------------------------- vector */

    /* V1: growth boundaries: 0 -> 4 -> 8 -> 16, exact reserve, one-over. */
    {
        vector v = vector();
        int all_ok = 1;
        int i;
        if (v.size() != 0 || v.capacity() != 0)
            all_ok = 0;
        for (i = 0; i < 4; i++) {
            if (v.push_back(i) != 0)
                all_ok = 0;
        }
        if (v.size() != 4 || v.capacity() != 4)
            all_ok = 0;
        if (v.push_back(4) != 0 || v.capacity() != 8 || v.size() != 5)
            all_ok = 0;
        for (i = 5; i < 8; i++)
            v.push_back(i);
        if (v.capacity() != 8)
            all_ok = 0;
        v.push_back(8);
        if (v.capacity() != 16 || v.size() != 9)
            all_ok = 0;

        v.reserve(16);
        if (v.capacity() != 16)
            all_ok = 0;
        v.reserve(7);
        if (v.capacity() != 16 || v.size() != 9)
            all_ok = 0;
        v.reserve(23);
        if (v.capacity() != 23)
            all_ok = 0;
        v.reserve(24);
        if (v.capacity() != 24)
            all_ok = 0;
        for (i = 0; i < 9; i++)
            if (v.at(i) != i)
                all_ok = 0;
        v.drop();
        report("V1_growth", all_ok, "S");
    }

    /* V2: empty slices and sub-slice clamping. */
    {
        vector v = vector();
        slice s;
        slice sub;
        int out = -7;
        int all_ok = 1;

        s = v.view();
        if (s.size() != 0 || s.valid() || s.stale() || s.at_checked(0, &out) == 0)
            all_ok = 0;
        sub = s.sub(0, 5);
        if (sub.size() != 0 || sub.valid())
            all_ok = 0;

        v.push_back(1);
        v.push_back(2);
        v.push_back(3);
        s = v.view();
        sub = s.sub(1, 99);
        if (sub.size() != 2 || !sub.valid() || sub.at_checked(0, &out) != 0 || out != 2 ||
            sub.at_checked(1, &out) != 0 || out != 3 || sub.at_checked(2, &out) == 0)
            all_ok = 0;
        sub = s.sub(2, 0);
        if (sub.size() != 0 || sub.valid())
            all_ok = 0;
        sub = s.sub(99, 2);
        if (sub.size() != 0 || sub.valid())
            all_ok = 0;

        v.clear();
        s = v.view();
        if (s.size() != 0 || s.valid() || s.stale())
            all_ok = 0;
        v.drop();
        report("V2_empty_slices", all_ok, "S");
    }

    /* V3: staleness after growth and after each shape-changing mutation. */
    {
        vector v = vector();
        slice s;
        slice sub;
        slice s2;
        int out = 0;
        int all_ok = 1;

        v.push_back(10);
        v.push_back(20);
        s = v.view();
        sub = s.sub(1, 1);
        v.push_back(30);
        if (s.stale() || !s.valid() || s.size() != 2 ||
            s.at_checked(1, &out) != 0 || out != 20 || s.at_checked(2, &out) == 0)
            all_ok = 0;
        v.push_back(40);
        if (s.stale() || !s.valid())
            all_ok = 0;
        v.push_back(50); /* reallocation 4 -> 8 */
        if (!s.stale() || s.valid() || s.at_checked(0, &out) == 0 ||
            !sub.stale() || sub.valid())
            all_ok = 0;

        s2 = v.view();
        v.pop_back(&out);
        if (!s2.stale() || s2.valid())
            all_ok = 0;
        s2 = v.view();
        v.insert(0, 99);
        if (!s2.stale() || s2.valid())
            all_ok = 0;
        s2 = v.view();
        v.erase(0, NULL);
        if (!s2.stale() || s2.valid())
            all_ok = 0;
        s2 = v.view();
        v.clear();
        if (!s2.stale() || s2.valid())
            all_ok = 0;

        v.push_back(7);
        v.push_back(8);
        s2 = v.view();
        {
            vector other = vector();
            other.push_back(1);
            if (v.copy_from(&other) != 0)
                all_ok = 0;
            other.drop();
        }
        if (!s2.stale() || s2.valid())
            all_ok = 0;
        s2 = v.view();
        if (v.reserve(64) != 0)
            all_ok = 0;
        if (!s2.stale() || s2.valid())
            all_ok = 0;
        s2 = v.view();
        if (v.shrink_to_fit() != 0)
            all_ok = 0;
        if (!s2.stale() || s2.valid())
            all_ok = 0;
        s2 = v.view();
        v.drop();
        if (!s2.stale() || s2.valid())
            all_ok = 0;
        report("V3_stale_slices", all_ok, "S");
    }

    /* V4: deterministic reserve failure and recovery, plus growth failure. */
    {
        vector v = vector();
        slice s;
        int out = 0;
        int all_ok = 1;
        v.push_back(10);
        v.push_back(20);
        v.push_back(30);
        s = v.view();

        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(NULL);
        stdlib_alloc_fail_after(0);
        if (v.reserve(64) != -1)
            all_ok = 0;
        stdlib_telemetry_snapshot(&tm);
        if (v.size() != 3 || v.capacity() != 4 ||
            v.at_checked(2, &out) != 0 || out != 30 ||
            s.stale() || !s.valid() || tm.alloc_failures != 1)
            all_ok = 0;
        stdlib_alloc_fail_reset();
        if (v.reserve(64) != 0 || v.capacity() != 64 || v.size() != 3 || !s.stale())
            all_ok = 0;
        stdlib_telemetry_set_enabled(0);

        {
            vector g = vector();
            slice gs;
            int i;
            for (i = 0; i < 4; i++)
                g.push_back(100 + i);
            gs = g.view();
            stdlib_alloc_fail_after(0);
            if (g.push_back(999) != -1)
                all_ok = 0;
            if (g.size() != 4 || g.capacity() != 4 ||
                g.at_checked(3, &out) != 0 || out != 103 ||
                gs.stale() || !gs.valid())
                all_ok = 0;
            stdlib_alloc_fail_reset();
            if (g.push_back(999) != 0 || g.capacity() != 8 ||
                g.at_checked(4, &out) != 0 || out != 999 || !gs.stale())
                all_ok = 0;
            g.drop();
        }
        v.drop();
        report("V4_oom_recovery", all_ok, "R");
    }

    /* V5: get/pop on empty (and after clear) return recoverable failure. */
    {
        vector v = vector();
        int out = -7;
        int all_ok = 1;
        if (v.at(0) != 0 || v.at_checked(0, &out) != -1 || out != -7 ||
            v.pop_back(&out) != -1 || out != -7 || v.pop_back(NULL) != -1)
            all_ok = 0;
        v.push_back(11);
        if (v.pop_back(&out) != 0 || out != 11 || v.size() != 0)
            all_ok = 0;
        if (v.pop_back(&out) != -1 || out != 11)
            all_ok = 0;
        v.push_back(1);
        v.clear();
        if (v.at_checked(0, &out) != -1 || v.pop_back(&out) != -1 || out != 11)
            all_ok = 0;
        v.drop();
        report("V5_empty_get_pop", all_ok, "R");
    }

    /* V6: allocation-overflow shapes are rejected without allocating. */
    {
        vector v = vector();
        void *p;
        int out = 0;
        int all_ok = 1;
        v.push_back(5);
        if (v.reserve(SIZE_MAX) != -1 || v.capacity() != 4 || v.size() != 1)
            all_ok = 0;
        if (v.reserve((STDLIB_ALLOC_LIMIT / sizeof(int)) + 1) != -1 ||
            v.capacity() != 4 || v.size() != 1)
            all_ok = 0;
        if (v.at_checked(0, &out) != 0 || out != 5)
            all_ok = 0;
        p = stdlib_calloc(SIZE_MAX, 2);
        if (p != NULL)
            all_ok = 0;
        p = stdlib_calloc((SIZE_MAX / 2) + 1, 2);
        if (p != NULL)
            all_ok = 0;
        p = stdlib_alloc(SIZE_MAX);
        if (p != NULL)
            all_ok = 0;
        v.drop();
        report("V6_overflow_shapes", all_ok, "R");
    }

    /* V7: allocator accounting - every successful block released by drop(). */
    {
        vector a = vector();
        vector b = vector();
        int all_ok = 1;
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        a.reserve(8);
        a.push_back(1);
        a.push_back(2);
        a.shrink_to_fit();
        if (b.copy_from(&a) != 0)
            all_ok = 0;
        b.reserve(32);
        a.drop();
        b.drop();
        stdlib_telemetry_snapshot(&tm);
        if (tm.alloc_count == 0 || tm.free_count != tm.alloc_count)
            all_ok = 0;
        stdlib_telemetry_set_enabled(0);
        report("V7_alloc_balance", all_ok, "R");
    }

    report("DEEP_ERROR_VECTOR", g_failures == 0, "F");
    return g_failures == 0 ? 0 : 1;
}
