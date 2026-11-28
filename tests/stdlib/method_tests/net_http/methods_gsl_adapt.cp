/* 100% public-method test for stdlib/gsl_adapt.hp.
 *
 * Enumerates every callable in the header: 3 fail-closed refusal helpers,
 * 11 narrow width pairs, 5 not_null helpers, 10 span helpers.  Each method has
 * one deterministic case that calls it and verifies success and, where the API
 * defines one, its refusal path.  Prints "<header> <method> PASS|FAIL" once per
 * method; exits non-zero iff any method fails.  No network, stdin or clock.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/gsl_adapt.hp"

static int mt_total = 0;
static int mt_passed = 0;
static int mt_failed = 0;

static void mt_report(const char *method, int ok) {
    mt_total++;
    if (ok) mt_passed++; else mt_failed++;
    print("gsl_adapt.hp", method, ok ? "PASS" : "FAIL");
}

int main(void) {
    error err = error();
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
    gsl_span_i32 view;
    gsl_span_i32 sub;
    gsl_span_i32 sub2;
    size_t k;
    int ok;

    /* ---- 1-3: shared fail-closed refusal helpers ------------------------ */
    error_reset(&err);
    ok = gsl_adapt_refuse_null(&err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID &&
         err.code() == CPLUS_EXIT_NULL_POINTER;
    mt_report("gsl_adapt_refuse_null", ok);

    error_reset(&err);
    ok = gsl_adapt_refuse_range(&err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE &&
         err.code() == CPLUS_EXIT_TYPE_CONFUSION;
    mt_report("gsl_adapt_refuse_range", ok);

    error_reset(&err);
    ok = gsl_adapt_refuse_bounds(&err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_BOUNDS &&
         err.code() == CPLUS_EXIT_BOUNDS;
    mt_report("gsl_adapt_refuse_bounds", ok);

    /* ---- 4-14: narrow (representable OK + out-of-range FAIL) ------------ */
    error_reset(&err);
    ok = gsl_narrow_i64_i8(-128LL, &i8, &err) == STDLIB_OK && i8 == -128 && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_i64_i8(128LL, &i8, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_i64_i8", ok);

    error_reset(&err);
    ok = gsl_narrow_i64_u8(255LL, &u8, &err) == STDLIB_OK && u8 == 255u && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_i64_u8(-1LL, &u8, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_i64_u8", ok);

    error_reset(&err);
    ok = gsl_narrow_i64_i16(-32768LL, &i16, &err) == STDLIB_OK &&
         i16 == -32768 && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_i64_i16(32768LL, &i16, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_i64_i16", ok);

    error_reset(&err);
    ok = gsl_narrow_i64_u16(65535LL, &u16, &err) == STDLIB_OK &&
         u16 == 65535u && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_i64_u16(-1LL, &u16, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_i64_u16", ok);

    error_reset(&err);
    ok = gsl_narrow_i64_i32(-2147483647LL - 1LL, &i32, &err) == STDLIB_OK &&
         i32 == INT_MIN && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_i64_i32(2147483648LL, &i32, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    error_reset(&err);
    ok = ok && gsl_narrow_i64_i32(1LL, (int *)0, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    mt_report("gsl_narrow_i64_i32", ok);

    error_reset(&err);
    ok = gsl_narrow_i64_u32(4294967295LL, &u32, &err) == STDLIB_OK &&
         u32 == UINT_MAX && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_i64_u32(4294967296LL, &u32, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_i64_u32", ok);

    error_reset(&err);
    ok = gsl_narrow_i64_i64(-9223372036854775807LL - 1LL, &i64, &err) == STDLIB_OK &&
         i64 == LLONG_MIN && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_i64_i64(9223372036854775807LL, &i64, &err) == STDLIB_OK &&
         i64 == LLONG_MAX;
    mt_report("gsl_narrow_i64_i64", ok);

    error_reset(&err);
    ok = gsl_narrow_i64_u64(9223372036854775807LL, &u64, &err) == STDLIB_OK &&
         u64 == (unsigned long long)LLONG_MAX && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_i64_u64(-1LL, &u64, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_i64_u64", ok);

    error_reset(&err);
    ok = gsl_narrow_u64_i64((unsigned long long)LLONG_MAX, &i64, &err) == STDLIB_OK &&
         i64 == LLONG_MAX && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_u64_i64(0xFFFFFFFFFFFFFFFFULL, &i64, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_u64_i64", ok);

    error_reset(&err);
    ok = gsl_narrow_u64_u8(255ULL, &u8, &err) == STDLIB_OK && u8 == 255u && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_u64_u8(256ULL, &u8, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_u64_u8", ok);

    error_reset(&err);
    ok = gsl_narrow_u64_u32((unsigned long long)UINT_MAX, &u32, &err) == STDLIB_OK &&
         u32 == UINT_MAX && err.ok();
    error_reset(&err);
    ok = ok && gsl_narrow_u64_u32((unsigned long long)UINT_MAX + 1ULL, &u32, &err) ==
                   STDLIB_FAIL && err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_narrow_u64_u32", ok);

    /* ---- 15-19: not_null ------------------------------------------------ */
    error_reset(&err);
    ok = gsl_not_null_i32_make(&x, &nn, &err) == STDLIB_OK && err.ok() &&
         nn.ptr == &x;
    error_reset(&err);
    ok = ok && gsl_not_null_i32_make((int *)0, &nn2, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    error_reset(&err);
    ok = ok && gsl_not_null_i32_make(&x, (gsl_not_null_i32 *)0, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    mt_report("gsl_not_null_i32_make", ok);

    error_reset(&err);
    ok = gsl_not_null_i32_get(&nn, &got_ptr, &err) == STDLIB_OK && err.ok() &&
         got_ptr == &x;
    nn2.ptr = (int *)0; /* bypass the factory: get() must re-assert invariant */
    error_reset(&err);
    ok = ok && gsl_not_null_i32_get(&nn2, &got_ptr, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    error_reset(&err);
    ok = ok && gsl_not_null_i32_get((const gsl_not_null_i32 *)0, &got_ptr, &err) ==
                   STDLIB_FAIL && err.category() == STDLIB_CATEGORY_INVALID;
    mt_report("gsl_not_null_i32_get", ok);

    nn.ptr = &x;
    gsl_not_null_i32_swap(&nn, &nn2);
    error_reset(&err);
    ok = nn.ptr == (int *)0 && nn2.ptr == &x;
    error_reset(&err);
    ok = ok && gsl_not_null_i32_get(&nn2, &got_ptr, &err) == STDLIB_OK &&
         got_ptr == &x;
    gsl_not_null_i32_swap((gsl_not_null_i32 *)0, &nn);
    ok = ok && nn.ptr == (int *)0 && nn2.ptr == &x; /* NULL operand is a no-op */
    mt_report("gsl_not_null_i32_swap", ok);

    error_reset(&err);
    ok = gsl_not_null_void_make((void *)&y, &nnv, &err) == STDLIB_OK && err.ok() &&
         nnv.ptr == (void *)&y;
    error_reset(&err);
    ok = ok && gsl_not_null_void_make((void *)0, &nnv, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    mt_report("gsl_not_null_void_make", ok);

    error_reset(&err);
    ok = gsl_not_null_void_make((void *)&y, &nnv, &err) == STDLIB_OK;
    ok = ok && gsl_not_null_void_get(&nnv, &got_void, &err) == STDLIB_OK &&
         err.ok() && got_void == (void *)&y;
    nnv.ptr = (void *)0;
    error_reset(&err);
    ok = ok && gsl_not_null_void_get(&nnv, &got_void, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    mt_report("gsl_not_null_void_get", ok);

    /* ---- 20-29: span ---------------------------------------------------- */
    raw = (int *)stdlib_alloc(8u * sizeof(int));
    ok = raw != (int *)0;
    if (raw != (int *)0) {
        for (k = 0u; k < 8u; k++) raw[k] = (int)(10u + k);
    }

    error_reset(&err);
    ok = ok && gsl_span_i32_view((void *)raw, 2u, 3u, &view, &err) == STDLIB_OK &&
         err.ok() && view.base == raw + 2 && view.count == 3u &&
         view.owner == (void *)raw && view.off_elems == 2u;
    error_reset(&err);
    ok = ok && gsl_span_i32_view((void *)0, 0u, 1u, &view, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    error_reset(&err);
    ok = ok && gsl_span_i32_view((void *)raw, 6u, 3u, &view, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_span_i32_view", ok);

    error_reset(&err);
    ok = ok && gsl_span_i32_make(raw, 8u, &sp, &err) == STDLIB_OK && err.ok() &&
         sp.base == raw && sp.count == 8u;
    error_reset(&err);
    ok = ok && gsl_span_i32_make((int *)0, 0u, &empty, &err) == STDLIB_OK &&
         gsl_span_i32_empty(&empty) == 1;
    error_reset(&err);
    ok = ok && gsl_span_i32_make((int *)0, 4u, &empty, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    error_reset(&err);
    ok = ok && gsl_span_i32_make(&x, 1u, &empty, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    error_reset(&err);
    ok = ok && gsl_span_i32_make(raw, 9u, &empty, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_RANGE;
    mt_report("gsl_span_i32_make", ok);

    ok = gsl_span_i32_size(&sp) == 8u && gsl_span_i32_size(&empty) == 0u &&
         gsl_span_i32_size((const gsl_span_i32 *)0) == 0u;
    mt_report("gsl_span_i32_size", ok);

    ok = gsl_span_i32_size_bytes(&sp) == 8u * sizeof(int) &&
         gsl_span_i32_size_bytes(&empty) == 0u;
    mt_report("gsl_span_i32_size_bytes", ok);

    ok = gsl_span_i32_empty(&sp) == 0 && gsl_span_i32_empty(&empty) == 1 &&
         gsl_span_i32_empty((const gsl_span_i32 *)0) == 1;
    mt_report("gsl_span_i32_empty", ok);

    ok = gsl_span_i32_data(&sp) == raw && gsl_span_i32_data(&empty) == (int *)0 &&
         gsl_span_i32_data((const gsl_span_i32 *)0) == (int *)0;
    mt_report("gsl_span_i32_data", ok);

    ok = 1;
    for (k = 0u; k < 8u; k++) {
        error_reset(&err);
        ok = ok && gsl_span_i32_at(&sp, k, &i32, &err) == STDLIB_OK && err.ok() &&
             i32 == (int)(10u + k);
    }
    error_reset(&err);
    ok = ok && gsl_span_i32_at(&sp, 8u, &i32, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_BOUNDS;
    error_reset(&err);
    ok = ok && gsl_span_i32_at(&sp, 0u, (int *)0, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    mt_report("gsl_span_i32_at", ok);

    error_reset(&err);
    ok = gsl_span_i32_first(&sp, 3u, &sub, &err) == STDLIB_OK && err.ok() &&
         gsl_span_i32_size(&sub) == 3u && gsl_span_i32_data(&sub) == raw;
    error_reset(&err);
    ok = ok && gsl_span_i32_first(&sp, 9u, &sub, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_BOUNDS;
    error_reset(&err);
    (void)gsl_span_i32_at(&sub, 2u, &i32, &err);
    ok = ok && i32 == 12 && err.ok();
    mt_report("gsl_span_i32_first", ok);

    error_reset(&err);
    ok = gsl_span_i32_last(&sp, 3u, &sub, &err) == STDLIB_OK && err.ok() &&
         gsl_span_i32_size(&sub) == 3u;
    error_reset(&err);
    (void)gsl_span_i32_at(&sub, 0u, &i32, &err);
    ok = ok && i32 == 15 && err.ok();
    error_reset(&err);
    ok = ok && gsl_span_i32_last(&sp, 9u, &sub, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_BOUNDS;
    mt_report("gsl_span_i32_last", ok);

    error_reset(&err);
    ok = gsl_span_i32_subspan(&sp, 2u, 4u, 0, &sub, &err) == STDLIB_OK && err.ok() &&
         gsl_span_i32_size(&sub) == 4u;
    error_reset(&err);
    (void)gsl_span_i32_at(&sub, 0u, &i32, &err);
    ok = ok && i32 == 12 && err.ok();
    error_reset(&err);
    ok = ok && gsl_span_i32_subspan(&sp, 2u, 0u, 1, &sub, &err) == STDLIB_OK &&
         gsl_span_i32_size(&sub) == 6u;
    error_reset(&err);
    ok = ok && gsl_span_i32_subspan(&sp, 9u, 0u, 1, &sub, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_BOUNDS;
    error_reset(&err);
    ok = ok && gsl_span_i32_subspan(&sp, 2u, 7u, 0, &sub, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_BOUNDS;
    error_reset(&err);
    ok = ok && gsl_span_i32_subspan(&sp, 2u, 4u, 0, &sub, &err) == STDLIB_OK &&
         gsl_span_i32_subspan(&sub, 1u, 2u, 0, &sub2, &err) == STDLIB_OK;
    error_reset(&err);
    (void)gsl_span_i32_at(&sub2, 0u, &i32, &err);
    ok = ok && i32 == 13 && err.ok();
    mt_report("gsl_span_i32_subspan", ok);

    stdlib_release(raw);

    print("METHOD_TEST gsl_adapt.hp", mt_total, mt_passed, mt_failed, 0);
    return mt_failed != 0;
}
