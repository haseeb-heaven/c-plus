/* Task 5 Phase 3 - runtime allocation diagnostics: liveness and refusal.
 *
 * COVERAGE
 *   State/liveness (live vs retired metadata), invalid-free refusal,
 *   double-free refusal, and ownership-transfer invalidation.
 *
 * SAFETY INVARIANT
 *   A genuine second cplus_release / stdlib_release on the same raw address is
 *   fatal by design (CPLUS_EXIT_DOUBLE_FREE) and is deliberately NOT invoked.
 *   Instead this suite exercises the refusal paths that fail BEFORE any
 *   deallocation: stdlib_mem_check on untracked/freed pointers, stdlib_realloc
 *   on an untracked pointer, stdlib_mem_release on a NULLed handle (idempotent
 *   cleanup), and stdlib_mem_transfer / stdlib_transfer_ownership handle checks.
 *   After a release, only metadata observers touch the old address; no stale
 *   pointer is ever dereferenced or passed to cplus_release.
 *
 * STRICT-SAFE PATTERNS: function-unique local prefixes, __builtin_memcpy for
 * byte copies, no variable division/modulo.
 *
 * OUTPUT: "<tag> <1|0>", final line "ALLOC_DIAG <checks> <failures>".
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

/* Deliberately untracked storage: never handed to a raw cplus_release. */
static char dl_untracked_storage[16];

static int dl_checks = 0;
static int dl_failed = 0;

static void dl_check(const char *tag, int cond) {
    dl_checks++;
    if (!cond)
        dl_failed++;
    print(tag, cond ? 1 : 0);
}

/* ---- live -> retired metadata transition ---------------------------------- */
static void dl_live_to_dead(void) {
    void *dl_p = stdlib_alloc(24);
    void *dl_old = dl_p;
    unsigned long long dl_g = stdlib_alloc_generation(dl_p);
    error dl_err;

    error_reset(&dl_err);
    dl_check("lv_live_state",
             dl_p != NULL && dl_g > 0 && stdlib_alloc_size(dl_p) == 24 &&
             stdlib_alloc_length(dl_p) == 24 &&
             stdlib_alloc_check_generation(dl_p, dl_g) == 1 &&
             stdlib_mem_check(dl_p, dl_g, &dl_err) == STDLIB_OK &&
             dl_err.ok() == 1);

    stdlib_release(dl_p); /* dl_p is now stale; dl_old keeps the value only */

    error_reset(&dl_err);
    dl_check("lv_dead_state",
             stdlib_alloc_size(dl_old) == 0 &&
             stdlib_alloc_length(dl_old) == 0 &&
             stdlib_alloc_elem_size(dl_old) == 0 &&
             stdlib_alloc_generation(dl_old) == 0 &&
             stdlib_alloc_check_generation(dl_old, dl_g) == 0 &&
             stdlib_mem_check(dl_old, dl_g, &dl_err) == STDLIB_FAIL &&
             dl_err.category() == STDLIB_CATEGORY_INVALID &&
             dl_err.failed_now() == 1);
}

/* ---- an address with no metadata is refused, never freed ------------------ */
static void dl_untracked_refusal(void) {
    void *dl_u = (void *)dl_untracked_storage;
    error dl_err;
    stdlib_telemetry_stats dl_tm0;
    stdlib_telemetry_stats dl_tm1;

    stdlib_telemetry_reset();
    error_reset(&dl_err);
    dl_check("lv_untracked_metadata",
             stdlib_alloc_size(dl_u) == 0 && stdlib_alloc_generation(dl_u) == 0 &&
             stdlib_mem_check(dl_u, 0, &dl_err) == STDLIB_FAIL &&
             dl_err.category() == STDLIB_CATEGORY_INVALID);

    /* stdlib_realloc checks the side table first: it refuses an untracked
     * pointer with a diagnostic and performs NO host free / reallocation. */
    stdlib_telemetry_snapshot(&dl_tm0);
    dl_check("lv_untracked_realloc_refused", stdlib_realloc(dl_u, 8) == NULL);
    stdlib_telemetry_snapshot(&dl_tm1);
    dl_check("lv_untracked_realloc_diagnosed",
             dl_tm1.invalid_ops == dl_tm0.invalid_ops + 1 &&
             dl_tm1.api_failures == dl_tm0.api_failures + 1 &&
             dl_tm1.free_count == dl_tm0.free_count &&
             stdlib_alloc_size(dl_u) == 0);
}

