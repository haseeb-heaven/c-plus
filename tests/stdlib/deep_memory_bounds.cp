/* Deep corpus test for the additive bounds-aware wrapper in stdlib/memory.hp.
 *
 * Covers: capacity/length queries, zero-length operations, exact fit, one-over
 * refusals (write AND read), NULL destinations/sources, interior access via
 * explicit (base, offset), stale generation tokens, borrow/transfer/release
 * semantics, repeated cleanup, and logging-off/on parity.
 *
 * Design under test: NO fat pointers, NO ABI change.  Every guarded helper
 * takes a raw pointer plus an explicit offset/length and reads capacity from
 * stdlib/error.hp metadata.  A refused write is checked by verifying the
 * destination's sentinel bytes are untouched (all reads stay in bounds, so
 * the test is UBSan-clean).
 *
 * Output: one "<tag> <0|1>" line per check, then
 * "MEMORY_BOUNDS <checks> <failures>".  Exit 0 only when failures == 0.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static int mb_checks = 0;
static int mb_failures = 0;
static int mb_callback_calls = 0;

static void mb_check(const char *tag, int cond) {
    mb_checks++;
    if (!cond)
        mb_failures++;
    print(tag, cond ? 1 : 0);
}

/* Verify an error record matches the expected failed/category/code triple. */
static void mb_err(const char *tag, error *e, int failed, int category,
                   int code) {
    int ok = e->failed_now() == failed;
    if (failed) {
        ok = ok && e->category() == category && e->code() == code &&
             (int)strlen(e->what()) > 0;
    } else {
        ok = ok && e->category() == STDLIB_CATEGORY_OK &&
             e->code() == (int)CPLUS_EXIT_OK;
    }
    mb_check(tag, ok);
}

static void mb_counting_callback(int category, cplus_exit_code code,
                                 const char *message) {
    (void)category;
    (void)code;
    (void)message;
    mb_callback_calls++;
}

/* Fill a tracked block with a byte using the guarded API and a fresh token. */
static int mb_fill_tracked(void *p, size_t cap, int value, error *e) {
    return stdlib_mem_set(p, 0, value, cap, stdlib_mem_token(p), e);
}

static int mb_all_zero(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != 0)
            return 0;
    }
    return 1;
}

static int mb_all_byte(const unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != v)
            return 0;
    }
    return 1;
}

/* ---- queries ------------------------------------------------------------ */

static void mb_queries(void) {
    error e = error();
    size_t cnt = 99;
    void *p = stdlib_alloc(32);
    void *arr = stdlib_calloc(8, 4);

    mb_check("q_cap_null", stdlib_mem_capacity((void *)0) == 0);
    mb_check("q_count_null",
             stdlib_mem_count((void *)0, 0, &cnt) == -1 && cnt == 0);
    mb_err("q_count_null_error", &e, 0, STDLIB_CATEGORY_OK, CPLUS_EXIT_OK);

    if (p == (void *)0 || arr == (void *)0) {
        mb_check("q_alloc_ok", 0);
        stdlib_mem_release(&p);
        stdlib_mem_release(&arr);
        return;
    }
    mb_check("q_alloc_ok", 1);
    mb_check("q_cap_tracked", stdlib_mem_capacity(p) == 32);
    cnt = 0;
    mb_check("q_count_bytes",
             stdlib_mem_count(p, 1, &cnt) == 0 && cnt == 32);
    cnt = 0;
    mb_check("q_count_elems",
             stdlib_mem_count(arr, 4, &cnt) == 0 && cnt == 8);
    cnt = 0;
    mb_check("q_count_recorded",
             stdlib_mem_count(arr, 0, &cnt) == 0 && cnt == 8);
    mb_check("q_token_nonzero", stdlib_mem_token(p) != 0);
    mb_check("q_token_untracked",
             stdlib_mem_capacity((void *)mb_queries) == 0 &&
                 stdlib_mem_token((void *)mb_queries) == 0);

    stdlib_mem_release(&p);
    stdlib_mem_release(&arr);
}

