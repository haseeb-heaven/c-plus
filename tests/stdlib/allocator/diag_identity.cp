/* Task 5 Phase 3 - runtime allocation diagnostics: identity, size, realloc.
 *
 * SCOPE
 *   Allocation identity beyond the pointer address (generation + metadata),
 *   size queries, state/liveness of live blocks, and reallocation history
 *   observed through generation tokens.  Allocation-site tracking is not
 *   available in the host metadata and is reported as an explicit SKIP.
 *
 * SAFETY INVARIANT
 *   This test NEVER dereferences a stale pointer and NEVER calls
 *   cplus_release / stdlib_release through an address captured before a
 *   reallocation.  After an invalidating operation it only calls metadata
 *   observers (stdlib_alloc_size / _elem_size / _length / _generation /
 *   _check_generation / stdlib_mem_check / stdlib_mem_count).  Those query a
 *   checked side table and never touch the user bytes.  Guarded writes use
 *   stdlib_mem_set with a CURRENT token; a stale token is only passed to a
 *   guarded call that refuses it before any write.
 *
 * STRICT-SAFE PATTERNS
 *   Every local has a function-unique prefix (C+ lowering resolves names
 *   across functions); no variable division or modulo is written here
 *   (stdlib_alloc_length / stdlib_alloc_query_length use stdlib_safe_div);
 *   byte copies, if any, use __builtin_memcpy.
 *
 * OUTPUT
 *   "<tag> <1|0>" per check, optional "SKIP <tag> <reason>", final line
 *   "ALLOC_DIAG <checks> <failures>".  Exit 1 iff failures > 0.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static int di_checks = 0;
static int di_failed = 0;

static void di_check(const char *tag, int cond) {
    di_checks++;
    if (!cond)
        di_failed++;
    print(tag, cond ? 1 : 0);
}

/* ---- size queries, elem size, element count, and live state --------------- */
static void di_sizes_and_state(void) {
    void *di_a = stdlib_alloc(7);
    void *di_b = stdlib_alloc(7);
    void *di_c = stdlib_calloc(8, 4);
    unsigned long long di_ga, di_gb, di_gc;
    size_t di_n = 0;
    error di_err;

    di_ga = stdlib_alloc_generation(di_a);
    di_gb = stdlib_alloc_generation(di_b);
    di_gc = stdlib_alloc_generation(di_c);

    di_check("id_alloc_size",
             di_a != NULL && di_b != NULL && di_c != NULL &&
             stdlib_alloc_size(di_a) == 7 && stdlib_alloc_size(di_b) == 7 &&
             stdlib_alloc_size(di_c) == 32);

    /* Same byte size for a and b, yet distinct, increasing generations:
     * identity is the (base, generation) pair, not the size or the address. */
    di_check("id_identity_generation",
             di_ga > 0 && di_gb > di_ga && di_gc > di_gb &&
             stdlib_alloc_check_generation(di_a, di_ga) == 1 &&
             stdlib_alloc_check_generation(di_a, di_gb) == 0 &&
             stdlib_alloc_check_generation(di_b, di_gb) == 1);

    di_check("id_calloc_layout",
             stdlib_alloc_elem_size(di_c) == 4 &&
             stdlib_alloc_length(di_c) == 8 &&
             stdlib_alloc_elem_size(di_a) == 1 &&
             stdlib_alloc_length(di_a) == 7);

    di_check("id_query_explicit",
             stdlib_alloc_query_length(di_c, 4, &di_n) == 0 && di_n == 8);
    di_n = 0;
    di_check("id_query_default",
             stdlib_alloc_query_length(di_c, 0, &di_n) == 0 && di_n == 8);
    di_n = 0;
    di_check("id_query_wider",
             stdlib_alloc_query_length(di_c, 8, &di_n) == 0 && di_n == 4);
    di_n = 1;
    di_check("id_query_null",
             stdlib_alloc_query_length(NULL, 4, &di_n) == -1 && di_n == 0 &&
             stdlib_alloc_size(NULL) == 0 &&
             stdlib_alloc_generation(NULL) == 0 &&
             stdlib_alloc_elem_size(NULL) == 0);

    di_n = 0;
    di_check("id_mem_wrappers",
             stdlib_mem_capacity(di_c) == 32 &&
             stdlib_mem_count(di_c, 0, &di_n) == 0 && di_n == 8);

    error_reset(&di_err);
    di_check("id_live_check",
             stdlib_mem_check(di_a, di_ga, &di_err) == STDLIB_OK &&
             di_err.ok() == 1 && di_err.category() == STDLIB_CATEGORY_OK);

    stdlib_release(di_a);
    stdlib_release(di_b);
    stdlib_release(di_c);
}

