/* ABI_SAFETY / allocation lifecycle and side-table protection (agent 2).
 *
 * Dedicated runtime-safety coverage for the checked allocator in
 * stdlib/error.hp and its metadata side table (stdlib_state.meta_tab).
 * Self-checking, deterministic, no stdin, no external library.
 *
 * Covered here:
 *   A  heap alloc/release via stdlib_alloc / stdlib_release, with the runtime
 *      side table (stdlib_state.meta_live) and telemetry returning to base;
 *   B  double-free DETECTION without invoking a fatal path: after a release,
 *      generation queries report 0 and stdlib_transfer_ownership refuses the
 *      stale alias.  A literal second stdlib_release() is fatal by design and
 *      is deliberately NOT called (see ABI_SAFETY_NOTES.md for the repro);
 *   C  invalid-free REFUSAL for an untracked stack address through the
 *      non-destructive stdlib_transfer_ownership / query helpers (no crash);
 *   D  use-after-free DETECTION ONLY via metadata queries
 *      (stdlib_alloc_generation / stdlib_alloc_check_generation /
 *       stdlib_alloc_query_length) — the stale pointer is never dereferenced;
 *   E  repeated alloc/release cycles with side-table and telemetry balance;
 *   F  custom allocator plug-in point: documented SKIP.
 *
 * Every check prints "<tag> <0|1>" (1 = passed). The final line is
 * "ABI_SAFETY <checks> <failures>"; the process exits 0 only on zero failures.
 * All owned variables use the unique "asal_" prefix.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int asal_checks = 0;
static int asal_failures = 0;

static void asal_check(const char *tag, int cond) {
    asal_checks++;
    if (!cond)
        asal_failures++;
    print(tag, cond ? 1 : 0);
}

static void asal_balance(const char *tag, stdlib_telemetry_stats *pre,
                         stdlib_telemetry_stats *post,
                         unsigned long long want) {
    int ok = post->alloc_count - pre->alloc_count == want &&
             post->free_count - pre->free_count == want;
    asal_check(tag, ok);
}

int main(void) {
    stdlib_telemetry_stats asal_pre;
    stdlib_telemetry_stats asal_post;
    size_t asal_base_live = 0;
    int asal_stack_anchor = 0x5A5A;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    asal_base_live = stdlib_state.meta_live;

    /* ---- A: heap alloc/release ----------------------------------------- */
    {
        void *asal_a_block = NULL;
        unsigned long long asal_a_gen = 0ULL;
        stdlib_telemetry_snapshot(&asal_pre);
        asal_a_block = stdlib_alloc(64);
        asal_check("asal_a_alloc_nonnull", asal_a_block != NULL);
        if (asal_a_block != NULL) {
            asal_a_gen = stdlib_alloc_generation(asal_a_block);
            asal_check("asal_a_gen_nonzero", asal_a_gen != 0ULL);
            asal_check("asal_a_size", stdlib_alloc_size(asal_a_block) == 64u);
            asal_check("asal_a_live_increment",
                       stdlib_state.meta_live == asal_base_live + 1u);
            stdlib_release(asal_a_block);
            asal_check("asal_a_released_gen",
                       stdlib_alloc_generation(asal_a_block) == 0ULL);
            asal_check("asal_a_live_restored",
                       stdlib_state.meta_live == asal_base_live);
        }
        stdlib_telemetry_snapshot(&asal_post);
        asal_balance("asal_a_balance", &asal_pre, &asal_post, 1ULL);
    }

    /* ---- B: double-free detection (non-fatal, metadata only) ------------ */
    {
        void *asal_b_block = NULL;
        void *asal_b_alias = NULL;
        void *asal_b_dst = NULL;
        unsigned long long asal_b_gen = 0ULL;
        stdlib_telemetry_snapshot(&asal_pre);
        asal_b_block = stdlib_alloc(48);
        asal_check("asal_b_alloc", asal_b_block != NULL);
        if (asal_b_block != NULL) {
            asal_b_gen = stdlib_alloc_generation(asal_b_block);
            asal_b_alias = asal_b_block; /* saved; never dereferenced again */
            stdlib_release(asal_b_block);
            asal_check("asal_b_stale_gen_zero",
                       stdlib_alloc_generation(asal_b_alias) == 0ULL);
            asal_check("asal_b_stale_check_reject",
                       stdlib_alloc_check_generation(asal_b_alias,
                                                     asal_b_gen) == 0);
            asal_check("asal_b_stale_query_reject",
                       stdlib_alloc_query_length(asal_b_alias, 1u, NULL) == -1);
            asal_check("asal_b_double_release_refused",
                       stdlib_transfer_ownership(&asal_b_dst, &asal_b_alias) == -1 &&
                           asal_b_alias != NULL && asal_b_dst == NULL);
            /* A literal second stdlib_release(asal_b_alias) is fatal by design. */
        }
        stdlib_telemetry_snapshot(&asal_post);
        asal_balance("asal_b_balance", &asal_pre, &asal_post, 1ULL);
    }

    /* ---- C: invalid-free refusal for untracked stack address ------------ */
    {
        void *asal_c_untracked = (void *)&asal_stack_anchor;
        void *asal_c_src = asal_c_untracked;
        void *asal_c_dst = NULL;
        size_t asal_c_count = 999u;
        asal_check("asal_c_untracked_size_zero",
                   stdlib_alloc_size(asal_c_untracked) == 0u);
        asal_check("asal_c_untracked_gen_zero",
                   stdlib_alloc_generation(asal_c_untracked) == 0ULL);
        asal_check("asal_c_untracked_query_reject",
                   stdlib_alloc_query_length(asal_c_untracked, 1u,
                                             &asal_c_count) == -1 &&
                       asal_c_count == 0u);
        asal_check("asal_c_invalid_free_refused",
                   stdlib_transfer_ownership(&asal_c_dst, &asal_c_src) == -1 &&
                       asal_c_src == asal_c_untracked && asal_c_dst == NULL);
        asal_check("asal_c_stack_anchor_intact", asal_stack_anchor == 0x5A5A);
    }

    /* ---- D: use-after-free detection via metadata only ------------------ */
    {
        void *asal_d_block = NULL;
        unsigned long long asal_d_gen = 0ULL;
        size_t asal_d_len = 0u;
        stdlib_telemetry_snapshot(&asal_pre);
        asal_d_block = stdlib_alloc(32);
        asal_check("asal_d_alloc", asal_d_block != NULL);
        if (asal_d_block != NULL) {
            asal_d_gen = stdlib_alloc_generation(asal_d_block);
            asal_check("asal_d_live_check",
                       stdlib_alloc_check_generation(asal_d_block,
                                                     asal_d_gen) == 1);
            asal_check("asal_d_query_len",
                       stdlib_alloc_query_length(asal_d_block, 4u,
                                                 &asal_d_len) == 0 &&
                           asal_d_len == 8u);
            stdlib_release(asal_d_block);
            /* Stale pointer is queried, never read or written. */
            asal_check("asal_d_uaf_gen_zero",
                       stdlib_alloc_generation(asal_d_block) == 0ULL);
            asal_check("asal_d_uaf_check_reject",
                       stdlib_alloc_check_generation(asal_d_block,
                                                     asal_d_gen) == 0);
            asal_d_len = 999u;
            asal_check("asal_d_uaf_query_reject",
                       stdlib_alloc_query_length(asal_d_block, 4u,
                                                 &asal_d_len) == -1 &&
                           asal_d_len == 0u);
        }
        stdlib_telemetry_snapshot(&asal_post);
        asal_balance("asal_d_balance", &asal_pre, &asal_post, 1ULL);
    }

    /* ---- E: repeated cleanup cycles ------------------------------------- */
    {
        int asal_e_i = 0;
        int asal_e_ok = 1;
        void *asal_e_block = NULL;
        unsigned long long asal_e_gen = 0ULL;
        stdlib_telemetry_snapshot(&asal_pre);
        for (asal_e_i = 0; asal_e_i < 64; ++asal_e_i) {
            asal_e_block = stdlib_alloc((size_t)(asal_e_i + 1u));
            if (asal_e_block == NULL) {
                asal_e_ok = 0;
                break;
            }
            asal_e_gen = stdlib_alloc_generation(asal_e_block);
            if (asal_e_gen == 0ULL ||
                stdlib_alloc_size(asal_e_block) != (size_t)(asal_e_i + 1u) ||
                stdlib_state.meta_live != asal_base_live + 1u) {
                asal_e_ok = 0;
            }
            stdlib_release(asal_e_block);
            asal_e_block = NULL;
            if (stdlib_state.meta_live != asal_base_live ||
                stdlib_alloc_check_generation(asal_e_block, asal_e_gen) != 0) {
                asal_e_ok = 0;
            }
        }
        asal_check("asal_e_repeated_cycles", asal_e_ok == 1 && asal_e_i == 64);
        stdlib_telemetry_snapshot(&asal_post);
        asal_balance("asal_e_balance", &asal_pre, &asal_post, 64ULL);
    }

    /* ---- F: custom allocator plug-in point (SKIP) ------------------------ */
    /* error.hp exposes no custom allocator hook: stdlib_alloc always routes
       to cplus_alloc and stdlib_release always routes to cplus_release.  There
       is nothing to test, so the check is recorded as an explicit SKIP. */
    print("SKIP abi_custom_alloc: no custom allocator plug-in point in "
          "stdlib/error.hp or the C+ runtime");
    asal_check("asal_custom_alloc_unavailable", 1);

    print("ABI_SAFETY", asal_checks, asal_failures);
    return asal_failures == 0 ? 0 : 1;
}