/* ---- empty / zero size -------------------------------------------------- */

static void mb_zero_size(void) {
    error e = error();
    unsigned char zdst[8];
    unsigned char zsrc[8];
    size_t i;
    void *p;
    int rc;

    for (i = 0; i < 8; i++) {
        zdst[i] = (unsigned char)(0xA0 + i);
        zsrc[i] = (unsigned char)(0x50 + i);
    }

    /* Need tracked blocks even for zero-length operations: the wrapper
     * validates pointers/capacity before short-circuiting. */
    p = stdlib_alloc(8);
    if (p == (void *)0) {
        mb_check("z_alloc_ok", 0);
        return;
    }
    rc = stdlib_mem_copy(p, 0, p, 0, 0, 0, &e);
    mb_check("z_copy_zero", rc == STDLIB_OK && !e.failed_now());
    rc = stdlib_mem_set(p, 0, 0x77, 0, 0, &e);
    mb_check("z_set_zero", rc == STDLIB_OK && !e.failed_now());
    mb_err("z_zero_error_clean", &e, 0, STDLIB_CATEGORY_OK, CPLUS_EXIT_OK);

    /* stdlib_alloc(0) records a 1-byte minimum; a 1-byte write fits, a 2-byte
     * write does not. */
    {
        void *z = stdlib_alloc(0);
        mb_check("z_alloc_zero_tracked", z != (void *)0);
        if (z != (void *)0) {
            mb_check("z_alloc_zero_cap1", stdlib_mem_capacity(z) == 1);
            mb_check("z_alloc_zero_set1",
                     stdlib_mem_set(z, 0, 0x11, 1, 0, &e) == STDLIB_OK);
            mb_check("z_alloc_zero_set2_refused",
                     stdlib_mem_set(z, 0, 0x11, 2, 0, &e) == STDLIB_FAIL &&
                         e.category() == STDLIB_CATEGORY_BOUNDS);
            stdlib_mem_release(&z);
        }
    }
    (void)zdst;
    (void)zsrc;
    stdlib_mem_release(&p);
}

/* ---- exact fit / one-over / NULL / untracked ---------------------------- */

