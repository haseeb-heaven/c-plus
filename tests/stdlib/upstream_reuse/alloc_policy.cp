/* Self-checking tests for the additive mimalloc-derived allocation-policy
 * helpers in stdlib/alloc_policy.hp.
 *
 * All values are computed from SIZE_MAX / sizeof(uintptr_t); no memory is
 * allocated and no system limit is exhausted.  Each check prints "<tag> <0|1>"
 * and the program prints "ALLOC_POLICY <checks>" /
 * "ALLOC_POLICY_FAILURES <failures>" and exits non-zero on any failure.
 *
 * NOTE: tests/stdlib/run_stdlib_tests.sh only globs top-level .cp tests, so
 * this subdirectory test is built and run explicitly (see alloc_policy_NOTES.md).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/alloc_policy.hp"

static int ap_checks = 0;
static int ap_failed = 0;

static void ap_chk(const char *tag, int cond) {
    ap_checks++;
    if (!cond)
        ap_failed++;
    print(tag, cond ? 1 : 0);
}

/* mi_count_size_overflow parity: quick path, overflow boundaries, SIZE_MAX,
 * exact fits, zero operands and a rejected NULL out-parameter. */
static void ap_count_size(void) {
    size_t total = (size_t)0;
    size_t sentinel = (size_t)12345;

    ap_chk("cs_one_sizemax",
           stdlib_count_size_overflow((size_t)1, SIZE_MAX, &total) == 0 &&
               total == SIZE_MAX);
    ap_chk("cs_one_zero",
           stdlib_count_size_overflow((size_t)1, (size_t)0, &total) == 0 &&
               total == (size_t)0);
    ap_chk("cs_zero_count",
           stdlib_count_size_overflow((size_t)0, SIZE_MAX, &total) == 0 &&
               total == (size_t)0);
    ap_chk("cs_zero_elem",
           stdlib_count_size_overflow(SIZE_MAX, (size_t)0, &total) == 0 &&
               total == (size_t)0);
    ap_chk("cs_sizemax_x2",
           stdlib_count_size_overflow(SIZE_MAX, (size_t)2, &total) == 1 &&
               total == SIZE_MAX);
    ap_chk("cs_2_xsizemax",
           stdlib_count_size_overflow((size_t)2, SIZE_MAX, &total) == 1 &&
               total == SIZE_MAX);
    ap_chk("cs_halfplus_x2",
           stdlib_count_size_overflow((SIZE_MAX / (size_t)2) + (size_t)1,
                                      (size_t)2, &total) == 1 &&
               total == SIZE_MAX);
    ap_chk("cs_half_x2",
           stdlib_count_size_overflow(SIZE_MAX / (size_t)2, (size_t)2,
                                      &total) == 0 &&
               total == SIZE_MAX - (size_t)1);
    ap_chk("cs_max_xmax",
           stdlib_count_size_overflow(SIZE_MAX, SIZE_MAX, &total) == 1 &&
               total == SIZE_MAX);
    ap_chk("cs_quarterplus_x4",
           stdlib_count_size_overflow((SIZE_MAX / (size_t)4) + (size_t)1,
                                      (size_t)4, &total) == 1 &&
               total == SIZE_MAX);
    ap_chk("cs_quarter_x4",
           stdlib_count_size_overflow(SIZE_MAX / (size_t)4, (size_t)4,
                                      &total) == 0 &&
               total == SIZE_MAX - (size_t)3);
    ap_chk("cs_exact_4096",
           stdlib_count_size_overflow((size_t)1024, (size_t)4, &total) == 0 &&
               total == (size_t)4096);
    ap_chk("cs_exact_21",
           stdlib_count_size_overflow((size_t)3, (size_t)7, &total) == 0 &&
               total == (size_t)21);
    total = sentinel;
    ap_chk("cs_null_out",
           stdlib_count_size_overflow((size_t)3, (size_t)7, NULL) == 1 &&
               total == sentinel);
}

/* _mi_align_up parity for power-of-two alignments, plus explicit rejection
 * of invalid alignments and of a rounded value that would wrap. */
static void ap_round_up(void) {
    size_t out = (size_t)0;
    size_t sentinel = (size_t)12345;

    ap_chk("ru_zero",
           stdlib_size_class_round_up((size_t)0, (size_t)8, &out) == 0 &&
               out == (size_t)0);
    ap_chk("ru_one",
           stdlib_size_class_round_up((size_t)1, (size_t)8, &out) == 0 &&
               out == (size_t)8);
    ap_chk("ru_exact",
           stdlib_size_class_round_up((size_t)8, (size_t)8, &out) == 0 &&
               out == (size_t)8);
    ap_chk("ru_plus_one",
           stdlib_size_class_round_up((size_t)9, (size_t)8, &out) == 0 &&
               out == (size_t)16);
    ap_chk("ru_21_to_24",
           stdlib_size_class_round_up((size_t)21, (size_t)8, &out) == 0 &&
               out == (size_t)24);
    out = sentinel;
    ap_chk("ru_sizemax_wrap",
           stdlib_size_class_round_up(SIZE_MAX, (size_t)8, &out) == -1 &&
               out == sentinel);
    out = sentinel;
    ap_chk("ru_non_pow2",
           stdlib_size_class_round_up((size_t)17, (size_t)3, &out) == -1 &&
               out == sentinel);
    out = sentinel;
    ap_chk("ru_zero_align",
           stdlib_size_class_round_up((size_t)17, (size_t)0, &out) == -1 &&
               out == sentinel);
    out = sentinel;
    ap_chk("ru_null_out",
           stdlib_size_class_round_up((size_t)17, (size_t)8, NULL) == -1 &&
               out == sentinel);
}