/* ---- generations are unique across many same-sized allocations ----------- */
static void di_generation_uniqueness(void) {
    void *di_ps[6];
    unsigned long long di_gs[6];
    unsigned long long di_prev = 0;
    int di_i = 0;
    int di_j = 0;
    int di_ok = 1;

    for (di_i = 0; di_i < 6; di_i++) {
        di_ps[di_i] = stdlib_alloc(16);
        di_gs[di_i] = stdlib_alloc_generation(di_ps[di_i]);
        if (di_ps[di_i] == NULL || di_gs[di_i] == 0)
            di_ok = 0;
        if (di_i > 0 && di_gs[di_i] <= di_prev)
            di_ok = 0;
        di_prev = di_gs[di_i];
    }
    for (di_i = 0; di_i < 6; di_i++) {
        for (di_j = di_i + 1; di_j < 6; di_j++) {
            if (di_gs[di_i] == di_gs[di_j])
                di_ok = 0;
        }
    }
    di_check("id_gen_monotonic", di_ok == 1);

    for (di_i = 0; di_i < 6; di_i++)
        stdlib_release(di_ps[di_i]);
}

/* ---- reallocation history: every successful resize retires the old token -- */
static void di_realloc_history(void) {
    void *di_r1 = stdlib_alloc(16);
    void *di_r2 = NULL;
    void *di_r3 = NULL;
    unsigned long long di_g0 = stdlib_alloc_generation(di_r1);
    unsigned long long di_g1 = 0;
    unsigned long long di_g2 = 0;
    stdlib_telemetry_stats di_tm_start;
    stdlib_telemetry_stats di_tm0;
    stdlib_telemetry_stats di_tm1;
    error di_err;

    stdlib_telemetry_snapshot(&di_tm_start);
    di_r2 = stdlib_realloc(di_r1, 32);
    di_g1 = stdlib_alloc_generation(di_r2);
    di_check("id_realloc_new_identity",
             di_r2 != NULL && stdlib_alloc_size(di_r2) == 32 &&
             di_g1 > di_g0 &&
             stdlib_alloc_check_generation(di_r1, di_g0) == 0 &&
             stdlib_alloc_check_generation(di_r2, di_g0) == 0 &&
             stdlib_alloc_check_generation(di_r2, di_g1) == 1);

    /* A stale token is refused BEFORE the write; the current token succeeds.
     * No pointer captured before the resize is read or released here. */
    error_reset(&di_err);
    di_check("id_realloc_stale_refused",
             stdlib_mem_set(di_r2, 0, 0x5a, 4, di_g0, &di_err) == STDLIB_FAIL &&
             di_err.category() == STDLIB_CATEGORY_LIFECYCLE);
    error_reset(&di_err);
    di_check("id_realloc_fresh_ok",
             stdlib_mem_set(di_r2, 0, 0x5a, 4, di_g1, &di_err) == STDLIB_OK &&
             di_err.ok() == 1);

    di_r3 = stdlib_realloc(di_r2, 64);
    di_g2 = stdlib_alloc_generation(di_r3);
    di_check("id_realloc_chain",
             di_r3 != NULL && stdlib_alloc_size(di_r3) == 64 && di_g2 > di_g1 &&
             stdlib_alloc_check_generation(di_r2, di_g1) == 0 &&
             stdlib_alloc_check_generation(di_r3, di_g2) == 1);

    /* A refused resize is failure-atomic: size AND generation stay unchanged. */
    stdlib_telemetry_snapshot(&di_tm0);
    di_check("id_realloc_oversize_refused",
             stdlib_realloc(di_r3, (size_t)1 << 42) == NULL &&
             stdlib_alloc_size(di_r3) == 64 &&
             stdlib_alloc_generation(di_r3) == di_g2);
    stdlib_telemetry_snapshot(&di_tm1);
    di_check("id_realloc_growth_counter",
             di_tm1.container_growths == di_tm_start.container_growths + 2);
    di_check("id_realloc_no_growth_on_refusal",
             di_tm1.container_growths == di_tm0.container_growths &&
             di_tm1.free_count == di_tm0.free_count);

    stdlib_release(di_r3);
}

/* ---- allocation-site tracking is not present in host metadata ------------ */
static void di_alloc_site_skip(void) {
    /* stdlib_meta_slot (stdlib/error.hp) stores only ptr, byte_size, elem_size
     * and generation.  There is no file/line/function/site field, and no API
     * in memory.hp or error.hp queries one.  Nothing to assert -> SKIP. */
    print("SKIP id_alloc_site no_host_site_metadata");
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();

    di_sizes_and_state();
    di_generation_uniqueness();
    di_realloc_history();
    di_alloc_site_skip();

    stdlib_alloc_fail_reset();
    di_check("id_terminated", 1);
    print("ALLOC_DIAG", di_checks, di_failed);
    return di_failed ? 1 : 0;
}
