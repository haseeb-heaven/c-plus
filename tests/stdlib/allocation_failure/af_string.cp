/* Deterministic allocation-failure injection suite -- stdlib/string.hp.
 *
 * Covers the three documented fallible string operations:
 *   * ctor        string("abc") with fail_at(1)  -> invalid, zero-state object
 *   * expansion   reserve(n) and append(text) reallocation -> -1, unchanged
 *   * copy_from   single allocation -> -1, byte-for-byte unchanged target
 * Each failed operation must leave size/capacity/c_str pointer/c_bytes
 * unchanged, telemetry balanced after cleanup, and a reset retry succeeds.
 * No memory is exhausted: failure is a logical allocator counter.
 *
 * STRICT-SAFE PATTERNS. A stale c_str() pointer is only ever COMPARED, never
 * dereferenced; out-parameters are initialised at declaration; container
 * (string) names are unique per function; every injection is paired with a
 * stdlib_alloc_fail_reset() in the same scope.
 *
 * Output: "<tag> <1|0>" per check, then "ALLOC_FAIL <checks> <failures>".
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

static int as_checks = 0;
static int as_failures = 0;

static void as_ck(const char *tag, int cond) {
    as_checks++;
    if (!cond)
        as_failures++;
    print(tag, cond ? 1 : 0);
}

/* ---- constructor allocation failure + retry ------------------------------ */
static void as_ctor(void) {
    string as_bad;
    stdlib_telemetry_stats tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    /* Single ctor allocation fails. */
    stdlib_alloc_fail_at(1);
    as_bad = string("abc");
    stdlib_alloc_fail_reset();

    as_ck("as_ctor_valid", as_bad.valid() == 0);
    as_ck("as_ctor_size", as_bad.size() == 0);
    as_ck("as_ctor_cap", as_bad.capacity() == 0);
    as_ck("as_ctor_cstr",
          as_bad.c_str() != (const char *)0 && strcmp(as_bad.c_str(), "") == 0);
    as_ck("as_ctor_at0", as_bad.at(0) == '\0');
    stdlib_telemetry_snapshot(&tm);
    as_ck("as_ctor_balanced", tm.alloc_count == tm.free_count &&
                              tm.alloc_failures >= 1);

    /* Recovery: a fresh construction after reset succeeds. */
    as_bad = string("abc");
    as_ck("as_ctor_retry", as_bad.valid() == 1 && as_bad.size() == 3 &&
                           as_bad.capacity() == 4 &&
                           strcmp(as_bad.c_str(), "abc") == 0);
    as_bad.drop();
    as_bad.drop(); /* repeated cleanup: idempotent */
    stdlib_telemetry_snapshot(&tm);
    as_ck("as_ctor_cleaned", tm.alloc_count == tm.free_count &&
                             tm.alloc_count > 0);
    stdlib_telemetry_set_enabled(0);
}

/* ---- reserve (explicit expansion) failure + retry ------------------------ */
static void as_reserve(void) {
    string as_rsv;
    const char *as_before;
    stdlib_telemetry_stats tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    as_rsv = string("abcd"); /* size 4, capacity 5 */
    as_before = as_rsv.c_str();

    stdlib_alloc_fail_at(1);
    as_ck("as_reserve_fail", as_rsv.reserve(64) == -1);
    stdlib_alloc_fail_reset();
    as_ck("as_reserve_state", as_rsv.size() == 4 && as_rsv.capacity() == 5 &&
                             as_rsv.valid() == 1);
    as_ck("as_reserve_contents", as_rsv.c_str() == as_before &&
                                strcmp(as_rsv.c_str(), "abcd") == 0);

    /* Retry succeeds with the requested capacity. */
    as_ck("as_reserve_retry", as_rsv.reserve(64) == 0 &&
                              as_rsv.capacity() == 64 && as_rsv.size() == 4 &&
                              strcmp(as_rsv.c_str(), "abcd") == 0);
    as_rsv.drop();
    stdlib_telemetry_snapshot(&tm);
    as_ck("as_reserve_cleaned", tm.alloc_count == tm.free_count &&
                                tm.alloc_failures >= 1);
    stdlib_telemetry_set_enabled(0);
}

