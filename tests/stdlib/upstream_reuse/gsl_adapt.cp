/* Upstream-reuse regression suite: Microsoft GSL narrow/not_null/span port.
 *
 * Exercises stdlib/gsl_adapt.hp, an ADDITIVE C+ adaptation of the algorithms
 * in microsoft/GSL (MIT).  Self-checking and deterministic: no stdin, no
 * wall-clock, bounded loops only.  Every check prints "<tag> <0|1>" and the
 * final line is "GSL_ADAPT <checks> <failures>"; the process exits non-zero
 * iff failures != 0.
 *
 * Coverage:
 *   A narrow: representable values (all four stages pass)
 *   B narrow: refusals (preflight / round-trip / sign check) + NULL out
 *   C not_null: guarded construction, get invariant, swap, zero overhead
 *   D span: explicit (base,count), at() refusal, first/last/subspan,
 *           provenance refusal, and pointer-representation ABI check
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/gsl_adapt.hp"

static int ga_checks = 0;
static int ga_failures = 0;

static void ga_check(const char *tag, int cond) {
    ga_checks++;
    if (!cond) ga_failures++;
    print(tag, cond ? 1 : 0);
}

int main(void) {
    error err = error();
    /* Out-params are pre-initialised: the strict checker proves initialization
     * locally, not through a callee's write to a pointer parameter. */
    signed char i8 = 0;
    unsigned char u8 = 0u;
    short i16 = 0;
    unsigned short u16 = 0u;
    int i32 = 0;
    unsigned int u32 = 0u;
    long long i64 = 0LL;
    unsigned long long u64 = 0ULL;
    int x = 41;
    int y = 7;
    gsl_not_null_i32 nn;
    gsl_not_null_i32 nn2;
    gsl_not_null_void nnv;
    int *got_ptr = (int *)0;
    void *got_void = (void *)0;
    int *raw = (int *)0;
    gsl_span_i32 sp;
    gsl_span_i32 empty;
    gsl_span_i32 sub;
    gsl_span_i32 sub2;
    size_t k;

    /* ---- A: narrow accepts every representable boundary ---------------- */
    err.clear();
    ga_check("ga_n_i8_min",
             gsl_narrow_i64_i8(-128LL, &i8, &err) == STDLIB_OK && i8 == -128 && err.ok());
    err.clear();
    ga_check("ga_n_i8_max",
             gsl_narrow_i64_i8(127LL, &i8, &err) == STDLIB_OK && i8 == 127 && err.ok());
    err.clear();
    ga_check("ga_n_u8_max",
             gsl_narrow_i64_u8(255LL, &u8, &err) == STDLIB_OK && u8 == 255u && err.ok());
    err.clear();
    ga_check("ga_n_i16_min",
             gsl_narrow_i64_i16(-32768LL, &i16, &err) == STDLIB_OK && i16 == -32768 && err.ok());
    err.clear();
    ga_check("ga_n_u16_max",
             gsl_narrow_i64_u16(65535LL, &u16, &err) == STDLIB_OK && u16 == 65535u && err.ok());
    err.clear();
    ga_check("ga_n_i32_min",
             gsl_narrow_i64_i32(-2147483647LL - 1LL, &i32, &err) == STDLIB_OK &&
                 i32 == INT_MIN && err.ok());
    err.clear();
    ga_check("ga_n_u32_max",
             gsl_narrow_i64_u32(4294967295LL, &u32, &err) == STDLIB_OK &&
                 u32 == UINT_MAX && err.ok());
    err.clear();
    ga_check("ga_n_i64_min",
             gsl_narrow_i64_i64(-9223372036854775807LL - 1LL, &i64, &err) == STDLIB_OK &&
                 i64 == LLONG_MIN && err.ok());
    err.clear();
    ga_check("ga_n_u64_max_signed",
             gsl_narrow_i64_u64(LLONG_MAX, &u64, &err) == STDLIB_OK &&
                 u64 == (unsigned long long)LLONG_MAX && err.ok());
    err.clear();
    ga_check("ga_n_u64_i64_max",
             gsl_narrow_u64_i64((unsigned long long)LLONG_MAX, &i64, &err) == STDLIB_OK &&
                 i64 == LLONG_MAX && err.ok());
    err.clear();
    ga_check("ga_n_u64_u8_max",
             gsl_narrow_u64_u8(255ULL, &u8, &err) == STDLIB_OK && u8 == 255u && err.ok());
    err.clear();
    ga_check("ga_n_u64_u32_max",
             gsl_narrow_u64_u32((unsigned long long)UINT_MAX, &u32, &err) == STDLIB_OK &&
                 u32 == UINT_MAX && err.ok());
    err.clear();
    ga_check("ga_n_zero", gsl_narrow_i64_i32(0LL, &i32, &err) == STDLIB_OK && i32 == 0);

    /* ---- B: narrow refuses; every refusal is fail-closed --------------- */
    err.clear();
    ga_check("ga_r_i8_high",
             gsl_narrow_i64_i8(128LL, &i8, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_i8_low",
             gsl_narrow_i64_i8(-129LL, &i8, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u8_negative",
             gsl_narrow_i64_u8(-1LL, &u8, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u8_high",
             gsl_narrow_i64_u8(256LL, &u8, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_i16_high",
             gsl_narrow_i64_i16(32768LL, &i16, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u16_negative",
             gsl_narrow_i64_u16(-1LL, &u16, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_i32_high",
             gsl_narrow_i64_i32(2147483648LL, &i32, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u32_sign",
             gsl_narrow_i64_u32(-1LL, &u32, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u32_high",
             gsl_narrow_i64_u32(4294967296LL, &u32, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u64_sign",
             gsl_narrow_i64_u64(-1LL, &u64, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u64_i64_high",
             gsl_narrow_u64_i64(UINT64_MAX, &i64, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u64_u8_high",
             gsl_narrow_u64_u8(256ULL, &u8, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_u64_u32_high",
             gsl_narrow_u64_u32((unsigned long long)UINT_MAX + 1ULL, &u32, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_RANGE);
    err.clear();
    ga_check("ga_r_null_out",
             gsl_narrow_i64_i32(1LL, (int *)0, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_INVALID);
    /* out is untouched on refusal */
    i32 = 1234;
    err.clear();
    (void)gsl_narrow_i64_i32(LLONG_MAX, &i32, &err);
    ga_check("ga_r_out_untouched", i32 == 1234);

    /* ---- C: not_null port ---------------------------------------------- */
    err.clear();
    ga_check("ga_nn_make", gsl_not_null_i32_make(&x, &nn, &err) == STDLIB_OK && err.ok());
    err.clear();
    ga_check("ga_nn_get",
             gsl_not_null_i32_get(&nn, &got_ptr, &err) == STDLIB_OK && got_ptr == &x);
    err.clear();
    ga_check("ga_nn_make_null",
             gsl_not_null_i32_make((int *)0, &nn2, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_INVALID);
    err.clear();
    ga_check("ga_nn_make_null_out",
             gsl_not_null_i32_make(&x, (gsl_not_null_i32 *)0, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_INVALID);
    nn2.ptr = (int *)0; /* simulate a caller bypassing the factory */
    err.clear();
    ga_check("ga_nn_get_recheck",
             gsl_not_null_i32_get(&nn2, &got_ptr, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_INVALID);
    gsl_not_null_i32_swap(&nn, &nn2);
    err.clear();
    ga_check("ga_nn_swap",
             gsl_not_null_i32_get(&nn, &got_ptr, &err) == STDLIB_FAIL &&
                 gsl_not_null_i32_get(&nn2, &got_ptr, &err) == STDLIB_OK);
    /* restore for later checks */
    gsl_not_null_i32_make(&x, &nn, &err);
    err.clear();
    ga_check("ga_nn_void_make", gsl_not_null_void_make((void *)&y, &nnv, &err) == STDLIB_OK);
    err.clear();
    ga_check("ga_nn_void_get",
             gsl_not_null_void_get(&nnv, &got_void, &err) == STDLIB_OK && got_void == (void *)&y);
    err.clear();
    ga_check("ga_nn_void_null",
             gsl_not_null_void_make((void *)0, &nnv, &err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_INVALID);
    ga_check("ga_nn_zero_overhead", sizeof(gsl_not_null_i32) == sizeof(int *));

    /* ---- D: span port --------------------------------------------------- */
    ga_check("ga_abi_pointer_size", sizeof(int *) == sizeof(void *));
    raw = (int *)stdlib_alloc(8u * sizeof(int));
    ga_check("ga_sp_alloc", raw != (int *)0);
    if (raw != (int *)0) {
        for (k = 0u; k < 8u; k++) raw[k] = (int)(10u + k);

        err.clear();
        ga_check("ga_sp_make",
                 gsl_span_i32_make(raw, 8u, &sp, &err) == STDLIB_OK && err.ok());
        ga_check("ga_sp_size", gsl_span_i32_size(&sp) == 8u);
        ga_check("ga_sp_size_bytes", gsl_span_i32_size_bytes(&sp) == 8u * sizeof(int));
        ga_check("ga_sp_empty_no", gsl_span_i32_empty(&sp) == 0);
        ga_check("ga_sp_data", gsl_span_i32_data(&sp) == raw);

        err.clear();
        ga_check("ga_sp_make_null_zero_ok",
                 gsl_span_i32_make((int *)0, 0u, &empty, &err) == STDLIB_OK &&
                     gsl_span_i32_empty(&empty) == 1);
        err.clear();
        ga_check("ga_sp_make_null_nonzero",
                 gsl_span_i32_make((int *)0, 4u, &empty, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_INVALID);
        err.clear();
        ga_check("ga_sp_make_untracked",
                 gsl_span_i32_make(&x, 1u, &empty, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_RANGE);
        err.clear();
        ga_check("ga_sp_make_overflow_count",
                 gsl_span_i32_make(raw, (SIZE_MAX / sizeof(int)) + 1u, &empty, &err) ==
                         STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_RANGE);
        err.clear();
        ga_check("ga_sp_make_too_big",
                 gsl_span_i32_make(raw, 9u, &empty, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_RANGE);

        for (k = 0u; k < 8u; k++) {
            err.clear();
            (void)gsl_span_i32_at(&sp, k, &i32, &err);
            ga_check(k == 0u ? "ga_sp_at_0" : (k == 7u ? "ga_sp_at_7" : "ga_sp_at_mid"),
                     i32 == (int)(10u + k) && err.ok());
        }
        err.clear();
        ga_check("ga_sp_at_end_refused",
                 gsl_span_i32_at(&sp, 8u, &i32, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);
        err.clear();
        ga_check("ga_sp_at_huge_refused",
                 gsl_span_i32_at(&sp, SIZE_MAX, &i32, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);
        err.clear();
        ga_check("ga_sp_at_empty_refused",
                 gsl_span_i32_at(&empty, 0u, &i32, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);
        err.clear();
        ga_check("ga_sp_at_null_out",
                 gsl_span_i32_at(&sp, 0u, (int *)0, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_INVALID);
        /* no dereference on refusal: i32 keeps the last good value (17) */
        ga_check("ga_sp_refusal_no_write", i32 == 17);

        err.clear();
        ga_check("ga_sp_first_ok",
                 gsl_span_i32_first(&sp, 3u, &sub, &err) == STDLIB_OK &&
                     gsl_span_i32_size(&sub) == 3u && gsl_span_i32_data(&sub) == raw);
        err.clear();
        (void)gsl_span_i32_at(&sub, 2u, &i32, &err);
        ga_check("ga_sp_first_read", i32 == 12 && err.ok());
        err.clear();
        ga_check("ga_sp_first_at_end_refused",
                 gsl_span_i32_at(&sub, 3u, &i32, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);
        err.clear();
        ga_check("ga_sp_first_too_big",
                 gsl_span_i32_first(&sp, 9u, &sub, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);

        err.clear();
        ga_check("ga_sp_last_ok",
                 gsl_span_i32_last(&sp, 3u, &sub, &err) == STDLIB_OK &&
                     gsl_span_i32_size(&sub) == 3u);
        err.clear();
        (void)gsl_span_i32_at(&sub, 0u, &i32, &err);
        ga_check("ga_sp_last_read", i32 == 15 && err.ok());
        err.clear();
        ga_check("ga_sp_last_too_big",
                 gsl_span_i32_last(&sp, 9u, &sub, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);

        err.clear();
        ga_check("ga_sp_subspan_ok",
                 gsl_span_i32_subspan(&sp, 2u, 4u, 0, &sub, &err) == STDLIB_OK &&
                     gsl_span_i32_size(&sub) == 4u);
        err.clear();
        (void)gsl_span_i32_at(&sub, 0u, &i32, &err);
        ga_check("ga_sp_subspan_read0", i32 == 12 && err.ok());
        err.clear();
        (void)gsl_span_i32_at(&sub, 3u, &i32, &err);
        ga_check("ga_sp_subspan_read3", i32 == 15 && err.ok());
        err.clear();
        ga_check("ga_sp_subspan_all",
                 gsl_span_i32_subspan(&sp, 2u, 0u, 1, &sub, &err) == STDLIB_OK &&
                     gsl_span_i32_size(&sub) == 6u);
        err.clear();
        ga_check("ga_sp_subspan_offset_oob",
                 gsl_span_i32_subspan(&sp, 9u, 0u, 1, &sub, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);
        err.clear();
        ga_check("ga_sp_subspan_count_oob",
                 gsl_span_i32_subspan(&sp, 2u, 7u, 0, &sub, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);

        /* nested subspan: provenance fields must compose */
        err.clear();
        ga_check("ga_sp_nested_make",
                 gsl_span_i32_subspan(&sp, 2u, 4u, 0, &sub, &err) == STDLIB_OK &&
                     gsl_span_i32_subspan(&sub, 1u, 2u, 0, &sub2, &err) == STDLIB_OK);
        err.clear();
        (void)gsl_span_i32_at(&sub2, 0u, &i32, &err);
        ga_check("ga_sp_nested_read", i32 == 13 && err.ok());
        err.clear();
        ga_check("ga_sp_nested_end_refused",
                 gsl_span_i32_at(&sub2, 2u, &i32, &err) == STDLIB_FAIL &&
                     err.category() == STDLIB_CATEGORY_BOUNDS);

        stdlib_release(raw);
        ga_check("ga_sp_release_live", stdlib_state.meta_live == 0u ||
                                           stdlib_alloc_size(raw) == 0u);
    }

    print("GSL_ADAPT", ga_checks, ga_failures);
    return ga_failures == 0 ? 0 : 1;
}
