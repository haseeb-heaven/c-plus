/* Task 5 Phase 3 - runtime allocation diagnostics: telemetry and statistics.
 *
 * COVERAGE
 *   Leak detection via telemetry on/off (counters are opt-in per operation),
 *   memory usage statistics (bytes_allocated, per-block size/elem/length,
 *   allocation sequence), and the diagnostic callback/error-category path.
 *
 * SAFETY INVARIANT
 *   All blocks are released through stdlib_release / stdlib_mem_release while
 *   still live.  No stale pointer is dereferenced; only metadata queries and
 *   telemetry snapshots are used for the diagnostics.
 *
 * STRICT-SAFE PATTERNS: function-unique local prefixes, __builtin_memcpy for
 * byte copies, no variable division/modulo (the library's stdlib_safe_div is
 * exercised indirectly through the length queries).
 *
 * OUTPUT: "<tag> <1|0>", final line "ALLOC_DIAG <checks> <failures>".
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static int dt_checks = 0;
static int dt_failed = 0;
static int dt_cb_calls = 0;
static int dt_cb_category = -1;

static void dt_check(const char *tag, int cond) {
    dt_checks++;
    if (!cond)
        dt_failed++;
    print(tag, cond ? 1 : 0);
}

/* Diagnostic hook: receives category/code/message, never buffer contents. */
static void dt_callback(int category, cplus_exit_code code, const char *message) {
    dt_cb_calls++;
    dt_cb_category = category;
    (void)code;
    (void)message;
}

/* ---- telemetry ON: outstanding allocations are visible -------------------- */
static void dt_telemetry_on(void) {
    void *dt_a = NULL;
    void *dt_b = NULL;
    stdlib_telemetry_stats dt_s;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    dt_a = stdlib_alloc(10);
    dt_b = stdlib_calloc(5, 4);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_on_counts",
             dt_s.alloc_count == 2 && dt_s.bytes_allocated == 30 &&
             dt_s.free_count == 0 && dt_s.alloc_failures == 0);
    dt_check("tm_on_leak_visible", dt_s.alloc_count > dt_s.free_count);

    stdlib_release(dt_a);
    stdlib_release(dt_b);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_on_balanced",
             dt_s.alloc_count == 2 && dt_s.free_count == 2 &&
             dt_s.bytes_allocated == 30);
    dt_check("tm_on_no_leak", dt_s.alloc_count == dt_s.free_count);
}

/* ---- telemetry OFF: no counters, no leak signal, no backfill -------------- */
static void dt_telemetry_off(void) {
    void *dt_x = NULL;
    void *dt_y = NULL;
    stdlib_telemetry_stats dt_s;

    stdlib_telemetry_set_enabled(0);
    stdlib_telemetry_reset();
    dt_x = stdlib_alloc(64);
    dt_y = stdlib_alloc(128);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_off_zero",
             dt_s.alloc_count == 0 && dt_s.free_count == 0 &&
             dt_s.bytes_allocated == 0 && dt_s.api_failures == 0);

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_off_not_backfilled",
             dt_s.alloc_count == 0 && dt_s.bytes_allocated == 0);

    /* Frees are counted at call time; allocation counters stay at zero. */
    stdlib_release(dt_x);
    stdlib_release(dt_y);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_off_free_counted",
             dt_s.free_count == 2 && dt_s.alloc_count == 0 &&
             dt_s.bytes_allocated == 0);
}

/* ---- memory usage statistics over a bounded mix of blocks ----------------- */
static void dt_usage_stats(void) {
    size_t dt_sizes[5];
    void *dt_blocks[5];
    size_t dt_sum = 0;
    int dt_i = 0;
    stdlib_telemetry_stats dt_s;

    dt_sizes[0] = 8;
    dt_sizes[1] = 16;
    dt_sizes[2] = 24;
    dt_sizes[3] = 32;
    dt_sizes[4] = 40;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    for (dt_i = 0; dt_i < 5; dt_i++) {
        dt_blocks[dt_i] = stdlib_alloc(dt_sizes[dt_i]);
        dt_sum += dt_sizes[dt_i];
    }
    dt_check("tm_usage_bytes",
             stdlib_alloc_seq() == 5 && dt_sum == 120);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_usage_totals",
             dt_s.alloc_count == 5 && dt_s.bytes_allocated == dt_sum);

    dt_check("tm_usage_per_block",
             stdlib_alloc_size(dt_blocks[0]) == 8 &&
             stdlib_alloc_size(dt_blocks[4]) == 40 &&
             stdlib_alloc_length(dt_blocks[4]) == 40 &&
             stdlib_alloc_elem_size(dt_blocks[4]) == 1);

    for (dt_i = 0; dt_i < 5; dt_i++)
        stdlib_release(dt_blocks[dt_i]);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_usage_balanced",
             dt_s.alloc_count == 5 && dt_s.free_count == 5);
}

/* ---- failure diagnostics: callback fires even with telemetry disabled ----- */
static void dt_failure_diagnostics(void) {
    stdlib_telemetry_stats dt_s;

    /* Telemetry OFF: counters stay silent, but the callback still reports. */
    stdlib_telemetry_set_enabled(0);
    stdlib_telemetry_reset();
    dt_cb_calls = 0;
    dt_cb_category = -1;
    stdlib_set_error_callback(dt_callback);
    dt_check("tm_cb_off_fires",
             stdlib_alloc((size_t)1 << 42) == NULL && dt_cb_calls == 1 &&
             dt_cb_category == STDLIB_CATEGORY_MEMORY);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_cb_off_no_counters",
             dt_s.alloc_failures == 0 && dt_s.api_failures == 0);
    stdlib_set_error_callback(NULL);

    /* Telemetry ON: the same failure is counted as an allocation failure and
     * an API failure, and the callback still runs exactly once. */
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    dt_cb_calls = 0;
    dt_cb_category = -1;
    stdlib_set_error_callback(dt_callback);
    dt_check("tm_cb_on_fires",
             stdlib_alloc((size_t)1 << 42) == NULL && dt_cb_calls == 1 &&
             dt_cb_category == STDLIB_CATEGORY_MEMORY);
    stdlib_telemetry_snapshot(&dt_s);
    dt_check("tm_cb_on_counters",
             dt_s.alloc_failures == 1 && dt_s.api_failures == 1 &&
             dt_s.alloc_count == 0);
    stdlib_set_error_callback(NULL);
    stdlib_telemetry_set_enabled(1);
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_set_error_callback(NULL);

    dt_telemetry_on();
    dt_telemetry_off();
    dt_usage_stats();
    dt_failure_diagnostics();

    stdlib_set_error_callback(NULL);
    stdlib_telemetry_set_enabled(1);
    dt_check("dt_terminated", 1);
    print("ALLOC_DIAG", dt_checks, dt_failed);
    return dt_failed ? 1 : 0;
}
