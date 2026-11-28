/* memory_ownership regression suite -- allocation edges, realloc, telemetry.
 *
 * Deterministic and self-checking: fixed operation order, fixed-seed LCG,
 * bounded loops (24 iterations).  No stdin, no wall-clock, no raw pointer
 * dereference of a released/stale address.  Every check prints
 * "<tag> <0|1>"; the final line is "MEMORY_OWNERSHIP <checks> <failures>".
 * The process exits non-zero iff failures != 0.
 *
 * Scope (see NOTES.md for the UAF detection limit statement):
 *   zero-size allocation, NULL handles, alloc/calloc size overflow,
 *   realloc success metadata consistency + refusal atomicity, injected
 *   allocation-failure cleanup, telemetry balance.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int moa_checks = 0;
static int moa_failures = 0;

static void moa_check(const char *tag, int cond) {
    moa_checks++;
    if (!cond)
        moa_failures++;
    print(tag, cond ? 1 : 0);
}

static void moa_balance(const char *tag, stdlib_telemetry_stats *pre,
                        stdlib_telemetry_stats *post,
                        unsigned long long want_alloc,
                        unsigned long long want_free) {
    moa_check(tag, post->alloc_count - pre->alloc_count == want_alloc &&
                   post->free_count - pre->free_count == want_free);
}

int main(void) {
    stdlib_telemetry_stats pre;
    stdlib_telemetry_stats post;
    stdlib_telemetry_stats fail_pre;
    stdlib_telemetry_stats fail_post;
    size_t base_live;
    size_t cnt;
    unsigned long long g0;
    unsigned long long g1;
    void *p;
    void *q;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    base_live = stdlib_state.meta_live;

    /* ---- A: zero-size allocation normalises to one tracked byte -------- */
    stdlib_telemetry_snapshot(&pre);
    p = stdlib_alloc(0u);
    moa_check("moa_zero_alloc_nonnull", p != (void *)0);
    if (p != (void *)0) {
        g0 = stdlib_alloc_generation(p);
        moa_check("moa_zero_gen_nonzero", g0 != 0ULL);
        moa_check("moa_zero_size_one", stdlib_alloc_size(p) == 1u);
        cnt = 0u;
        moa_check("moa_zero_query_one",
                  stdlib_alloc_query_length(p, 1u, &cnt) == 0 && cnt == 1u);
        ((unsigned char *)p)[0] = 0xA5; /* one byte is in bounds */
        moa_check("moa_zero_write_in_bounds", ((unsigned char *)p)[0] == 0xA5);
        stdlib_release(p);
        moa_check("moa_zero_gen_retired", stdlib_alloc_generation(p) == 0ULL);
        moa_check("moa_zero_size_retired", stdlib_alloc_size(p) == 0u);
        moa_check("moa_zero_live_restored", stdlib_state.meta_live == base_live);
    }
    stdlib_telemetry_snapshot(&post);
    moa_balance("moa_zero_balance", &pre, &post, 1ULL, 1ULL);

    /* ---- B: calloc zero-size and multiplication overflow --------------- */
    stdlib_telemetry_snapshot(&pre);
    p = stdlib_calloc(0u, 8u);
    moa_check("moa_calloc_zero_nonnull", p != (void *)0);
    if (p != (void *)0) {
        moa_check("moa_calloc_zero_size_one", stdlib_alloc_size(p) == 1u);
        moa_check("moa_calloc_zero_elem", stdlib_alloc_elem_size(p) == 8u);
        moa_check("moa_calloc_zero_zeroed", ((unsigned char *)p)[0] == 0);
        stdlib_release(p);
    }
    q = stdlib_calloc((size_t)SIZE_MAX, (size_t)2);
    moa_check("moa_calloc_overflow_null", q == (void *)0);
    q = stdlib_calloc((size_t)2, (size_t)SIZE_MAX);
    moa_check("moa_calloc_overflow_null_2", q == (void *)0);
    moa_check("moa_calloc_overflow_no_live", stdlib_state.meta_live == base_live);
    stdlib_telemetry_snapshot(&post);
    moa_check("moa_calloc_overflow_fail_counted",
              post.alloc_count - pre.alloc_count == 1ULL &&
                  post.alloc_failures - pre.alloc_failures == 2ULL &&
                  post.api_failures - pre.api_failures == 2ULL);

    /* ---- C: alloc size overflow / hard limit refusal ------------------- */
    stdlib_telemetry_snapshot(&pre);
    p = stdlib_alloc((size_t)(STDLIB_ALLOC_LIMIT + 1u));
    moa_check("moa_alloc_over_limit_null", p == (void *)0);
    p = stdlib_alloc((size_t)SIZE_MAX);
    moa_check("moa_alloc_size_max_null", p == (void *)0);
    moa_check("moa_alloc_overflow_no_live", stdlib_state.meta_live == base_live);
    stdlib_telemetry_snapshot(&post);
    moa_check("moa_alloc_overflow_fail_counted",
              post.alloc_failures - pre.alloc_failures == 2ULL &&
                  post.api_failures - pre.api_failures == 2ULL &&
                  post.alloc_count == pre.alloc_count);

    /* ---- D: NULL handles are refused by metadata, never dereferenced --- */
    cnt = 77u;
    moa_check("moa_null_size_zero", stdlib_alloc_size((void *)0) == 0u);
    moa_check("moa_null_elem_zero", stdlib_alloc_elem_size((void *)0) == 0u);
    moa_check("moa_null_gen_zero", stdlib_alloc_generation((void *)0) == 0ULL);
    moa_check("moa_null_check_zero",
              stdlib_alloc_check_generation((void *)0, 1ULL) == 0);
    moa_check("moa_null_query_refused",
              stdlib_alloc_query_length((void *)0, 1u, &cnt) == -1 && cnt == 0u);
    stdlib_telemetry_snapshot(&pre);
    stdlib_release((void *)0);
    stdlib_telemetry_snapshot(&post);
    moa_check("moa_null_release_noop", post.free_count == pre.free_count);

    /* ---- E: realloc success keeps metadata consistent ------------------ */
    stdlib_telemetry_snapshot(&pre);
    p = stdlib_calloc(8u, 4u); /* 32 bytes, recorded element size 4 */
    moa_check("moa_realloc_alloc_ok", p != (void *)0);
    if (p != (void *)0) {
        g0 = stdlib_alloc_generation(p);
        moa_check("moa_realloc_pre_size", stdlib_alloc_size(p) == 32u);
        moa_check("moa_realloc_pre_elem", stdlib_alloc_elem_size(p) == 4u);
        q = stdlib_realloc(p, 64u);
        moa_check("moa_realloc_grow_nonnull", q != (void *)0);
        if (q != (void *)0) {
            g1 = stdlib_alloc_generation(q);
            moa_check("moa_realloc_new_size", stdlib_alloc_size(q) == 64u);
            moa_check("moa_realloc_new_elem", stdlib_alloc_elem_size(q) == 4u);
            cnt = 0u;
            moa_check("moa_realloc_new_length",
                      stdlib_alloc_query_length(q, 4u, &cnt) == 0 && cnt == 16u);
            moa_check("moa_realloc_gen_bumped", g1 != 0ULL && g1 != g0);
            moa_check("moa_realloc_old_retired",
                      stdlib_alloc_size(p) == (p == q ? 64u : 0u) &&
                          stdlib_alloc_generation(p) == (p == q ? g1 : 0ULL));
            moa_check("moa_realloc_stale_token_refused",
                      stdlib_alloc_check_generation(q, g0) == 0);
            moa_check("moa_realloc_fresh_token_ok",
                      stdlib_alloc_check_generation(q, g1) == 1);
            /* Payload preserved: q is live and at least 32 bytes long. */
            moa_check("moa_realloc_payload_preserved",
                      ((unsigned char *)q)[0] == 0 &&
                          ((unsigned char *)q)[31] == 0);
            stdlib_release(q);
            moa_check("moa_realloc_release_retires",
                      stdlib_alloc_generation(q) == 0ULL &&
                          stdlib_state.meta_live == base_live);
        }
    }
    stdlib_telemetry_snapshot(&post);
    moa_check("moa_realloc_growth_counted",
              post.container_growths - pre.container_growths == 1ULL);
    moa_balance("moa_realloc_balance", &pre, &post, 1ULL, 1ULL);

    /* ---- F: realloc(NULL,n) and realloc(p,0) normalisation ------------- */
    stdlib_telemetry_snapshot(&pre);
    p = stdlib_realloc((void *)0, 24u);
    moa_check("moa_realloc_null_alloc",
              p != (void *)0 && stdlib_alloc_size(p) == 24u);
    if (p != (void *)0) {
        q = stdlib_realloc(p, 0u);
        moa_check("moa_realloc_zero_one_byte",
                  q != (void *)0 && stdlib_alloc_size(q) == 1u);
        if (q != (void *)0)
            stdlib_release(q);
        else
            stdlib_release(p);
    }
    stdlib_telemetry_snapshot(&post);
    /* realloc(NULL,n) routes through stdlib_alloc; resize does not add an
     * alloc/free count; the one explicit release accounts for the one free. */
    moa_balance("moa_realloc_null_zero_balance", &pre, &post, 1ULL, 1ULL);
    /* realloc(NULL,n) returns early through stdlib_alloc and does not count
     * as a growth; only the realloc(p,0) resize increments the growth counter. */
    moa_check("moa_realloc_null_zero_growth",
              post.container_growths - pre.container_growths == 1ULL);

    /* ---- G: realloc refusal is failure-atomic -------------------------- */
    stdlib_telemetry_snapshot(&pre);
    p = stdlib_alloc(16u);
    moa_check("moa_refusal_alloc", p != (void *)0);
    if (p != (void *)0) {
        g0 = stdlib_alloc_generation(p);
        ((unsigned char *)p)[0] = 0x5C;
        q = stdlib_realloc(p, (size_t)(STDLIB_ALLOC_LIMIT + 1u));
        moa_check("moa_realloc_over_limit_refused", q == (void *)0);
        moa_check("moa_realloc_refusal_preserves_meta",
                  stdlib_alloc_size(p) == 16u &&
                      stdlib_alloc_generation(p) == g0 &&
                      ((unsigned char *)p)[0] == 0x5C);
        {
            int anchor = 0x1234;
            q = stdlib_realloc((void *)&anchor, 8u);
            moa_check("moa_realloc_untracked_refused", q == (void *)0);
            moa_check("moa_realloc_refusal_anchor_intact", anchor == 0x1234);
        }
        moa_check("moa_realloc_refusal_no_live_growth",
                  stdlib_state.meta_live == base_live + 1u);
        stdlib_release(p);
        moa_check("moa_refusal_cleanup", stdlib_state.meta_live == base_live);
    }
    stdlib_telemetry_snapshot(&post);
    /* RANGE and INVALID categories both land in stdlib_note's default arm
     * (invalid_ops), not bounds_violations. */
    moa_check("moa_realloc_refusal_fail_counted",
              post.invalid_ops - pre.invalid_ops == 2ULL &&
                  post.api_failures - pre.api_failures == 2ULL &&
                  post.alloc_count - pre.alloc_count == 1ULL);
    moa_balance("moa_refusal_balance", &pre, &post, 1ULL, 1ULL);

    /* ---- H: injected failure cleanup + fixed-seed bounded cycles ------- */
    stdlib_telemetry_snapshot(&fail_pre);
    stdlib_alloc_fail_at(1);
    p = stdlib_alloc(8u);
    moa_check("moa_injected_fail_null", p == (void *)0);
    moa_check("moa_injected_fail_no_live", stdlib_state.meta_live == base_live);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_snapshot(&fail_post);
    moa_check("moa_injected_fail_counted",
              fail_post.alloc_failures - fail_pre.alloc_failures == 1ULL &&
                  fail_post.alloc_count == fail_pre.alloc_count);

    {
        unsigned long rng = 0x13572468UL;
        int i;
        int ok = 1;
        stdlib_telemetry_snapshot(&pre);
        for (i = 0; i < 24; ++i) {
            size_t sz;
            rng = rng * 1664525UL + 1013904223UL; /* unsigned wrap: defined */
            sz = (size_t)((rng >> 16) & 0x3FUL) + 1u; /* 1..64 */
            p = stdlib_alloc(sz);
            if (p == (void *)0 || stdlib_alloc_size(p) != sz ||
                stdlib_state.meta_live != base_live + 1u) {
                ok = 0;
                break;
            }
            ((unsigned char *)p)[0] = (unsigned char)(i & 0xFF);
            stdlib_release(p);
            if (stdlib_state.meta_live != base_live) {
                ok = 0;
                break;
            }
        }
        moa_check("moa_seeded_cycles", ok == 1 && i == 24);
        stdlib_telemetry_snapshot(&post);
        moa_balance("moa_seeded_cycles_balance", &pre, &post, 24ULL, 24ULL);
    }

    moa_check("moa_final_live_balanced", stdlib_state.meta_live == base_live);
    print("MEMORY_OWNERSHIP", moa_checks, moa_failures);
    return moa_failures == 0 ? 0 : 1;
}