/* _mi_wsize_from_size parity with an explicit wrap guard. */
static void ap_words(void) {
    size_t out = (size_t)0;
    size_t sentinel = (size_t)12345;

    ap_chk("w_zero",
           stdlib_size_class_words((size_t)0, &out) == 0 && out == (size_t)0);
    ap_chk("w_one",
           stdlib_size_class_words((size_t)1, &out) == 0 && out == (size_t)1);
    ap_chk("w_exact_word",
           stdlib_size_class_words(sizeof(uintptr_t), &out) == 0 &&
               out == (size_t)1);
    ap_chk("w_word_plus_one",
           stdlib_size_class_words(sizeof(uintptr_t) + (size_t)1, &out) == 0 &&
               out == (size_t)2);
    out = sentinel;
    ap_chk("w_sizemax_wrap",
           stdlib_size_class_words(SIZE_MAX, &out) == -1 && out == sentinel);
    ap_chk("w_null_out", stdlib_size_class_words((size_t)1, NULL) == -1);
}

/* Zero-size normalization: mimalloc's sizeof(void*) rule and this repo's 1u
 * rule are both caller-selectable; non-zero sizes pass through unchanged. */
static void ap_normalize(void) {
    size_t out = (size_t)0;
    size_t sentinel = (size_t)12345;

    ap_chk("nz_zero_ptr",
           stdlib_size_normalize_zero((size_t)0, sizeof(void *), &out) == 0 &&
               out == sizeof(void *));
    ap_chk("nz_zero_one",
           stdlib_size_normalize_zero((size_t)0, (size_t)1, &out) == 0 &&
               out == (size_t)1);
    ap_chk("nz_keep",
           stdlib_size_normalize_zero((size_t)7, sizeof(void *), &out) == 0 &&
               out == (size_t)7);
    ap_chk("nz_max_keep",
           stdlib_size_normalize_zero(SIZE_MAX, (size_t)1, &out) == 0 &&
               out == SIZE_MAX);
    out = sentinel;
    ap_chk("nz_null_out",
           stdlib_size_normalize_zero((size_t)0, (size_t)1, NULL) == -1 &&
               out == sentinel);
}

/* Compose the primitives the way a checked allocation-size policy would:
 * count*size -> zero normalization -> word rounding -> alignment rounding,
 * with every failure short-circuiting before the next multiply. */
static void ap_pipeline(void) {
    size_t total = (size_t)0;
    size_t norm = (size_t)0;
    size_t words = (size_t)0;
    size_t padded = (size_t)0;
    size_t out = (size_t)0;
    size_t bad = (SIZE_MAX / (size_t)4) + (size_t)1;
    size_t sentinel = (size_t)12345;

    ap_chk("pipe_reject_before_rounding",
           stdlib_count_size_overflow(bad, (size_t)4, &total) == 1 &&
               total == SIZE_MAX);
    ap_chk("pipe_total",
           stdlib_count_size_overflow((size_t)3, (size_t)7, &total) == 0 &&
               total == (size_t)21);
    ap_chk("pipe_norm",
           stdlib_size_normalize_zero(total, sizeof(void *), &norm) == 0 &&
               norm == (size_t)21);
    ap_chk("pipe_words",
           stdlib_size_class_words(norm, &words) == 0 && words == (size_t)3);
    ap_chk("pipe_word_bytes",
           stdlib_size_class_round_up(words * sizeof(uintptr_t),
                                      sizeof(uintptr_t), &padded) == 0 &&
               padded == (size_t)3 * sizeof(uintptr_t));
    ap_chk("pipe_align8",
           stdlib_size_class_round_up(padded, (size_t)8, &out) == 0 &&
               out == padded);
    ap_chk("pipe_boundary_total",
           stdlib_count_size_overflow(SIZE_MAX / (size_t)2, (size_t)2,
                                      &total) == 0 &&
               total == SIZE_MAX - (size_t)1);
    words = sentinel;
    ap_chk("pipe_boundary_words",
           stdlib_size_class_words(total, &words) == -1 && words == sentinel);
}

int main(void) {
    ap_count_size();
    ap_round_up();
    ap_words();
    ap_normalize();
    ap_pipeline();
    print("ALLOC_POLICY", ap_checks);
    print("ALLOC_POLICY_FAILURES", ap_failed);
    return ap_failed == 0 ? 0 : 1;
}
