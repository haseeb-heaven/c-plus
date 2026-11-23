/*
 * test_p0_alloc_contract.c
 *
 * P0 regression tests for the Safe CRT allocation APIs
 * (stdlib/safe/safe_alloc.h) plus the pointer-to-pointer cleanup helper from
 * the hardened libc (libc/install-include/cplus_libc.h).
 *
 * Coverage:
 *   - calloc_s(8,16): zeroing plus byte_size / elem_size / elem_count /
 *     generation / query_length metadata.
 *   - realloc_s: injected failure preserves the original block (identity,
 *     size, bytes, generation); success invalidates the old identity.
 *   - free_s / free_checked_s: NULL no-op, double free and invalid free
 *     (stack, interior, raw malloc) are refused, not executed.
 *   - zero-size allocation (malloc_s(0), calloc_s(0,0), calloc_s(4,0)).
 *   - multiplication overflow (EOVERFLOW) and over-limit sizes (EINVAL /
 *     ENOMEM).
 *   - allocation-failure injection (fail_after / fail_reset / seq).
 *   - pointer-to-pointer cleanup: cplus_safe_transfer_ownership and
 *     cplus_free_checked(&ptr) null the caller's pointer.
 *
 * Output: one line `<tag> <0|1>` per check, then
 *   P0_ALLOC <checks> <failures>
 * Exit status is 0 only when failures == 0.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"
#include "cplus_libc.h"

static int g_checks = 0;
static int g_failures = 0;

static void ck(const char *tag, int ok) {
    ++g_checks;
    if (!ok) ++g_failures;
    printf("%s %d\n", tag, ok ? 1 : 0);
}

#define CHECK(tag, expr) ck((tag), (expr) ? 1 : 0)

static int all_zero_bytes(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != 0) return 0;
    return 1;
}

static int all_bytes_byte(const unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != v) return 0;
    return 1;
}

/* ---- calloc_s(8,16) capacity via allocator metadata --------------------- */
static void test_calloc_capacity(void) {
    unsigned char *p = (unsigned char *)calloc_s(8, 16);
    cplus_safe_alloc_meta m;
    size_t cnt = 0;

    CHECK("calloc_8x16_nonnull", p != NULL);
    if (p == NULL) return;

    CHECK("calloc_8x16_zeroed", all_zero_bytes(p, 128));
    CHECK("calloc_8x16_ptr_size", cplus_safe_ptr_size(p) == 128);
    CHECK("calloc_8x16_elem_size", cplus_safe_ptr_elem_size(p) == 16);
    CHECK("calloc_8x16_count", cplus_safe_ptr_count(p) == 8);
    CHECK("calloc_8x16_is_live", cplus_safe_ptr_is_live(p) == 1);
    CHECK("calloc_8x16_query_length",
          cplus_safe_query_length(p, 16, &cnt) == CPLUS_SAFE_OK && cnt == 8);
    CHECK("calloc_8x16_get_meta",
          cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK);
    CHECK("calloc_8x16_meta_fields",
          m.byte_size == 128 && m.elem_size == 16 && m.elem_count == 8
          && m.is_live == 1 && m.generation > 0);
    CHECK("calloc_8x16_len_macro", cplus_safe_len(p) == 128);
    p[0] = 0x11;
    p[127] = 0x22;
    CHECK("calloc_8x16_writable", p[0] == 0x11 && p[127] == 0x22);
    free_s(p);
}

/* ---- realloc failure preserves original; success invalidates it --------- */
static void test_realloc_contract(void) {
    unsigned char *p = (unsigned char *)malloc_s(16);
    unsigned char *q;
    cplus_safe_alloc_meta m;
    unsigned long long g0;
    size_t i;

    CHECK("realloc_setup", p != NULL);
    if (p == NULL) return;
    for (i = 0; i < 16; ++i) p[i] = 0x5A;
    CHECK("realloc_meta_before", cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK);
    g0 = m.generation;

    /* inject an allocation failure and try to grow the live block */
    cplus_safe_alloc_fail_reset();
    cplus_safe_alloc_fail_after(0);
    errno = 0;
    q = (unsigned char *)realloc_s(p, 64);
    CHECK("realloc_fail_null", q == NULL);
    CHECK("realloc_fail_errno", errno == ENOMEM);
    CHECK("realloc_fail_keeps_live", cplus_safe_ptr_is_live(p) == 1);
    CHECK("realloc_fail_keeps_size", cplus_safe_ptr_size(p) == 16);
    CHECK("realloc_fail_keeps_bytes", all_bytes_byte(p, 16, 0x5A));
    CHECK("realloc_fail_keeps_generation",
          cplus_safe_ptr_check_generation(p, g0) == 1);
    cplus_safe_alloc_fail_reset();

    /* successful realloc: old pointer is no longer a live identity */
    q = (unsigned char *)realloc_s(p, 64);
    CHECK("realloc_ok_nonnull", q != NULL);
    if (q != NULL) {
        CHECK("realloc_ok_old_not_live", cplus_safe_ptr_is_live(p) == 0);
        CHECK("realloc_ok_new_size", cplus_safe_ptr_size(q) == 64);
        CHECK("realloc_ok_preserves_bytes", all_bytes_byte(q, 16, 0x5A));
        CHECK("realloc_meta_after", cplus_safe_alloc_get_meta(q, &m) == CPLUS_SAFE_OK);
        CHECK("realloc_ok_generation_bumped", m.generation > g0);
        free_s(q);
    }

    /* realloc_s(NULL, n) is a fresh allocation */
    q = (unsigned char *)realloc_s(NULL, 24);
    CHECK("realloc_null_allocates", q != NULL);
    if (q != NULL) free_s(q);
}