static void mb_bounds_and_null(void) {
    error e = error();
    unsigned char *dst = (unsigned char *)stdlib_alloc(16);
    unsigned char *src = (unsigned char *)stdlib_alloc(16);
    unsigned char *small = (unsigned char *)stdlib_alloc(8);
    unsigned char stackbuf[16];
    size_t i;
    int rc;

    if (dst == (unsigned char *)0 || src == (unsigned char *)0 ||
        small == (unsigned char *)0) {
        mb_check("b_alloc_ok", 0);
        stdlib_mem_release((void **)&dst);
        stdlib_mem_release((void **)&src);
        stdlib_mem_release((void **)&small);
        return;
    }
    mb_check("b_alloc_ok", 1);

    for (i = 0; i < 16; i++) {
        src[i] = (unsigned char)(i + 1);
        dst[i] = 0xEE;
    }

    /* exact fit: 16 bytes into a 16-byte block */
    rc = stdlib_mem_copy(dst, 0, src, 0, 16, 0, &e);
    mb_check("x_copy_exact",
             rc == STDLIB_OK && !e.failed_now() && memcmp(dst, src, 16) == 0);

    /* exact-fit set and overlapping memmove */
    rc = stdlib_mem_set(dst, 0, 0x5A, 16, 0, &e);
    mb_check("x_set_exact",
             rc == STDLIB_OK && mb_all_byte(dst, 16, 0x5A));
    rc = stdlib_mem_move(dst, 0, dst, 4, 12, 0, &e);
    mb_check("x_move_exact", rc == STDLIB_OK && dst[0] == 0x5A);

    /* one-over write: dst capacity 16, request 17 -> refuse, no writes */
    stdlib_mem_set(dst, 0, 0xEE, 16, 0, &e);
    rc = stdlib_mem_copy(dst, 0, src, 0, 17, 0, &e);
    mb_check("o_copy_over",
             rc == STDLIB_FAIL && e.failed_now() &&
                 e.category() == STDLIB_CATEGORY_BOUNDS &&
                 e.code() == (int)CPLUS_EXIT_BOUNDS);
    mb_check("o_copy_over_no_write", mb_all_byte(dst, 16, 0xEE));

    /* offset+length crosses the end by one */
    rc = stdlib_mem_set(dst, 15, 0x11, 2, 0, &e);
    mb_check("o_set_off_over",
             rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_BOUNDS);
    mb_check("o_set_off_over_no_write", dst[15] == 0xEE);

    /* source too short is refused too (read bounds) */
    rc = stdlib_mem_copy(dst, 0, small, 0, 9, 0, &e);
    mb_check("o_src_over",
             rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_BOUNDS &&
                 strstr(e.what(), "source") != (const char *)0);

    /* offset beyond capacity, even with zero length, is refused */
    rc = stdlib_mem_set(dst, 17, 0, 0, 0, &e);
    mb_check("o_off_gt_cap", rc == STDLIB_FAIL);

    /* NULL destination/source are refused (never dereferenced) */
    rc = stdlib_mem_copy((void *)0, 0, src, 0, 1, 0, &e);
    mb_check("n_dst_null",
             rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID &&
                 e.code() == (int)CPLUS_EXIT_NULL_POINTER);
    rc = stdlib_mem_copy(dst, 0, (void *)0, 0, 1, 0, &e);
    mb_check("n_src_null",
             rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID);
    rc = stdlib_mem_set((void *)0, 0, 0, 1, 0, &e);
    mb_check("n_set_null", rc == STDLIB_FAIL);
    rc = stdlib_mem_borrow((void *)0, 0, 1, 0, &e);
    mb_check("n_borrow_null", rc == STDLIB_FAIL);

    /* Documented limitation: a raw stack pointer has no metadata and is
     * refused rather than trusted. */
    mb_check("u_stack_untracked", stdlib_mem_capacity(stackbuf) == 0);
    rc = stdlib_mem_set(stackbuf, 0, 0x11, 1, 0, &e);
    mb_check("u_stack_refused",
             rc == STDLIB_FAIL &&
                 e.category() == STDLIB_CATEGORY_INVALID);

    stdlib_mem_release((void **)&dst);
    stdlib_mem_release((void **)&src);
    stdlib_mem_release((void **)&small);
}

/* ---- interior pointer via explicit (base, offset) ----------------------- */

static void mb_interior(void) {
    error e = error();
    unsigned char *p = (unsigned char *)stdlib_alloc(64);
    unsigned char *q = (unsigned char *)stdlib_alloc(16);
    size_t i;
    int rc;

    if (p == (unsigned char *)0 || q == (unsigned char *)0) {
        mb_check("i_alloc_ok", 0);
        stdlib_mem_release((void **)&p);
        stdlib_mem_release((void **)&q);
        return;
    }
    mb_check("i_alloc_ok", 1);
    mb_fill_tracked(p, 64, 0, &e);

    /* write bytes [16,24) through base+offset */
    rc = stdlib_mem_set(p, 16, 0xAB, 8, 0, &e);
    mb_check("i_set_interior", rc == STDLIB_OK);
    mb_check("i_set_neighbors_clean",
             mb_all_zero(p, 16) && mb_all_zero(p + 24, 40) &&
                 mb_all_byte(p + 16, 8, 0xAB));

    /* read from interior of q into interior of p */
    for (i = 0; i < 16; i++)
        q[i] = (unsigned char)(0xC0 + i);
    rc = stdlib_mem_copy(p, 32, q, 4, 8, 0, &e);
    mb_check("i_copy_interior", rc == STDLIB_OK && p[32] == 0xC4 &&
                                    p[39] == 0xCB);
    mb_check("i_copy_neighbors_clean", mb_all_zero(p + 24, 8));

    /* interior offset + one-over is refused and leaves the tail intact */
    rc = stdlib_mem_set(p, 60, 0xFF, 8, 0, &e);
    mb_check("i_over_from_interior",
             rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_BOUNDS);
    mb_check("i_over_tail_clean", mb_all_zero(p + 60, 4));

    /* off == cap with zero length is a valid no-op */
    mb_check("i_off_eq_cap_zero",
             stdlib_mem_set(p, 64, 0, 0, 0, &e) == STDLIB_OK);

    stdlib_mem_release((void **)&p);
    stdlib_mem_release((void **)&q);
}

