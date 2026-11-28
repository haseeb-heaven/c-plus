/* Extra self-checking coverage for the UNTESTED stdlib/error.hp metadata and
 * ownership paths named by the final audit (FINAL_AUDIT.md section 17 and
 * tests/stdlib/agent2_audit/module_audits/error.hp.md section 7).
 *
 * Covered here, without modifying any header or existing test:
 *   A  stdlib_alloc_size / elem_size / length / query_length on a tracked
 *      stdlib_alloc block (including caller elem_size mismatch and the
 *      elem_size==0 fallback) plus NULL / untracked / released probes;
 *   B  the same queries on a stdlib_calloc block whose recorded elem_size is
 *      the real element size, and the zero-fill contract;
 *   C  calloc(elem_size==0) and calloc(count==0) fallback metadata;
 *   D  stdlib_alloc_generation and stdlib_alloc_check_generation mismatch
 *      detection, re-record generation bump, and released-block generation 0;
 *   E  stdlib_transfer_ownership rejection branches: non-NULL dead source
 *      (stack address), non-NULL dead source (released block), dst==src,
 *      non-NULL destination, NULL source; then a successful transfer with a
 *      fresh generation and a stale alias;
 *   F  invalid category/code mapping via error_fail_cat/error_fail and the
 *      stdlib_category_name() "unknown" fallback, including the stdlib_note
 *      counter mapping for every category;
 *   G  repeated reset/fail cycles with a callback that calls error_fail
 *      re-entrantly: exactly one callback per outer failure, no recursion,
 *      no stuck reporting guard;
 *   H  telemetry balance after drops and the stdlib_telemetry_snapshot(NULL)
 *      no-op.
 *
 * All allocations are deterministic (no randomness, no I/O, no environment
 * dependence). Stale pointers are NEVER dereferenced: they are only passed to
 * helpers that hash/compare the pointer value. No fatal probe is executed, no
 * header is edited to force a pass, and no undefined behaviour is used.
 *
 * Each check prints "<tag> <0|1>". The final line is
 * "EXTRA_ERROR_METADATA <checks> <failures>"; exit 0 only if failures == 0.
 * Every owned/error variable has a unique eem_-prefixed name.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int eem_checks = 0;
static int eem_failures = 0;

static void eem_check(const char *tag, int cond) {
    eem_checks++;
    if (!cond)
        eem_failures++;
    print(tag, cond ? 1 : 0);
}

/* Re-entrancy probe state. The reporting guard in stdlib_note() must drop the
 * nested error_fail raised from inside this callback, so eem_cb_depth must
 * never exceed 1 and eem_cb_overflow must stay 0. */
static error *eem_cb_target = (error *)0;
static cplus_exit_code eem_cb_code = CPLUS_EXIT_RESOURCE;
static const char *eem_cb_msg = "nested";
static int eem_cb_calls = 0;
static int eem_cb_depth = 0;
static int eem_cb_overflow = 0;

static void eem_reentrant_cb(int category, cplus_exit_code code,
                             const char *message) {
    (void)category;
    (void)code;
    (void)message;
    if (eem_cb_depth != 0) {
        eem_cb_overflow++;
        return;
    }
    eem_cb_depth++;
    eem_cb_calls++;
    if (eem_cb_target != (error *)0)
        error_fail(eem_cb_target, eem_cb_code, eem_cb_msg);
    eem_cb_depth--;
}

/* Assert an allocation/free delta inside a telemetry epoch. */
static void eem_balance(const char *tag, stdlib_telemetry_stats *pre,
                        stdlib_telemetry_stats *post,
                        unsigned long long want) {
    int ok = post->alloc_count - pre->alloc_count == want &&
             post->free_count - pre->free_count == want;
    eem_check(tag, ok);
}