/* ---- free_s double/invalid free refusal -------------------------------- */
static void test_free_refusal(void) {
    void *p = malloc_s(16);
    void *interior;
    void *raw;
    int stack_val = 42;
    size_t live_after_free;

    CHECK("free_setup", p != NULL);
    if (p == NULL) return;
    interior = (void *)((char *)p + 1); /* compute while the block is live */

    CHECK("free_null_noop", free_checked_s(NULL) == CPLUS_SAFE_OK);
    CHECK("free_valid_ok", free_checked_s(p) == CPLUS_SAFE_OK);
    CHECK("free_ptr_was_freed", cplus_safe_ptr_was_freed(p) == 1);
    live_after_free = cplus_safe_alloc_live_count();

    errno = 0;
    CHECK("free_double_refused", free_checked_s(p) == CPLUS_SAFE_EINVAL);
    CHECK("free_double_errno", errno == EINVAL);

    errno = 0;
    CHECK("free_stack_refused", free_checked_s(&stack_val) == CPLUS_SAFE_EINVAL);
    CHECK("free_stack_errno", errno == EINVAL);
    errno = 0;
    CHECK("free_interior_refused", free_checked_s(interior) == CPLUS_SAFE_EINVAL);
    CHECK("free_interior_errno", errno == EINVAL);

    raw = malloc(24);
    CHECK("free_raw_setup", raw != NULL);
    if (raw != NULL) {
        errno = 0;
        CHECK("free_raw_malloc_refused", free_checked_s(raw) == CPLUS_SAFE_EINVAL);
        CHECK("free_raw_errno", errno == EINVAL);
        CHECK("realloc_raw_refused", realloc_s(raw, 48) == NULL);
        free(raw); /* raw malloc memory must be released with raw free */
    }
    CHECK("free_refusals_no_live_change",
          cplus_safe_alloc_live_count() == live_after_free);
}

/* ---- zero-size allocation ----------------------------------------------- */
static void test_zero_size(void) {
    void *a = malloc_s(0);
    void *b = calloc_s(0, 0);
    void *c = calloc_s(4, 0);

    CHECK("malloc_zero_nonnull", a != NULL);
    if (a != NULL) {
        CHECK("malloc_zero_is_live", cplus_safe_ptr_is_live(a) == 1);
        CHECK("malloc_zero_size_one", cplus_safe_ptr_size(a) == 1);
        CHECK("malloc_zero_free_ok", free_checked_s(a) == CPLUS_SAFE_OK);
    }
    CHECK("calloc_zero_zero_nonnull", b != NULL);
    if (b != NULL) {
        CHECK("calloc_zero_zero_size_one", cplus_safe_ptr_size(b) == 1);
        CHECK("calloc_zero_zero_is_zero", ((unsigned char *)b)[0] == 0);
        free_s(b);
    }
    CHECK("calloc_n_zero_nonnull", c != NULL);
    if (c != NULL) {
        CHECK("calloc_n_zero_size_one", cplus_safe_ptr_size(c) == 1);
        CHECK("calloc_n_zero_is_zero", ((unsigned char *)c)[0] == 0);
        free_s(c);
    }
}

/* ---- multiplication overflow and over-limit sizes ----------------------- */
static void test_overflow(void) {
    errno = 0;
    CHECK("calloc_overflow_max_by_2", calloc_s(SIZE_MAX, 2) == NULL);
    CHECK("calloc_overflow_eover", errno == EOVERFLOW);
    errno = 0;
    CHECK("calloc_overflow_half_plus_1",
          calloc_s(SIZE_MAX / 2 + 1, 2) == NULL);
    CHECK("calloc_overflow_half_eover", errno == EOVERFLOW);
    errno = 0;
    CHECK("calloc_rsizemax_enomem", calloc_s(RSIZE_MAX_MEM, 1) == NULL);
    CHECK("calloc_rsizemax_errno", errno == ENOMEM);
    errno = 0;
    CHECK("malloc_size_einval", malloc_s((size_t)-1) == NULL);
    CHECK("malloc_size_einval_errno", errno == EINVAL);
}