/* ---- idempotent cleanup: a second release cannot free twice --------------- */
static void dl_double_free_refusal(void) {
    void *dl_h = stdlib_alloc(12);
    void *dl_h_old = dl_h;
    unsigned long long dl_hg = stdlib_alloc_generation(dl_h);
    stdlib_telemetry_stats dl_tm0;
    stdlib_telemetry_stats dl_tm1;
    stdlib_telemetry_stats dl_tm2;

    stdlib_telemetry_snapshot(&dl_tm0);
    stdlib_mem_release(&dl_h);
    stdlib_telemetry_snapshot(&dl_tm1);
    dl_check("lv_mem_release_nulls",
             dl_h == NULL && stdlib_alloc_size(dl_h_old) == 0 &&
             stdlib_alloc_generation(dl_h_old) == 0 &&
             stdlib_alloc_check_generation(dl_h_old, dl_hg) == 0 &&
             dl_tm1.free_count == dl_tm0.free_count + 1);

    /* The NULLed handle makes repeated cleanup a true no-op. */
    stdlib_mem_release(&dl_h);
    stdlib_mem_release(&dl_h);
    stdlib_telemetry_snapshot(&dl_tm2);
    dl_check("lv_double_release_noop",
             dl_h == NULL && dl_tm2.free_count == dl_tm1.free_count &&
             dl_tm2.alloc_count == dl_tm1.alloc_count);

    /* Releasing a NULL handle directly is also explicitly a no-op. */
    stdlib_release(NULL);
    stdlib_telemetry_snapshot(&dl_tm2);
    dl_check("lv_null_release_noop", dl_tm2.free_count == dl_tm1.free_count);
}

/* ---- transfer invalidates the source token and refuses bad handles -------- */
static void dl_ownership_transfer(void) {
    void *dl_src = stdlib_alloc(40);
    void *dl_dst = NULL;
    void *dl_bad_src = (void *)dl_untracked_storage;
    unsigned long long dl_g = stdlib_alloc_generation(dl_src);
    error dl_err;

    error_reset(&dl_err);
    dl_check("lv_transfer_ok",
             stdlib_mem_transfer(&dl_dst, &dl_src, &dl_err) == STDLIB_OK &&
             dl_src == NULL && dl_dst != NULL && dl_err.ok() == 1 &&
             stdlib_alloc_size(dl_dst) == 40 &&
             stdlib_alloc_check_generation(dl_dst, dl_g) == 0 &&
             stdlib_alloc_generation(dl_dst) > dl_g);

    /* The pre-transfer token no longer authorizes a guarded write. */
    error_reset(&dl_err);
    dl_check("lv_transfer_stale_token",
             stdlib_mem_set(dl_dst, 0, 0x11, 4, dl_g, &dl_err) == STDLIB_FAIL &&
             dl_err.category() == STDLIB_CATEGORY_LIFECYCLE);

    /* Bad handles are refused by both the guarded wrapper and the raw helper. */
    error_reset(&dl_err);
    dl_check("lv_transfer_null_handles",
             stdlib_mem_transfer(&dl_dst, &dl_src, &dl_err) == STDLIB_FAIL &&
             dl_err.category() == STDLIB_CATEGORY_OWNERSHIP &&
             dl_dst != NULL && dl_src == NULL);

    {
        void *dl_d2 = NULL;
        void *dl_s2 = dl_bad_src;
        error_reset(&dl_err);
        dl_check("lv_transfer_untracked",
                 stdlib_transfer_ownership(&dl_d2, &dl_s2) == -1 &&
                 dl_d2 == NULL && dl_s2 == dl_bad_src);
    }
    {
        void *dl_self = stdlib_alloc(8);
        error_reset(&dl_err);
        dl_check("lv_transfer_self",
                 stdlib_mem_transfer(&dl_self, &dl_self, &dl_err) == STDLIB_FAIL &&
                 dl_self != NULL &&
                 dl_err.category() == STDLIB_CATEGORY_OWNERSHIP);
        stdlib_mem_release(&dl_self);
    }

    stdlib_mem_release(&dl_dst);
    stdlib_mem_release(&dl_src); /* NULL after successful transfer: no-op */
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_set_error_callback(NULL);

    dl_live_to_dead();
    dl_untracked_refusal();
    dl_double_free_refusal();
    dl_ownership_transfer();

    dl_check("dl_terminated", 1);
    print("ALLOC_DIAG", dl_checks, dl_failed);
    return dl_failed ? 1 : 0;
}