int main(void) {
    stdlib_telemetry_stats eem_pre;
    stdlib_telemetry_stats eem_post;
    stdlib_telemetry_stats eem_a_pre;
    stdlib_telemetry_stats eem_a_post;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);

    /* ---- A: queries on a stdlib_alloc block (recorded elem_size == 1) --- */
    {
        void *eem_a_block = NULL;
        size_t eem_a_out = 999u;
        stdlib_telemetry_snapshot(&eem_a_pre);
        eem_a_block = stdlib_alloc(40);
        eem_check("eem_a_alloc", eem_a_block != NULL);
        if (eem_a_block != NULL) {
            eem_check("eem_a_size", stdlib_alloc_size(eem_a_block) == 40u);
            eem_check("eem_a_elem_size",
                      stdlib_alloc_elem_size(eem_a_block) == 1u);
            eem_check("eem_a_length", stdlib_alloc_length(eem_a_block) == 40u);

            eem_a_out = 999u;
            eem_check("eem_a_query_default",
                      stdlib_alloc_query_length(eem_a_block, 0, &eem_a_out) == 0 &&
                          eem_a_out == 40u);
            eem_a_out = 999u;
            eem_check("eem_a_query_elem8",
                      stdlib_alloc_query_length(eem_a_block, 8, &eem_a_out) == 0 &&
                          eem_a_out == 5u);
            eem_a_out = 999u;
            eem_check("eem_a_query_elem7_mismatch",
                      stdlib_alloc_query_length(eem_a_block, 7, &eem_a_out) == 0 &&
                          eem_a_out == 5u); /* 40 / 7 == 5 */
            eem_a_out = 999u;
            eem_check("eem_a_query_null_ptr",
                      stdlib_alloc_query_length(NULL, 1, &eem_a_out) == -1 &&
                          eem_a_out == 0u);
            eem_check("eem_a_query_null_out",
                      stdlib_alloc_query_length(eem_a_block, 1, NULL) == -1);

            /* Untracked local address: all queries report 0 / -1. */
            eem_check("eem_a_untracked_queries",
                      stdlib_alloc_size((void *)&eem_a_out) == 0u &&
                          stdlib_alloc_elem_size((void *)&eem_a_out) == 0u &&
                          stdlib_alloc_length((void *)&eem_a_out) == 0u &&
                          stdlib_alloc_generation((void *)&eem_a_out) == 0ULL);

            stdlib_release(eem_a_block);
            eem_check("eem_a_released_zero",
                      stdlib_alloc_size(eem_a_block) == 0u &&
                          stdlib_alloc_length(eem_a_block) == 0u &&
                          stdlib_alloc_generation(eem_a_block) == 0ULL);
        }
        stdlib_telemetry_snapshot(&eem_a_post);
        eem_balance("eem_a_balance", &eem_a_pre, &eem_a_post, 1ULL);
    }

    /* ---- B: queries on a stdlib_calloc block (real elem_size) ---------- */
    {
        void *eem_b_block = NULL;
        int *eem_b_view = NULL;
        size_t eem_b_out = 999u;
        int eem_b_i = 0;
        stdlib_telemetry_snapshot(&eem_pre);
        eem_b_block = stdlib_calloc(8, sizeof(int));
        eem_check("eem_b_alloc", eem_b_block != NULL);
        if (eem_b_block != NULL) {
            eem_check("eem_b_size",
                      stdlib_alloc_size(eem_b_block) == 8u * sizeof(int));
            eem_check("eem_b_elem_size",
                      stdlib_alloc_elem_size(eem_b_block) == sizeof(int));
            eem_check("eem_b_length", stdlib_alloc_length(eem_b_block) == 8u);

            eem_b_out = 999u;
            eem_check("eem_b_query_default",
                      stdlib_alloc_query_length(eem_b_block, 0, &eem_b_out) == 0 &&
                          eem_b_out == 8u); /* elem_size 0 -> recorded 4 */
            eem_b_out = 999u;
            eem_check("eem_b_query_elem1",
                      stdlib_alloc_query_length(eem_b_block, 1, &eem_b_out) == 0 &&
                          eem_b_out == 8u * sizeof(int));
            eem_b_out = 999u;
            eem_check("eem_b_query_elem4",
                      stdlib_alloc_query_length(eem_b_block, sizeof(int),
                                                &eem_b_out) == 0 &&
                          eem_b_out == 8u);
            eem_b_out = 999u;
            eem_check("eem_b_query_elem8_mismatch",
                      stdlib_alloc_query_length(eem_b_block, 8, &eem_b_out) == 0 &&
                          eem_b_out == 4u); /* 32 / 8 == 4 */

            eem_b_view = (int *)eem_b_block;
            for (eem_b_i = 0; eem_b_i < 8; eem_b_i++) {
                if (eem_b_view[eem_b_i] != 0)
                    break;
            }
            eem_check("eem_b_zeroed", eem_b_i == 8);
            for (eem_b_i = 0; eem_b_i < 8; eem_b_i++)
                eem_b_view[eem_b_i] = eem_b_i;
            eem_check("eem_b_in_bounds_write", eem_b_view[7] == 7);

            stdlib_release(eem_b_block);
        }
        stdlib_telemetry_snapshot(&eem_post);
        eem_balance("eem_b_balance", &eem_pre, &eem_post, 1ULL);
    }

    /* ---- C: calloc(elem_size==0) and calloc(count==0) fallbacks --------- */
    {
        void *eem_c_zero_elem = NULL;
        void *eem_c_zero_count = NULL;
        size_t eem_c_out = 999u;
        stdlib_telemetry_snapshot(&eem_pre);
        eem_c_zero_elem = stdlib_calloc(10, 0);
        eem_c_zero_count = stdlib_calloc(0, sizeof(int));
        eem_check("eem_c_alloc", eem_c_zero_elem != NULL && eem_c_zero_count != NULL);
        if (eem_c_zero_elem != NULL) {
            /* calloc normalizes a 0 element size to 1 before recording. */
            eem_check("eem_c_zero_elem_meta",
                      stdlib_alloc_size(eem_c_zero_elem) == 1u &&
                          stdlib_alloc_elem_size(eem_c_zero_elem) == 1u &&
                          stdlib_alloc_length(eem_c_zero_elem) == 1u);
            eem_c_out = 999u;
            eem_check("eem_c_zero_elem_query",
                      stdlib_alloc_query_length(eem_c_zero_elem, 0,
                                                &eem_c_out) == 0 &&
                          eem_c_out == 1u);
        }
        if (eem_c_zero_count != NULL) {
            /* count==0 -> total 1 byte, recorded elem_size stays 4, so the
             * derived length is 1 / 4 == 0. */
            eem_check("eem_c_zero_count_meta",
                      stdlib_alloc_size(eem_c_zero_count) == 1u &&
                          stdlib_alloc_elem_size(eem_c_zero_count) == sizeof(int) &&
                          stdlib_alloc_length(eem_c_zero_count) == 0u);
            eem_c_out = 999u;
            eem_check("eem_c_zero_count_query",
                      stdlib_alloc_query_length(eem_c_zero_count, 0,
                                                &eem_c_out) == 0 &&
                          eem_c_out == 0u);
        }
        stdlib_release(eem_c_zero_elem);
        stdlib_release(eem_c_zero_count);
        stdlib_telemetry_snapshot(&eem_post);
        eem_balance("eem_c_balance", &eem_pre, &eem_post, 2ULL);
    }

    /* ---- D: generation and check_generation mismatch detection --------- */
    {
        void *eem_d_block = NULL;
        unsigned long long eem_d_g1 = 0ULL;
        unsigned long long eem_d_g2 = 0ULL;
        stdlib_telemetry_snapshot(&eem_pre);
        eem_d_block = stdlib_alloc(16);
        eem_check("eem_d_alloc", eem_d_block != NULL);
        if (eem_d_block != NULL) {
            eem_d_g1 = stdlib_alloc_generation(eem_d_block);
            eem_check("eem_d_gen_nonzero", eem_d_g1 != 0ULL);
            eem_check("eem_d_gen_null", stdlib_alloc_generation(NULL) == 0ULL);
            eem_check("eem_d_check_ok",
                      stdlib_alloc_check_generation(eem_d_block, eem_d_g1) == 1);
            eem_check("eem_d_check_mismatch_plus1",
                      stdlib_alloc_check_generation(eem_d_block,
                                                    eem_d_g1 + 1ULL) == 0);
            eem_check("eem_d_check_mismatch_xor",
                      stdlib_alloc_check_generation(eem_d_block,
                                                    eem_d_g1 ^ 0xABCDULL) == 0);
            eem_check("eem_d_check_gen0",
                      stdlib_alloc_check_generation(eem_d_block, 0ULL) == 0);
            eem_check("eem_d_check_null",
                      stdlib_alloc_check_generation(NULL, eem_d_g1) == 0);

            /* Re-recording the same pointer updates its metadata and bumps
             * the generation: the old generation must become stale. */
            stdlib_meta_record(eem_d_block, 16, 1);
            eem_d_g2 = stdlib_alloc_generation(eem_d_block);
            eem_check("eem_d_rerecord_gen",
                      eem_d_g2 != 0ULL && eem_d_g2 != eem_d_g1);
            eem_check("eem_d_old_gen_stale",
                      stdlib_alloc_check_generation(eem_d_block, eem_d_g1) == 0 &&
                          stdlib_alloc_check_generation(eem_d_block, eem_d_g2) == 1);

            stdlib_release(eem_d_block);
            eem_check("eem_d_released_gen",
                      stdlib_alloc_generation(eem_d_block) == 0ULL &&
                          stdlib_alloc_check_generation(eem_d_block,
                                                        eem_d_g2) == 0);
        }
        stdlib_telemetry_snapshot(&eem_post);
        eem_balance("eem_d_balance", &eem_pre, &eem_post, 1ULL);
    }

    /* ---- E: transfer_ownership rejection branches + success ------------ */
    {
        void *eem_e_live = NULL;
        void *eem_e_dst = NULL;
        void *eem_e_self = NULL;
        void *eem_e_other = NULL;
        void *eem_e_stale = NULL;
        void *eem_e_nullsrc = NULL;
        unsigned long long eem_e_gen = 0ULL;
        unsigned long long eem_e_own_before = 0ULL;
        unsigned long long eem_e_api_before = 0ULL;

        stdlib_telemetry_snapshot(&eem_pre);
        eem_e_live = stdlib_alloc(24);
        eem_e_other = stdlib_alloc(8);
        eem_e_stale = stdlib_alloc(8);
        eem_check("eem_e_alloc",
                  eem_e_live != NULL && eem_e_other != NULL && eem_e_stale != NULL);
        if (eem_e_live != NULL && eem_e_other != NULL && eem_e_stale != NULL) {
            eem_e_gen = stdlib_alloc_generation(eem_e_live);
            eem_e_own_before = eem_pre.ownership_violations;
            eem_e_api_before = eem_pre.api_failures;

            /* (1) non-NULL dead source: a stack address, never a live block. */
            eem_e_dst = NULL;
            eem_e_self = (void *)&eem_e_gen; /* stack address, non-NULL */
            eem_check("eem_e_dead_stack_reject",
                      stdlib_transfer_ownership(&eem_e_dst, &eem_e_self) == -1 &&
                          eem_e_self == (void *)&eem_e_gen && eem_e_dst == NULL);

            /* (2) non-NULL dead source: a released block pointer (stale). */
            stdlib_release(eem_e_stale);
            eem_e_dst = NULL;
            eem_check("eem_e_dead_released_reject",
                      stdlib_transfer_ownership(&eem_e_dst, &eem_e_stale) == -1 &&
                          eem_e_stale != NULL && eem_e_dst == NULL);
            /* eem_e_stale is intentionally NOT released again. */

            /* (3) dst == src with a live block. */
            eem_e_self = eem_e_live;
            eem_check("eem_e_dst_src_reject",
                      stdlib_transfer_ownership(&eem_e_self, &eem_e_self) == -1 &&
                          eem_e_self == eem_e_live);

            /* (4) destination already holds a live block. */
            eem_e_dst = eem_e_other;
            eem_check("eem_e_dst_nonnull_reject",
                      stdlib_transfer_ownership(&eem_e_dst, &eem_e_live) == -1 &&
                          eem_e_live != NULL && eem_e_dst == eem_e_other);

            /* (5) NULL source. */
            eem_e_dst = NULL;
            eem_check("eem_e_src_null_reject",
                      stdlib_transfer_ownership(&eem_e_dst, &eem_e_nullsrc) == -1 &&
                          eem_e_dst == NULL);

            stdlib_telemetry_snapshot(&eem_post);
            eem_check("eem_e_reject_telemetry",
                      eem_post.ownership_violations - eem_e_own_before == 5ULL &&
                          eem_post.api_failures - eem_e_api_before == 5ULL);

            /* (6) successful transfer: fresh generation, stale alias. */
            eem_e_dst = NULL;
            eem_check("eem_e_transfer_ok",
                      stdlib_transfer_ownership(&eem_e_dst, &eem_e_live) == 0);
            eem_check("eem_e_transfer_handles",
                      eem_e_live == NULL && eem_e_dst != NULL);
            eem_check("eem_e_transfer_fresh_gen",
                      stdlib_alloc_generation(eem_e_dst) != 0ULL &&
                          stdlib_alloc_generation(eem_e_dst) != eem_e_gen &&
                          stdlib_alloc_check_generation(eem_e_dst,
                                                        eem_e_gen) == 0);
            eem_check("eem_e_transfer_length",
                      stdlib_alloc_length(eem_e_dst) == 24u);
            eem_check("eem_e_second_transfer_reject",
                      stdlib_transfer_ownership(&eem_e_dst, &eem_e_live) == -1);

            stdlib_release(eem_e_dst);
            stdlib_release(eem_e_other);
        }
        stdlib_telemetry_snapshot(&eem_post);
        eem_balance("eem_e_balance", &eem_pre, &eem_post, 3ULL);
    }

    /* ---- F: invalid category/code mapping + name fallback -------------- */
    {
        error eem_f_err = error();
        int eem_f_i = 0;
        int eem_f_cats[9];
        const char *eem_f_names[9];
        int eem_f_ok = 1;
        unsigned long long eem_f_want_alloc = 0ULL;
        unsigned long long eem_f_want_bounds = 0ULL;
        unsigned long long eem_f_want_own = 0ULL;
        unsigned long long eem_f_want_life = 0ULL;
        unsigned long long eem_f_want_invalid = 0ULL;
        char eem_f_tag[40];

        eem_f_cats[0] = STDLIB_CATEGORY_OK;
        eem_f_cats[1] = STDLIB_CATEGORY_INVALID;
        eem_f_cats[2] = STDLIB_CATEGORY_RANGE;
        eem_f_cats[3] = STDLIB_CATEGORY_IO;
        eem_f_cats[4] = STDLIB_CATEGORY_MEMORY;
        eem_f_cats[5] = STDLIB_CATEGORY_BOUNDS;
        eem_f_cats[6] = STDLIB_CATEGORY_OWNERSHIP;
        eem_f_cats[7] = STDLIB_CATEGORY_LIFECYCLE;
        eem_f_cats[8] = STDLIB_CATEGORY_UTF8;
        eem_f_names[0] = "ok";
        eem_f_names[1] = "invalid";
        eem_f_names[2] = "range";
        eem_f_names[3] = "io";
        eem_f_names[4] = "memory";
        eem_f_names[5] = "bounds";
        eem_f_names[6] = "ownership";
        eem_f_names[7] = "lifecycle";
        eem_f_names[8] = "utf8";

        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        for (eem_f_i = 0; eem_f_i < 9; eem_f_i++) {
            stdlib_telemetry_snapshot(&eem_pre);
            error_reset(&eem_f_err);
            error_fail_cat(&eem_f_err, eem_f_cats[eem_f_i],
                           (cplus_exit_code)(200 + eem_f_i), "m");
            stdlib_telemetry_snapshot(&eem_post);
            eem_f_ok = eem_f_err.failed_now() == 1 &&
                       eem_f_err.category() == eem_f_cats[eem_f_i] &&
                       eem_f_err.code() == 200 + eem_f_i &&
                       strcmp(eem_f_err.what(), "m") == 0 &&
                       strcmp(stdlib_category_name(eem_f_cats[eem_f_i]),
                              eem_f_names[eem_f_i]) == 0;
            eem_f_ok = eem_f_ok &&
                       eem_post.api_failures - eem_pre.api_failures == 1ULL;
            if (eem_f_cats[eem_f_i] == STDLIB_CATEGORY_MEMORY)
                eem_f_want_alloc++;
            else if (eem_f_cats[eem_f_i] == STDLIB_CATEGORY_BOUNDS)
                eem_f_want_bounds++;
            else if (eem_f_cats[eem_f_i] == STDLIB_CATEGORY_OWNERSHIP)
                eem_f_want_own++;
            else if (eem_f_cats[eem_f_i] == STDLIB_CATEGORY_LIFECYCLE)
                eem_f_want_life++;
            else
                eem_f_want_invalid++;
            snprintf(eem_f_tag, sizeof(eem_f_tag), "eem_f_cat_%d", eem_f_i);
            eem_check(eem_f_tag, eem_f_ok);
        }
        stdlib_telemetry_snapshot(&eem_post);
        eem_check("eem_f_note_ctr_mapping",
                  eem_post.alloc_failures == eem_f_want_alloc &&
                      eem_post.bounds_violations == eem_f_want_bounds &&
                      eem_post.ownership_violations == eem_f_want_own &&
                      eem_post.lifecycle_failures == eem_f_want_life &&
                      eem_post.invalid_ops == eem_f_want_invalid &&
                      eem_post.api_failures == 9ULL);

        eem_check("eem_f_name_fallback_high",
                  strcmp(stdlib_category_name(999), "unknown") == 0);
        eem_check("eem_f_name_fallback_neg",
                  strcmp(stdlib_category_name(-5), "unknown") == 0);

        error_reset(&eem_f_err);
        error_fail_cat(&eem_f_err, 999, (cplus_exit_code)7777, "weird");
        eem_check("eem_f_invalid_cat_stored",
                  eem_f_err.category() == 999 && eem_f_err.code() == 7777 &&
                      strcmp(eem_f_err.what(), "weird") == 0);

        error_reset(&eem_f_err);
        error_fail(&eem_f_err, (cplus_exit_code)4242, "badcode");
        eem_check("eem_f_unknown_code_invalid",
                  eem_f_err.failed_now() == 1 &&
                      eem_f_err.category() == STDLIB_CATEGORY_INVALID &&
                      eem_f_err.code() == 4242 &&
                      strcmp(eem_f_err.what(), "badcode") == 0);

        error_reset(&eem_f_err);
        error_fail(&eem_f_err, CPLUS_EXIT_USE_AFTER_FREE, "lifetime");
        eem_check("eem_f_lifecycle_map",
                  eem_f_err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                      eem_f_err.code() == (int)CPLUS_EXIT_USE_AFTER_FREE);
    }

    /* ---- G: reset/fail cycles with a reentrant callback ---------------- */
    {
        error eem_g_err = error();
        int eem_g_i = 0;
        int eem_g_ok = 1;
        stdlib_telemetry_stats eem_g_tm;

        stdlib_set_error_callback((stdlib_error_callback)0);
        error_reset(&eem_g_err);
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        eem_cb_target = &eem_g_err;
        eem_cb_code = CPLUS_EXIT_RESOURCE;
        eem_cb_msg = "nested";
        eem_cb_calls = 0;
        eem_cb_depth = 0;
        eem_cb_overflow = 0;
        stdlib_set_error_callback(eem_reentrant_cb);

        for (eem_g_i = 0; eem_g_i < 64; eem_g_i++) {
            error_reset(&eem_g_err);
            error_fail(&eem_g_err, CPLUS_EXIT_IO, "outer");
        }
        stdlib_telemetry_snapshot(&eem_g_tm);
        eem_g_ok = eem_cb_calls == 64 && eem_cb_overflow == 0 &&
                   eem_cb_depth == 0;
        /* Per cycle: outer IO note (invalid_ops + api_failures) and one nested
         * RESOURCE note dropped by the guard (alloc_failures + api_failures).
         * The outer record wins because error_fail_cat writes fields after
         * stdlib_note() returns. */
        eem_g_ok = eem_g_ok && eem_g_tm.api_failures == 128ULL &&
                   eem_g_tm.invalid_ops == 64ULL &&
                   eem_g_tm.alloc_failures == 64ULL;
        eem_g_ok = eem_g_ok && eem_g_err.failed_now() == 1 &&
                   eem_g_err.category() == STDLIB_CATEGORY_IO &&
                   eem_g_err.code() == (int)CPLUS_EXIT_IO &&
                   strcmp(eem_g_err.what(), "outer") == 0;
        eem_check("eem_g_cycles_reentrant", eem_g_ok);

        eem_cb_calls = 0;
        error_reset(&eem_g_err);
        error_fail(&eem_g_err, CPLUS_EXIT_BOUNDS, "again");
        eem_check("eem_g_no_stuck_guard",
                  eem_cb_calls == 1 && eem_cb_overflow == 0 &&
                      eem_g_err.category() == STDLIB_CATEGORY_BOUNDS &&
                      eem_g_err.code() == (int)CPLUS_EXIT_BOUNDS &&
                      strcmp(eem_g_err.what(), "again") == 0);

        error_reset(&eem_g_err);
        eem_check("eem_g_reset_clean",
                  eem_g_err.ok() == 1 && eem_g_err.failed_now() == 0 &&
                      eem_g_err.category() == STDLIB_CATEGORY_OK &&
                      eem_g_err.code() == (int)CPLUS_EXIT_OK &&
                      strcmp(eem_g_err.what(), "") == 0);

        stdlib_set_error_callback((stdlib_error_callback)0);
        eem_cb_target = (error *)0;
    }

    /* ---- H: telemetry balance after drops + snapshot(NULL) no-op ------- */
    {
        void *eem_h_a = NULL;
        void *eem_h_b = NULL;
        stdlib_telemetry_stats eem_h_pre_null;
        stdlib_telemetry_stats eem_h_post_null;

        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_telemetry_snapshot(&eem_h_pre_null);
        stdlib_telemetry_snapshot(NULL); /* documented no-op */
        stdlib_telemetry_snapshot(&eem_h_post_null);
        eem_check("eem_h_snapshot_null_noop",
                  memcmp(&eem_h_pre_null, &eem_h_post_null,
                         sizeof(stdlib_telemetry_stats)) == 0);

        stdlib_telemetry_snapshot(&eem_pre);
        eem_h_a = stdlib_alloc(64);
        eem_h_b = stdlib_calloc(5, sizeof(double));
        eem_check("eem_h_alloc", eem_h_a != NULL && eem_h_b != NULL);
        stdlib_release(eem_h_a);
        stdlib_release(eem_h_b);
        stdlib_release(NULL); /* idempotent no-op */
        stdlib_telemetry_snapshot(&eem_post);
        eem_check("eem_h_balance",
                  eem_post.alloc_count - eem_pre.alloc_count == 2ULL &&
                      eem_post.free_count - eem_pre.free_count == 2ULL &&
                      eem_post.bytes_allocated - eem_pre.bytes_allocated ==
                          64ULL + 5ULL * sizeof(double) &&
                      eem_post.lifecycle_failures == 0ULL);
    }

    stdlib_telemetry_set_enabled(0);
    print("EXTRA_ERROR_METADATA", eem_checks, eem_failures);
    return eem_failures == 0 ? 0 : 1;
}