/* ---- generation tokens and borrow/own semantics ------------------------- */

static void mb_generation_and_ownership(void) {
    error e = error();
    void *p = stdlib_alloc(16);
    void *q = (void *)0;
    void *bad = (void *)0;
    unsigned long long tok;
    unsigned long long tok2;

    if (p == (void *)0) {
        mb_check("g_alloc_ok", 0);
        return;
    }
    mb_check("g_alloc_ok", 1);

    tok = stdlib_mem_token(p);
    mb_check("g_current_token_ok",
             stdlib_mem_set(p, 0, 0x33, 16, tok, &e) == STDLIB_OK);
    mb_check("g_borrow_current_ok",
             stdlib_mem_borrow(p, 4, 8, tok, &e) == STDLIB_OK);
    mb_check("g_borrow_over_refused",
             stdlib_mem_borrow(p, 12, 8, tok, &e) == STDLIB_FAIL &&
                 e.category() == STDLIB_CATEGORY_BOUNDS);

    /* OWN: transfer bumps the generation, invalidating tok.  error records are
     * sticky until reset, so clear the one left by the refused borrow above. */
    error_reset(&e);
    mb_check("g_transfer_ok",
             stdlib_mem_transfer(&q, &p, &e) == STDLIB_OK && !e.failed_now() &&
                 q != (void *)0 && p == (void *)0);
    tok2 = stdlib_mem_token(q);
    mb_check("g_token_changed", tok2 != 0 && tok2 != tok);

    mb_check("g_stale_write_refused",
             stdlib_mem_set(q, 0, 0x44, 16, tok, &e) == STDLIB_FAIL &&
                 e.category() == STDLIB_CATEGORY_LIFECYCLE &&
                 e.code() == (int)CPLUS_EXIT_USE_AFTER_FREE);
    mb_check("g_stale_borrow_refused",
             stdlib_mem_borrow(q, 0, 1, tok, &e) == STDLIB_FAIL &&
                 e.category() == STDLIB_CATEGORY_LIFECYCLE);
    mb_check("g_new_token_ok",
             stdlib_mem_set(q, 0, 0x44, 16, tok2, &e) == STDLIB_OK);

    /* Invalid transfer handles are refused without touching the live block. */
    mb_check("g_transfer_dst_occupied",
             stdlib_mem_transfer(&q, &bad, &e) == STDLIB_FAIL &&
                 e.category() == STDLIB_CATEGORY_OWNERSHIP && q != (void *)0);
    mb_check("g_transfer_src_null",
             stdlib_mem_transfer(&bad, &p, &e) == STDLIB_FAIL &&
                 e.category() == STDLIB_CATEGORY_OWNERSHIP);

    stdlib_mem_release(&q);
    stdlib_mem_release(&p);
}

/* ---- repeated cleanup --------------------------------------------------- */

static void mb_cleanup(void) {
    error e = error();
    stdlib_telemetry_stats before;
    stdlib_telemetry_stats after;
    void *p = stdlib_alloc(8);
    void *nil = (void *)0;

    if (p == (void *)0) {
        mb_check("c_alloc_ok", 0);
        return;
    }
    mb_check("c_alloc_ok", 1);
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_snapshot(&before);

    stdlib_mem_release(&p);
    mb_check("c_release_nulls_handle", p == (void *)0);
    stdlib_mem_release(&p); /* second call is a no-op */
    stdlib_mem_release(&nil);
    mb_check("c_release_null_noop", nil == (void *)0);

    stdlib_telemetry_snapshot(&after);
    mb_check("c_single_free_counted",
             after.free_count - before.free_count == 1ULL);
    (void)e;
}

