/* memory_ownership regression suite -- ownership, lifetime, logging parity.
 *
 * Deterministic and self-checking: fixed operation order, bounded scenario,
 * no stdin and no raw dereference of a released/stale address.  Double-free,
 * invalid-free and use-after-free are observed ONLY through metadata queries
 * (stdlib_alloc_size/generation/check_generation/query_length and the
 * non-fatal ownership-transfer refusal).  A literal second stdlib_release()
 * or cplus_check_live() on a stale pointer would be fatal by design and is
 * deliberately NOT executed: see NOTES.md for the detection-limit statement.
 *
 * Every check prints "<tag> <0|1>"; the final line is
 * "MEMORY_OWNERSHIP <checks> <failures>".  Exit non-zero iff failures != 0.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static int mol_checks = 0;
static int mol_failures = 0;
static int mol_callback_calls = 0;

static void mol_check(const char *tag, int cond) {
    mol_checks++;
    if (!cond)
        mol_failures++;
    print(tag, cond ? 1 : 0);
}

static void mol_callback(int category, cplus_exit_code code,
                         const char *message) {
    (void)category;
    (void)code;
    (void)message;
    mol_callback_calls++;
}

/* Fixed, bounded ownership scenario.  Records (rc, failed, category, code)
 * into a caller-provided vector of exactly n slots.  Logging can be toggled
 * without changing the API results.  The interior address is queried, never
 * dereferenced; a stale generation token is refused through the error object. */
static void mol_parity_scenario(int enable, long *out, int n) {
    error e = error();
    void *a = (void *)0;
    void *b = (void *)0;
    void *dst = (void *)0;
    unsigned long long tok;
    int k = 0;
    int rc;

    if (enable) {
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(mol_callback);
    } else {
        stdlib_telemetry_set_enabled(0);
        stdlib_set_error_callback((stdlib_error_callback)0);
    }

    a = stdlib_alloc(16u);
    b = stdlib_alloc(8u);
    if (a == (void *)0 || b == (void *)0) {
        while (k < n)
            out[k++] = -999L;
        if (a != (void *)0)
            stdlib_release(a);
        if (b != (void *)0)
            stdlib_release(b);
        return;
    }
    tok = stdlib_mem_token(a);

    rc = stdlib_transfer_ownership(&dst, &a);
    out[k++] = (long)rc;               /* transfer is non-fatal */
    out[k++] = (long)e.failed_now();   /* error object untouched */

    {
        void *nil = (void *)0;
        void *d2 = (void *)0;
        error_reset(&e);
        rc = stdlib_transfer_ownership(&d2, &nil);
        out[k++] = (long)rc;
        out[k++] = (long)e.failed_now();
    }

    error_reset(&e);
    rc = stdlib_mem_set(dst, 0u, 0x44, 16u, tok, &e); /* stale token */
    out[k++] = (long)rc;
    out[k++] = (long)e.failed_now();
    out[k++] = (long)e.category();
    out[k++] = (long)e.code();

    error_reset(&e);
    rc = stdlib_mem_set(dst, 0u, 0x55, 16u, stdlib_mem_token(dst), &e);
    out[k++] = (long)rc;
    out[k++] = (long)e.failed_now();

    error_reset(&e);
    rc = stdlib_mem_set((void *)((unsigned char *)b + 4), 0u, 0x11, 1u,
                        0ULL, &e);
    out[k++] = (long)rc;
    out[k++] = (long)e.category();

    while (k < n)
        out[k++] = 0L;
    stdlib_release(dst);
    stdlib_release(b);
}