/* ---- failure injection --------------------------------------------------- */
static void test_failure_injection(void) {
    unsigned long seq0, seq1, seq2;
    void *p1, *p2, *p3;

    cplus_safe_alloc_fail_reset();
    seq0 = cplus_safe_alloc_seq();
    cplus_safe_alloc_fail_after(1);
    p1 = malloc_s(16);
    seq1 = cplus_safe_alloc_seq();
    CHECK("inject_first_succeeds", p1 != NULL);
    CHECK("inject_seq_advances_on_success", seq1 > seq0);

    errno = 0;
    p2 = malloc_s(16);
    seq2 = cplus_safe_alloc_seq();
    CHECK("inject_second_fails", p2 == NULL);
    CHECK("inject_second_enomem", errno == ENOMEM);
    CHECK("inject_seq_does_not_rewind", seq2 >= seq1);

    cplus_safe_alloc_fail_reset();
    p3 = malloc_s(16);
    CHECK("inject_reset_recovers", p3 != NULL);

    if (p1 != NULL) free_s(p1);
    if (p3 != NULL) free_s(p3);
}

/* ---- pointer-to-pointer cleanup ----------------------------------------- */
static void test_pointer_cleanup(void) {
    void *src = malloc_s(16);
    void *dst = NULL;
    void *orig;
    void *src2;
    void *dst2;
    unsigned long long old_gen;
    cplus_safe_alloc_meta m;
    void *libc_p;

    CHECK("transfer_setup", src != NULL);
    if (src == NULL) return;
    orig = src;
    CHECK("transfer_meta_before",
          cplus_safe_alloc_get_meta(src, &m) == CPLUS_SAFE_OK);
    old_gen = m.generation;

    CHECK("transfer_ok", cplus_safe_transfer_ownership(&dst, &src) == CPLUS_SAFE_OK);
    CHECK("transfer_src_nulled", src == NULL);
    CHECK("transfer_dst_gets_block", dst == orig);
    CHECK("transfer_still_live", cplus_safe_ptr_is_live(dst) == 1);
    CHECK("transfer_generation_bumped",
          cplus_safe_ptr_check_generation(dst, old_gen) == 0);
    CHECK("transfer_same_slot_refused",
          cplus_safe_transfer_ownership(&dst, &dst) == CPLUS_SAFE_EINVAL);
    CHECK("transfer_null_addr_refused",
          cplus_safe_transfer_ownership(NULL, &dst) == CPLUS_SAFE_EINVAL);
    CHECK("transfer_dst_alive_after_refusals", dst == orig);
    free_s(dst);

    /* *dst must be empty, otherwise the transfer is refused */
    src2 = malloc_s(8);
    dst2 = malloc_s(8);
    CHECK("transfer_occupied_setup", src2 != NULL && dst2 != NULL);
    CHECK("transfer_dst_occupied_refused",
          cplus_safe_transfer_ownership(&dst2, &src2) == CPLUS_SAFE_EINVAL);
    CHECK("transfer_occupied_src_unchanged", src2 != NULL);
    CHECK("transfer_occupied_dst_unchanged", dst2 != NULL);
    if (src2 != NULL) free_s(src2);
    if (dst2 != NULL) free_s(dst2);

    /* hardened-libc pointer-to-pointer cleanup: frees and nulls the slot */
    libc_p = cplus_malloc(24);
    CHECK("libc_cleanup_setup", libc_p != NULL);
    if (libc_p != NULL) {
        CHECK("libc_free_checked_clears_slot", cplus_free_checked(&libc_p) == 0);
        CHECK("libc_free_checked_slot_null", libc_p == NULL);
        CHECK("libc_free_checked_null_again", cplus_free_checked(&libc_p) == 0);
    }
}

int main(void) {
    size_t base_live = cplus_safe_alloc_live_count();
    size_t base_bytes = cplus_safe_alloc_live_bytes();

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    test_calloc_capacity();
    test_realloc_contract();
    test_free_refusal();
    test_zero_size();
    test_overflow();
    test_failure_injection();
    test_pointer_cleanup();

    CHECK("alloc_live_count_balanced", cplus_safe_alloc_live_count() == base_live);
    CHECK("alloc_live_bytes_balanced", cplus_safe_alloc_live_bytes() == base_bytes);

    printf("P0_ALLOC %d %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