/* ---- logging off/on parity ---------------------------------------------- */

/* Run a fixed mixed scenario; record (rc, failed, category, code) for each
 * step.  The result vector must be byte-for-byte identical whether the error
 * callback/telemetry logging is off or on. */
static void mb_parity_scenario(int enable_logging, long *out, int n) {
    error e = error();
    unsigned char *d;
    unsigned char *s;
    int k = 0;
    int rc;

    if (enable_logging) {
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(mb_counting_callback);
    } else {
        stdlib_telemetry_set_enabled(0);
        stdlib_set_error_callback((stdlib_error_callback)0);
    }

    d = (unsigned char *)stdlib_alloc(16);
    s = (unsigned char *)stdlib_alloc(8);
    if (d == (unsigned char *)0 || s == (unsigned char *)0) {
        while (k < n)
            out[k++] = -999L;
        stdlib_mem_release((void **)&d);
        stdlib_mem_release((void **)&s);
        return;
    }
    memset(s, 0x67, 8);

    rc = stdlib_mem_copy(d, 0, s, 0, 8, 0, &e);
    out[k++] = (long)rc;
    out[k++] = (long)e.failed_now();
    out[k++] = (long)e.category();
    out[k++] = (long)e.code();

    error_reset(&e);
    rc = stdlib_mem_copy(d, 0, s, 0, 9, 0, &e);
    out[k++] = (long)rc;
    out[k++] = (long)e.failed_now();
    out[k++] = (long)e.category();
    out[k++] = (long)e.code();

    error_reset(&e);
    rc = stdlib_mem_set((void *)0, 0, 0, 1, 0, &e);
    out[k++] = (long)rc;
    out[k++] = (long)e.failed_now();
    out[k++] = (long)e.category();
    out[k++] = (long)e.code();

    error_reset(&e);
    rc = stdlib_mem_borrow(d, 8, 8, stdlib_mem_token(d), &e);
    out[k++] = (long)rc;
    out[k++] = (long)e.failed_now();
    out[k++] = (long)e.category();
    out[k++] = (long)e.code();

    while (k < n)
        out[k++] = 0L;
    stdlib_mem_release((void **)&d);
    stdlib_mem_release((void **)&s);
}

static void mb_parity(void) {
    long off[16];
    long on[16];
    stdlib_telemetry_stats tm;
    int i;
    int same = 1;

    for (i = 0; i < 16; i++) {
        off[i] = 0L;
        on[i] = 0L;
    }

    mb_callback_calls = 0;
    stdlib_telemetry_reset();
    mb_parity_scenario(0, off, 16);
    mb_check("p_callback_off_silent", mb_callback_calls == 0);
    stdlib_telemetry_snapshot(&tm);
    mb_check("p_telemetry_off_quiet",
             tm.bounds_violations == 0ULL && tm.api_failures == 0ULL);

    mb_callback_calls = 0;
    stdlib_telemetry_reset();
    mb_parity_scenario(1, on, 16);
    mb_check("p_callback_on_fires", mb_callback_calls > 0);
    stdlib_telemetry_snapshot(&tm);
    mb_check("p_telemetry_on_counts",
             tm.bounds_violations > 0ULL && tm.api_failures > 0ULL);

    for (i = 0; i < 16; i++) {
        if (off[i] != on[i])
            same = 0;
    }
    mb_check("p_results_identical", same);

    /* restore defaults for any host that shares the process */
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
}

int main(void) {
    mb_queries();
    mb_zero_size();
    mb_bounds_and_null();
    mb_interior();
    mb_generation_and_ownership();
    mb_cleanup();
    mb_parity();

    print("MEMORY_BOUNDS", mb_checks, mb_failures);
    return mb_failures == 0 ? 0 : 1;
}