int main(void) {
    stdlib_telemetry_stats pre;
    stdlib_telemetry_stats post;
    size_t base_live;
    size_t cnt;
    unsigned long long g0;
    unsigned long long g1;
    void *p;
    void *alias;
    void *dst;
    void *src;
    error e;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    base_live = stdlib_state.meta_live;

    /* ---- A: double-free / UAF refusal via metadata queries ONLY -------- */
    stdlib_telemetry_snapshot(&pre);
    p = stdlib_alloc(48u);
    mol_check("mol_df_alloc", p != (void *)0);
    if (p != (void *)0) {
        g0 = stdlib_alloc_generation(p);
        mol_check("mol_df_gen_nonzero", g0 != 0ULL);
        alias = p; /* saved handle: queried only, never dereferenced again */
        stdlib_release(p);

        /* Detection limit: these queries refuse the stale identity safely.
         * We never call stdlib_release(alias) a second time and never
         * dereference alias; doing either is a fatal runtime path. */
        mol_check("mol_uaf_limit_gen_zero",
                  stdlib_alloc_generation(alias) == 0ULL);
        mol_check("mol_uaf_limit_size_zero",
                  stdlib_alloc_size(alias) == 0u);
        mol_check("mol_uaf_limit_check_reject",
                  stdlib_alloc_check_generation(alias, g0) == 0);
        cnt = 99u;
        mol_check("mol_uaf_limit_query_reject",
                  stdlib_alloc_query_length(alias, 1u, &cnt) == -1 &&
                      cnt == 0u);
        mol_check("mol_uaf_limit_refusal_only",
                  stdlib_alloc_generation(alias) == 0ULL &&
                      stdlib_alloc_check_generation(alias, g0) == 0 &&
                      stdlib_alloc_query_length(alias, 1u, &cnt) == -1);
        dst = (void *)0;
        src = alias;
        mol_check("mol_df_second_release_refused",
                  stdlib_transfer_ownership(&dst, &src) == -1 &&
                      src == alias && dst == (void *)0);
        mol_check("mol_df_live_restored", stdlib_state.meta_live == base_live);
    }
    stdlib_telemetry_snapshot(&post);
    mol_check("mol_df_fail_counted",
              post.ownership_violations - pre.ownership_violations == 1ULL &&
                  post.api_failures - pre.api_failures == 1ULL &&
                  post.free_count - pre.free_count == 1ULL);
    mol_check("mol_df_balance", post.alloc_count - pre.alloc_count == 1ULL &&
                                   post.free_count - pre.free_count == 1ULL);

    /* ---- B: untracked stack address and interior pointer refusal ------- */
    {
        int anchor = 0x5A5A;
        void *untracked = (void *)&anchor;
        dst = (void *)0;
        src = untracked;
        mol_check("mol_inv_untracked_size_zero",
                  stdlib_alloc_size(untracked) == 0u);
        mol_check("mol_inv_untracked_gen_zero",
                  stdlib_alloc_generation(untracked) == 0ULL);
        cnt = 88u;
        mol_check("mol_inv_untracked_query",
                  stdlib_alloc_query_length(untracked, 1u, &cnt) == -1 &&
                      cnt == 0u);
        mol_check("mol_inv_untracked_transfer_refused",
                  stdlib_transfer_ownership(&dst, &src) == -1 &&
                      src == untracked && dst == (void *)0);
        mol_check("mol_inv_anchor_intact", anchor == 0x5A5A);
    }

    stdlib_telemetry_snapshot(&pre);
    e = error();
    p = stdlib_alloc(64u);
    mol_check("mol_interior_alloc", p != (void *)0);
    if (p != (void *)0) {
        g0 = stdlib_mem_token(p);
        alias = (void *)((unsigned char *)p + 16); /* interior, not an owner */
        mol_check("mol_interior_not_owner_size", stdlib_alloc_size(alias) == 0u);
        mol_check("mol_interior_not_owner_gen",
                  stdlib_alloc_generation(alias) == 0ULL);
        dst = (void *)0;
        src = alias;
        mol_check("mol_interior_transfer_refused",
                  stdlib_transfer_ownership(&dst, &src) == -1 &&
                      src == alias && dst == (void *)0);
        /* A guarded write through (base, offset) is valid and stays in range. */
        error_reset(&e);
        mol_check("mol_interior_guarded_write",
                  stdlib_mem_set(p, 16u, 0x77, 8u, g0, &e) == STDLIB_OK &&
                      !e.failed_now());
        mol_check("mol_interior_bytes",
                  ((unsigned char *)p)[15] == 0 &&
                      ((unsigned char *)p)[16] == 0x77 &&
                      ((unsigned char *)p)[23] == 0x77 &&
                      ((unsigned char *)p)[24] == 0);
        /* The interior address itself carries no metadata, so it is refused
         * without being dereferenced. */
        error_reset(&e);
        mol_check("mol_interior_direct_refused",
                  stdlib_mem_set(alias, 0u, 0x11, 1u, 0ULL, &e) == STDLIB_FAIL &&
                      e.category() == STDLIB_CATEGORY_INVALID &&
                      ((unsigned char *)p)[16] == 0x77);
        stdlib_release(p);
        mol_check("mol_interior_cleanup", stdlib_state.meta_live == base_live);
    }
    stdlib_telemetry_snapshot(&post);
    mol_check("mol_interior_fail_counted",
              post.invalid_ops - pre.invalid_ops >= 1ULL &&
                  post.ownership_violations - pre.ownership_violations >= 1ULL);
    mol_check("mol_interior_balance",
              post.alloc_count - pre.alloc_count == 1ULL &&
                  post.free_count - pre.free_count == 1ULL);

    /* ---- C: ownership transfer bumps generation; stale token refused --- */
    stdlib_telemetry_snapshot(&pre);
    p = stdlib_alloc(16u);
    mol_check("mol_xfer_alloc", p != (void *)0);
    if (p != (void *)0) {
        g0 = stdlib_alloc_generation(p);
        dst = (void *)0;
        mol_check("mol_xfer_ok",
                  stdlib_transfer_ownership(&dst, &p) == 0 &&
                      p == (void *)0 && dst != (void *)0);
        g1 = stdlib_alloc_generation(dst);
        mol_check("mol_xfer_gen_bumped", g1 != 0ULL && g1 != g0);
        mol_check("mol_xfer_old_token_stale",
                  stdlib_alloc_check_generation(dst, g0) == 0);
        mol_check("mol_xfer_new_token_ok",
                  stdlib_alloc_check_generation(dst, g1) == 1);
        e = error();
        mol_check("mol_xfer_stale_guarded_write",
                  stdlib_mem_set(dst, 0u, 0x33, 16u, g0, &e) == STDLIB_FAIL &&
                      e.category() == STDLIB_CATEGORY_LIFECYCLE &&
                      e.code() == (int)CPLUS_EXIT_USE_AFTER_FREE);
        error_reset(&e);
        mol_check("mol_xfer_fresh_guarded_write",
                  stdlib_mem_set(dst, 0u, 0x44, 16u, g1, &e) == STDLIB_OK &&
                      !e.failed_now());

        /* Refusal handles: NULL source, occupied destination, dst == src. */
        {
            void *nil = (void *)0;
            void *d2 = (void *)0;
            mol_check("mol_xfer_null_src_refused",
                      stdlib_transfer_ownership(&d2, &nil) == -1 &&
                          nil == (void *)0 && d2 == (void *)0);
            src = stdlib_alloc(8u);
            mol_check("mol_xfer_aux_alloc", src != (void *)0);
            if (src != (void *)0) {
                void *occupied = dst;
                mol_check("mol_xfer_occupied_dst_refused",
                          stdlib_transfer_ownership(&occupied, &src) == -1 &&
                              occupied == dst && src != (void *)0);
                mol_check("mol_xfer_same_handle_refused",
                          stdlib_transfer_ownership(&src, &src) == -1);
                stdlib_release(src);
            }
        }
        stdlib_release(dst);
        mol_check("mol_xfer_cleanup", stdlib_state.meta_live == base_live);
    }
    stdlib_telemetry_snapshot(&post);
    mol_check("mol_xfer_balance", post.alloc_count - pre.alloc_count == 2ULL &&
                                      post.free_count - pre.free_count == 2ULL);
    mol_check("mol_xfer_fail_counted",
              post.ownership_violations - pre.ownership_violations >= 3ULL);

    /* ---- D: logging / callback parity on a fixed ownership scenario ---- */
    {
        long off[16];
        long on[16];
        int i;
        int same = 1;
        stdlib_telemetry_stats tm;

        for (i = 0; i < 16; ++i) {
            off[i] = 0L;
            on[i] = 0L;
        }

        mol_callback_calls = 0;
        stdlib_telemetry_reset();
        mol_parity_scenario(0, off, 16);
        mol_check("mol_parity_off_silent", mol_callback_calls == 0);
        stdlib_telemetry_snapshot(&tm);
        mol_check("mol_parity_off_quiet",
                  tm.api_failures == 0ULL && tm.ownership_violations == 0ULL &&
                      tm.lifecycle_failures == 0ULL);

        mol_callback_calls = 0;
        stdlib_telemetry_reset();
        stdlib_telemetry_snapshot(&pre);
        mol_parity_scenario(1, on, 16);
        stdlib_telemetry_snapshot(&post);
        mol_check("mol_parity_on_fires", mol_callback_calls > 0);
        mol_check("mol_parity_on_balance",
                  post.alloc_count - pre.alloc_count == 2ULL &&
                      post.free_count - pre.free_count == 2ULL);
        stdlib_telemetry_snapshot(&tm);
        mol_check("mol_parity_on_counts",
                  tm.api_failures > 0ULL && tm.ownership_violations > 0ULL &&
                      tm.lifecycle_failures > 0ULL);

        for (i = 0; i < 16; ++i) {
            if (off[i] != on[i])
                same = 0;
        }
        mol_check("mol_parity_results_identical", same);

        /* Restore process defaults for any other code sharing the process. */
        stdlib_set_error_callback((stdlib_error_callback)0);
        stdlib_telemetry_set_enabled(1);
        mol_check("mol_parity_cleanup", stdlib_state.meta_live == base_live);
    }

    mol_check("mol_final_live_balanced", stdlib_state.meta_live == base_live);
    print("MEMORY_OWNERSHIP", mol_checks, mol_failures);
    return mol_failures == 0 ? 0 : 1;
}