/* ---- append (implicit expansion) failure + no-realloc path + retry -------- */
static void as_append(void) {
    string as_app;
    const char *as_before;
    stdlib_telemetry_stats tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    as_app = string("abcd"); /* size 4, capacity 5 */
    as_before = as_app.c_str();

    /* append needs 4+4+1 = 9 > 5, so it reserves (double to 10): one alloc. */
    stdlib_alloc_fail_at(1);
    as_ck("as_append_fail", as_app.append("EFGH") == -1);
    stdlib_alloc_fail_reset();
    as_ck("as_append_state", as_app.size() == 4 && as_app.capacity() == 5 &&
                            as_app.valid() == 1);
    as_ck("as_append_contents", as_app.c_str() == as_before &&
                               strcmp(as_app.c_str(), "abcd") == 0);

    /* Retry succeeds and appends the exact bytes. */
    as_ck("as_append_retry", as_app.append("EFGH") == 0 &&
                             as_app.size() == 8 && as_app.capacity() == 10 &&
                             strcmp(as_app.c_str(), "abcdEFGH") == 0);

    /* An append that needs no growth must not allocate (injection is inert). */
    stdlib_alloc_fail_reset();
    as_ck("as_append_noalloc_pre", as_app.reserve(64) == 0);
    stdlib_alloc_fail_at(1);
    as_ck("as_append_noalloc", as_app.append("IJ") == 0 &&
                               as_app.size() == 10 &&
                               strcmp(as_app.c_str(), "abcdEFGHIJ") == 0);
    stdlib_alloc_fail_reset(); /* clear the still-pending one-shot injection */

    as_app.drop();
    stdlib_telemetry_snapshot(&tm);
    as_ck("as_append_cleaned", tm.alloc_count == tm.free_count &&
                               tm.alloc_failures >= 1);
    stdlib_telemetry_set_enabled(0);
}

/* ---- copy_from failure + retry ------------------------------------------- */
static void as_copy_from(void) {
    string as_dst;
    string as_src;
    const char *as_before;
    stdlib_telemetry_stats tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    as_dst = string("original"); /* size 8, capacity 9 */
    as_src = string("s");        /* size 1, capacity 2 */
    as_before = as_dst.c_str();

    stdlib_alloc_fail_at(1);
    as_ck("as_copy_fail", as_dst.copy_from(&as_src) == -1);
    stdlib_alloc_fail_reset();
    as_ck("as_copy_state", as_dst.size() == 8 && as_dst.capacity() == 9 &&
                           as_dst.valid() == 1);
    as_ck("as_copy_contents", as_dst.c_str() == as_before &&
                             strcmp(as_dst.c_str(), "original") == 0);
    /* At the failure point the two original buffers are still live: the failed
     * copy performed no allocation, but the injected failure was counted.
     * Full balance is asserted after drop() in as_copy_cleaned below. */
    stdlib_telemetry_snapshot(&tm);
    as_ck("as_copy_fail_counted", tm.alloc_count == 2ULL &&
                                  tm.free_count == 0ULL &&
                                  tm.alloc_failures >= 1ULL);

    as_ck("as_copy_retry", as_dst.copy_from(&as_src) == 0 &&
                           as_dst.size() == 1 && as_dst.capacity() == 2 &&
                           strcmp(as_dst.c_str(), "s") == 0);
    as_dst.drop();
    as_src.drop();
    stdlib_telemetry_snapshot(&tm);
    as_ck("as_copy_cleaned", tm.alloc_count == tm.free_count &&
                             tm.alloc_count > 0);
    stdlib_telemetry_set_enabled(0);
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_alloc_fail_reset();

    as_ctor();
    as_reserve();
    as_append();
    as_copy_from();

    stdlib_alloc_fail_reset();
    print("ALLOC_FAIL", as_checks, as_failures);
    stdlib_telemetry_set_enabled(0);
    return as_failures ? 1 : 0;
}
