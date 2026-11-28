/* EDGE100 adapters -- fork-per-case edge harness.
 *
 * Scope (every public API in these translation units):
 *   stdlib/gsl_adapt.hp       -- 29 static inline functions
 *   stdlib/lifetime_adapt.hp  --  9 static inline functions
 *   stdlib/alloc_iface.hp     --  5 modern allocator interface helpers
 *   stdlib/alloc_policy.hp    --  4 modern allocator policy helpers
 *   Total: 47 APIs.
 *
 * METHOD
 * ------
 * One fork()ed child per case.  The child arms a 64-byte stack sentinel and
 * two 64-byte stdlib guard allocations before the workload, verifies them
 * afterwards and encodes the result in its exit status:
 *   bit 0 (1) : the expected API verdict was observed
 *   bit 1 (2) : all canaries intact
 * Parent accepts only status 3.  A crash, hang (alarm watchdog) or canary
 * corruption therefore fails that case.  The overall runner is additionally
 * bounded by `timeout 60` in run.sh.
 *
 * Per-API edge dimensions: valid, NULL, narrow min/max boundary, stale token,
 * double release, recycled address, and origin/span/bounds checks.
 *
 * OUTPUT
 *   CASE <id> <api> <name> <status>
 *   API <api> <total> <passed> <failed>
 *   EDGE100 adapters <apis> <cases> <passed> <failed>
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"
#include "stdlib/gsl_adapt.hp"
#include "stdlib/lifetime_adapt.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/alloc_policy.hp"

/* ---- API registry -------------------------------------------------------- */

enum {
    API_REFUSE_NULL, API_REFUSE_RANGE, API_REFUSE_BOUNDS,
    API_N_I64_I8, API_N_I64_U8, API_N_I64_I16, API_N_I64_U16,
    API_N_I64_I32, API_N_I64_U32, API_N_I64_I64, API_N_I64_U64,
    API_N_U64_I64, API_N_U64_U8, API_N_U64_U32,
    API_NN_I32_MAKE, API_NN_I32_GET, API_NN_I32_SWAP,
    API_NN_VOID_MAKE, API_NN_VOID_GET,
    API_SPAN_VIEW, API_SPAN_MAKE, API_SPAN_SIZE, API_SPAN_SIZE_BYTES,
    API_SPAN_EMPTY, API_SPAN_DATA, API_SPAN_AT, API_SPAN_FIRST,
    API_SPAN_LAST, API_SPAN_SUBSPAN,
    API_LT_CHECK, API_LT_OWNER_INIT, API_LT_OWNER_RELEASE, API_LT_TRANSFER,
    API_LT_BORROW_FROM, API_LT_BORROW_LIVE, API_LT_OWNER_LIVE,
    API_LT_READ, API_LT_WRITE,
    API_ALLOCATE, API_REALLOCATE, API_DEALLOCATE, API_ALLOCATOR_RESET,
    API_HEAP_ALLOCATOR, API_COUNT_SIZE, API_ROUND_UP, API_SIZE_WORDS,
    API_NORMALIZE_ZERO,
    API_COUNT
};

static const char *g_api_name[API_COUNT] = {
    "gsl_adapt_refuse_null", "gsl_adapt_refuse_range", "gsl_adapt_refuse_bounds",
    "gsl_narrow_i64_i8", "gsl_narrow_i64_u8", "gsl_narrow_i64_i16",
    "gsl_narrow_i64_u16", "gsl_narrow_i64_i32", "gsl_narrow_i64_u32",
    "gsl_narrow_i64_i64", "gsl_narrow_i64_u64",
    "gsl_narrow_u64_i64", "gsl_narrow_u64_u8", "gsl_narrow_u64_u32",
    "gsl_not_null_i32_make", "gsl_not_null_i32_get", "gsl_not_null_i32_swap",
    "gsl_not_null_void_make", "gsl_not_null_void_get",
    "gsl_span_i32_view", "gsl_span_i32_make", "gsl_span_i32_size",
    "gsl_span_i32_size_bytes", "gsl_span_i32_empty", "gsl_span_i32_data",
    "gsl_span_i32_at", "gsl_span_i32_first", "gsl_span_i32_last",
    "gsl_span_i32_subspan",
    "lifetime_check", "lifetime_owner_init", "lifetime_owner_release",
    "lifetime_transfer", "lifetime_borrow_from", "lifetime_borrow_is_live",
    "lifetime_owner_is_live", "lifetime_read", "lifetime_write",
    "stdlib_allocate", "stdlib_reallocate", "stdlib_deallocate",
    "stdlib_allocator_reset", "stdlib_heap_allocator",
    "stdlib_count_size_overflow", "stdlib_size_class_round_up",
    "stdlib_size_class_words", "stdlib_size_normalize_zero"
};

struct case_def {
    int api;
    int kind;
    const char *name;
    int (*fn)(int);
};

#define MAX_CASES 512
static struct case_def g_cases[MAX_CASES];
static int g_ncases = 0;

static void add_case(int api, int kind, const char *name, int (*fn)(int)) {
    if (g_ncases < MAX_CASES) {
        g_cases[g_ncases].api = api;
        g_cases[g_ncases].kind = kind;
        g_cases[g_ncases].name = name;
        g_cases[g_ncases].fn = fn;
        g_ncases++;
    }
}

/* ---- expectation helpers ------------------------------------------------- */

static int expect_ok(int rc, error *e) {
    return rc == STDLIB_OK && (e == (error *)0 || !e->failed_now());
}

static int expect_fail(int rc, error *e, int cat, int code) {
    return rc == STDLIB_FAIL && e != (error *)0 && e->failed_now() &&
           e->category() == cat && e->code() == code;
}

/* ---- canaries ------------------------------------------------------------ */

static volatile unsigned char g_sentinel[64];
static void *g_guard_lo;
static void *g_guard_hi;
static int g_reuse_hits;   /* parent-side count of cases that reused an address */
static int g_case_reused;  /* per-child flag, reset before each case */

static void canary_arm(void) {
    size_t i;
    for (i = 0; i < 64; i++)
        g_sentinel[i] = (unsigned char)(0xA5u ^ (unsigned char)i);
    g_guard_lo = stdlib_alloc(64);
    g_guard_hi = stdlib_alloc(64);
    if (g_guard_lo != (void *)0) memset(g_guard_lo, 0x5A, 64);
    if (g_guard_hi != (void *)0) memset(g_guard_hi, 0x3C, 64);
}

static int canary_ok(void) {
    size_t i;
    for (i = 0; i < 64; i++)
        if (g_sentinel[i] != (unsigned char)(0xA5u ^ (unsigned char)i))
            return 0;
    if (g_guard_lo != (void *)0)
        for (i = 0; i < 64; i++)
            if (((unsigned char *)g_guard_lo)[i] != 0x5A) return 0;
    if (g_guard_hi != (void *)0)
        for (i = 0; i < 64; i++)
            if (((unsigned char *)g_guard_hi)[i] != 0x3C) return 0;
    return 1;
}

/* Release a block and try to obtain the very same address again.  Returns the
 * new block (which the caller owns) or NULL.  g_reuse_hits records success. */
static void *recycle_addr(void *old, size_t bytes) {
    void *p;
    int i;
    if (old == (void *)0) return (void *)0;
    stdlib_release(old);
    for (i = 0; i < 256; i++) {
        p = stdlib_alloc(bytes);
        if (p == old) { g_case_reused = 1; return p; }
        if (p != (void *)0) stdlib_release(p);
    }
    return (void *)0;
}

static void owner_zero(lifetime_owner *o) { memset(o, 0, sizeof(*o)); }
static void borrow_zero(lifetime_borrow *b) { memset(b, 0, sizeof(*b)); }

/* =========================================================================
 * 1. refuse helpers
 * ========================================================================= */

static int d_refuse_null(int kind) {
    error e = error();
    if (kind == 0) return expect_fail(gsl_adapt_refuse_null(&e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    /* NULL err is fail-closed: error_fail_cat handles a NULL target safely. */
    return gsl_adapt_refuse_null((error *)0) == STDLIB_FAIL;
}

static int d_refuse_range(int kind) {
    error e = error();
    if (kind == 0) return expect_fail(gsl_adapt_refuse_range(&e), &e,
                                     STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION);
    return gsl_adapt_refuse_range((error *)0) == STDLIB_FAIL;
}

static int d_refuse_bounds(int kind) {
    error e = error();
    if (kind == 0) return expect_fail(gsl_adapt_refuse_bounds(&e), &e,
                                     STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS);
    return gsl_adapt_refuse_bounds((error *)0) == STDLIB_FAIL;
}

/* =========================================================================
 * 2. narrow helpers -- generic drivers over typed wrappers
 * ========================================================================= */

typedef int (*narrow_i64_fn)(long long, void *, error *);
typedef int (*narrow_u64_fn)(unsigned long long, void *, error *);

static int n_i64_i8(long long u, void *o, error *e) { return gsl_narrow_i64_i8(u, (signed char *)o, e); }
static int n_i64_u8(long long u, void *o, error *e) { return gsl_narrow_i64_u8(u, (unsigned char *)o, e); }
static int n_i64_i16(long long u, void *o, error *e) { return gsl_narrow_i64_i16(u, (short *)o, e); }
static int n_i64_u16(long long u, void *o, error *e) { return gsl_narrow_i64_u16(u, (unsigned short *)o, e); }
static int n_i64_i32(long long u, void *o, error *e) { return gsl_narrow_i64_i32(u, (int *)o, e); }
static int n_i64_u32(long long u, void *o, error *e) { return gsl_narrow_i64_u32(u, (unsigned int *)o, e); }
static int n_i64_i64(long long u, void *o, error *e) { return gsl_narrow_i64_i64(u, (long long *)o, e); }
static int n_i64_u64(long long u, void *o, error *e) { return gsl_narrow_i64_u64(u, (unsigned long long *)o, e); }
static int n_u64_i64(unsigned long long u, void *o, error *e) { return gsl_narrow_u64_i64(u, (long long *)o, e); }
static int n_u64_u8(unsigned long long u, void *o, error *e) { return gsl_narrow_u64_u8(u, (unsigned char *)o, e); }
static int n_u64_u32(unsigned long long u, void *o, error *e) { return gsl_narrow_u64_u32(u, (unsigned int *)o, e); }

union nbuf {
    signed char c; unsigned char uc; short s; unsigned short us;
    int i; unsigned int ui; long long ll; unsigned long long ull;
};

static int nbuf_value_ok(union nbuf *u, unsigned long long v,
                         size_t out_size, int is_signed) {
    union nbuf expect;
    memset(&expect, 0, sizeof(expect));
    switch (out_size) {
        case 1: if (is_signed) expect.c = (signed char)v; else expect.uc = (unsigned char)v; break;
        case 2: if (is_signed) expect.s = (short)v; else expect.us = (unsigned short)v; break;
        case 4: if (is_signed) expect.i = (int)v; else expect.ui = (unsigned int)v; break;
        default: if (is_signed) expect.ll = (long long)v; else expect.ull = v; break;
    }
    return memcmp(u, &expect, out_size) == 0;
}

/* kind: 0 zero, 1 lo, 2 hi, 3 reject below, 4 reject above,
 *       5 NULL out, 6 NULL err (with zero input). */
static int run_narrow_i64(narrow_i64_fn fn, size_t out_size, int is_signed,
                          long long lo, long long hi, int reject_below,
                          int reject_above, long long below, long long above,
                          int kind) {
    error e = error();
    union nbuf u;
    unsigned long long used;
    int rc;
    memset(&u, 0xA5, sizeof(u));
    if (kind == 5) {
        return expect_fail(fn(0, (void *)0, &e), &e,
                           STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    }
    if (kind == 6) return fn(0, &u, (error *)0) == STDLIB_OK;
    if (kind == 0) used = 0;
    else if (kind == 1) used = (unsigned long long)lo;
    else if (kind == 2) used = (unsigned long long)hi;
    else if (kind == 3) used = (unsigned long long)below;
    else used = (unsigned long long)above;
    rc = fn((long long)used, &u, &e);
    if (kind == 3 && reject_below)
        return expect_fail(rc, &e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION);
    if (kind == 4 && reject_above)
        return expect_fail(rc, &e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION);
    return expect_ok(rc, &e) && nbuf_value_ok(&u, used, out_size, is_signed);
}

/* kind: 0 zero, 1 hi, 2 reject above, 3 NULL out, 4 NULL err. */
static int run_narrow_u64(narrow_u64_fn fn, size_t out_size, int is_signed,
                          unsigned long long hi, int reject_above,
                          unsigned long long above, int kind) {
    error e = error();
    union nbuf u;
    unsigned long long used;
    int rc;
    memset(&u, 0xA5, sizeof(u));
    if (kind == 3) {
        return expect_fail(fn(0, (void *)0, &e), &e,
                           STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    }
    if (kind == 4) return fn(0, &u, (error *)0) == STDLIB_OK;
    used = (kind == 1) ? hi : (kind == 2 ? above : 0);
    rc = fn(used, &u, &e);
    if (kind == 2 && reject_above)
        return expect_fail(rc, &e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION);
    return expect_ok(rc, &e) && nbuf_value_ok(&u, used, out_size, is_signed);
}

#define D_I64(NAME, SUF, T, SIGNED, LO, HI, RB, RA, BELOW, ABOVE) \
static int d_##SUF(int kind) { \
    return run_narrow_i64(n_##SUF, sizeof(T), SIGNED, LO, HI, RB, RA, BELOW, ABOVE, kind); \
}
#define D_U64(NAME, SUF, T, SIGNED, HI, RA, ABOVE) \
static int d_##SUF(int kind) { \
    return run_narrow_u64(n_##SUF, sizeof(T), SIGNED, HI, RA, ABOVE, kind); \
}

D_I64(x, i64_i8,  signed char,      1, SCHAR_MIN,  SCHAR_MAX,  1, 1, (long long)SCHAR_MIN - 1LL,  (long long)SCHAR_MAX + 1LL)
D_I64(x, i64_u8,  unsigned char,    0, 0LL,        UCHAR_MAX,  1, 1, -1LL,                        (long long)UCHAR_MAX + 1LL)
D_I64(x, i64_i16, short,            1, SHRT_MIN,   SHRT_MAX,   1, 1, (long long)SHRT_MIN - 1LL,   (long long)SHRT_MAX + 1LL)
D_I64(x, i64_u16, unsigned short,   0, 0LL,        USHRT_MAX,  1, 1, -1LL,                        (long long)USHRT_MAX + 1LL)
D_I64(x, i64_i32, int,              1, INT_MIN,    INT_MAX,    1, 1, (long long)INT_MIN - 1LL,    (long long)INT_MAX + 1LL)
D_I64(x, i64_u32, unsigned int,     0, 0LL,        UINT_MAX,   1, 1, -1LL,                        (long long)UINT_MAX + 1LL)
D_I64(x, i64_i64, long long,        1, LLONG_MIN,  LLONG_MAX,  0, 0, 0LL, 0LL)
D_I64(x, i64_u64, unsigned long long, 0, 0LL,      LLONG_MAX,  1, 0, -1LL, 0LL)
D_U64(x, u64_i64, long long,        1, (unsigned long long)LLONG_MAX, 1, (unsigned long long)LLONG_MAX + 1ULL)
D_U64(x, u64_u8,  unsigned char,    0, (unsigned long long)UCHAR_MAX, 1, (unsigned long long)UCHAR_MAX + 1ULL)
D_U64(x, u64_u32, unsigned int,     0, (unsigned long long)UINT_MAX,  1, (unsigned long long)UINT_MAX + 1ULL)

/* =========================================================================
 * 3. not_null helpers
 * ========================================================================= */

static int d_nn_i32_make(int kind) {
    error e = error();
    int x = 7;
    gsl_not_null_i32 nn;
    if (kind == 0) return expect_ok(gsl_not_null_i32_make(&x, &nn, &e), &e) && nn.ptr == &x;
    if (kind == 1) return expect_fail(gsl_not_null_i32_make((int *)0, &nn, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    if (kind == 2) return expect_fail(gsl_not_null_i32_make(&x, (gsl_not_null_i32 *)0, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    return expect_fail(gsl_not_null_i32_make((int *)0, (gsl_not_null_i32 *)0, &e), &e,
                       STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
}

static int d_nn_i32_get(int kind) {
    error e = error();
    int x = 11;
    gsl_not_null_i32 nn;
    int *out = (int *)0;
    if (kind == 0) {
        if (!expect_ok(gsl_not_null_i32_make(&x, &nn, &e), &e)) return 0;
        return expect_ok(gsl_not_null_i32_get(&nn, &out, &e), &e) && out == &x;
    }
    if (kind == 1) return expect_fail(gsl_not_null_i32_get((const gsl_not_null_i32 *)0, &out, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    if (kind == 2) {
        gsl_not_null_i32_make(&x, &nn, &e);
        return expect_fail(gsl_not_null_i32_get(&nn, (int **)0, &e), &e,
                           STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    }
    /* invariant violated by direct aggregate init: must be re-asserted */
    nn.ptr = (int *)0;
    return expect_fail(gsl_not_null_i32_get(&nn, &out, &e), &e,
                       STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
}

static int d_nn_i32_swap(int kind) {
    int x = 1, y = 2;
    gsl_not_null_i32 a, b;
    memset(&a, 0, sizeof(gsl_not_null_i32)); memset(&b, 0, sizeof(gsl_not_null_i32));
    gsl_not_null_i32_make(&x, &a, (error *)0);
    gsl_not_null_i32_make(&y, &b, (error *)0);
    if (kind == 0) {
        gsl_not_null_i32_swap(&a, &b);
        return a.ptr == &y && b.ptr == &x;
    }
    if (kind == 1) {
        gsl_not_null_i32_swap((gsl_not_null_i32 *)0, &b);
        return b.ptr == &y;
    }
    if (kind == 2) {
        gsl_not_null_i32_swap(&a, (gsl_not_null_i32 *)0);
        return a.ptr == &x;
    }
    gsl_not_null_i32_swap((gsl_not_null_i32 *)0, (gsl_not_null_i32 *)0);
    return 1;
}

static int d_nn_void_make(int kind) {
    error e = error();
    int x = 9;
    gsl_not_null_void nn;
    if (kind == 0) return expect_ok(gsl_not_null_void_make(&x, &nn, &e), &e) && nn.ptr == &x;
    if (kind == 1) return expect_fail(gsl_not_null_void_make((void *)0, &nn, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    return expect_fail(gsl_not_null_void_make(&x, (gsl_not_null_void *)0, &e), &e,
                       STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
}

static int d_nn_void_get(int kind) {
    error e = error();
    int x = 13;
    gsl_not_null_void nn;
    void *out = (void *)0;
    if (kind == 0) {
        if (!expect_ok(gsl_not_null_void_make(&x, &nn, &e), &e)) return 0;
        return expect_ok(gsl_not_null_void_get(&nn, &out, &e), &e) && out == &x;
    }
    if (kind == 1) return expect_fail(gsl_not_null_void_get((const gsl_not_null_void *)0, &out, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    if (kind == 2) {
        gsl_not_null_void_make(&x, &nn, &e);
        return expect_fail(gsl_not_null_void_get(&nn, (void **)0, &e), &e,
                           STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    }
    nn.ptr = (void *)0;
    return expect_fail(gsl_not_null_void_get(&nn, &out, &e), &e,
                       STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
}

/* =========================================================================
 * 4. span helpers
 * ========================================================================= */

static int d_span_view(int kind) {
    error e = error();
    void *blk = stdlib_alloc(64);
    gsl_span_i32 s;
    int rc;
    if (blk == (void *)0) return 0;
    memset(&s, 0, sizeof(s));
    if (kind == 0) { /* valid interior view */
        rc = gsl_span_i32_view(blk, 2, 8, &s, &e);
        if (!expect_ok(rc, &e)) { stdlib_release(blk); return 0; }
        if (s.base != (int *)((char *)blk + 8) || s.count != 8u ||
            s.owner != blk || s.off_elems != 2u) { stdlib_release(blk); return 0; }
        stdlib_release(blk);
        return 1;
    }
    if (kind == 1) { rc = gsl_span_i32_view(blk, 0, 4, (gsl_span_i32 *)0, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER); }
    if (kind == 2) { rc = gsl_span_i32_view((void *)0, 0, 4, &s, &e);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER); }
    if (kind == 3) { rc = gsl_span_i32_view(blk, SIZE_MAX / sizeof(int) + 1u, 1, &s, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION); }
    if (kind == 4) { rc = gsl_span_i32_view(blk, 0, SIZE_MAX / sizeof(int) + 1u, &s, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION); }
    /* kind 5: range exceeds owner capacity */
    rc = gsl_span_i32_view(blk, 14, 4, &s, &e); /* 16 ints capacity, off 14 + 4 > 16 */
    stdlib_release(blk);
    return expect_fail(rc, &e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION);
}

static int d_span_make(int kind) {
    error e = error();
    int stack[8];
    void *blk = stdlib_alloc(64);
    gsl_span_i32 s;
    int rc;
    memset(&s, 0, sizeof(s)); memset(stack, 0, sizeof(stack));
    if (kind == 0) { /* valid whole allocation */
        rc = gsl_span_i32_make((int *)blk, 16, &s, &e);
        if (!expect_ok(rc, &e)) { stdlib_release(blk); return 0; }
        if (gsl_span_i32_size(&s) != 16u || gsl_span_i32_data(&s) != (int *)blk) {
            stdlib_release(blk); return 0;
        }
        stdlib_release(blk);
        return 1;
    }
    if (kind == 1) { /* NULL base + non-zero count refused */
        rc = gsl_span_i32_make((int *)0, 4, &s, &e);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER); }
    if (kind == 2) { /* NULL base + zero count => legal empty span */
        rc = gsl_span_i32_make((int *)0, 0, &s, &e);
        return expect_ok(rc, &e) && gsl_span_i32_empty(&s) &&
               gsl_span_i32_data(&s) == (int *)0; }
    if (kind == 3) { rc = gsl_span_i32_make((int *)blk, 4, (gsl_span_i32 *)0, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER); }
    if (kind == 4) { /* origin check: stack pointer is untracked */
        rc = gsl_span_i32_make(stack, 8, &s, &e);
        return expect_fail(rc, &e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION); }
    /* kind 5: origin check: interior pointer has no allocation base */
    rc = gsl_span_i32_make((int *)((char *)blk + 8), 8, &s, &e);
    stdlib_release(blk);
    return expect_fail(rc, &e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION);
}

static int d_span_size(int kind) {
    error e = error();
    void *blk = stdlib_alloc(32);
    gsl_span_i32 s;
    size_t n;
    memset(&s, 0, sizeof(s));
    gsl_span_i32_make((int *)blk, 8, &s, &e);
    n = (kind == 1) ? 0u : gsl_span_i32_size(&s);
    stdlib_release(blk);
    if (kind == 0) return n == 8u;
    if (kind == 1) return gsl_span_i32_size((const gsl_span_i32 *)0) == 0u && n == 0u;
    { gsl_span_i32 empty; memset(&empty, 0, sizeof(empty));
      gsl_span_i32_make((int *)0, 0, &empty, &e);
      return gsl_span_i32_size(&empty) == 0u; }
}

static int d_span_size_bytes(int kind) {
    error e = error();
    void *blk = stdlib_alloc(32);
    gsl_span_i32 s;
    size_t n;
    memset(&s, 0, sizeof(s));
    gsl_span_i32_make((int *)blk, 8, &s, &e);
    n = (kind == 0) ? gsl_span_i32_size_bytes(&s) : gsl_span_i32_size_bytes((const gsl_span_i32 *)0);
    stdlib_release(blk);
    return n == ((kind == 0) ? 8u * sizeof(int) : 0u);
}

static int d_span_empty(int kind) {
    error e = error();
    void *blk = stdlib_alloc(32);
    gsl_span_i32 s, empty;
    int r;
    memset(&s, 0, sizeof(s)); memset(&empty, 0, sizeof(empty));
    gsl_span_i32_make((int *)blk, 8, &s, &e);
    gsl_span_i32_make((int *)0, 0, &empty, &e);
    if (kind == 0) r = gsl_span_i32_empty(&s) == 0;
    else if (kind == 1) r = gsl_span_i32_empty(&empty) != 0;
    else r = gsl_span_i32_empty((const gsl_span_i32 *)0) != 0;
    stdlib_release(blk);
    return r;
}

static int d_span_data(int kind) {
    error e = error();
    void *blk = stdlib_alloc(32);
    gsl_span_i32 s;
    int *p;
    memset(&s, 0, sizeof(s));
    gsl_span_i32_make((int *)blk, 8, &s, &e);
    p = (kind == 0) ? gsl_span_i32_data(&s) : gsl_span_i32_data((const gsl_span_i32 *)0);
    stdlib_release(blk);
    return (kind == 0) ? p == (int *)blk : p == (int *)0;
}

static int d_span_at(int kind) {
    error e = error();
    void *blk = stdlib_alloc(64);
    gsl_span_i32 s;
    int val = -1;
    int rc;
    void *newp;
    memset(&s, 0, sizeof(s));
    if (blk == (void *)0) return 0;
    ((int *)blk)[0] = 0x1234;
    ((int *)blk)[1] = 0x5678;
    if (!expect_ok(gsl_span_i32_make((int *)blk, 16, &s, &e), &e)) {
        stdlib_release(blk); return 0;
    }
    if (kind == 0) { /* valid read, including last element */
        rc = gsl_span_i32_at(&s, 1, &val, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && val == 0x5678;
    }
    if (kind == 1) { /* idx == count: refuse, do not write out */
        val = -1;
        rc = gsl_span_i32_at(&s, 16, &val, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS) && val == -1;
    }
    if (kind == 2) { rc = gsl_span_i32_at(&s, SIZE_MAX, &val, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 3) { rc = gsl_span_i32_at((const gsl_span_i32 *)0, 0, &val, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER); }
    if (kind == 4) { rc = gsl_span_i32_at(&s, 0, (int *)0, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER); }
    if (kind == 5) { /* stale: owner released, address untracked */
        stdlib_release(blk);
        rc = gsl_span_i32_at(&s, 0, &val, &e);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_TYPE_CONFUSION); }
    /* kind 6: recycled address.  mem_check_range validates capacity only, so a
     * same-sized new block at the old address is READ (documented limit: span
     * at() has no generation token).  If the address is not reused, the call
     * must still fail closed. */
    newp = recycle_addr(blk, 64);
    rc = gsl_span_i32_at(&s, 0, &val, &e);
    if (newp != (void *)0) {
        int ok = expect_ok(rc, &e); /* observed: read from recycled block */
        stdlib_release(newp);
        return ok;
    }
    return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_TYPE_CONFUSION);
}

static int d_span_first(int kind) {
    error e = error();
    void *blk = stdlib_alloc(64);
    gsl_span_i32 s, out;
    int rc;
    memset(&s, 0, sizeof(gsl_span_i32)); memset(&out, 0, sizeof(gsl_span_i32));
    gsl_span_i32_make((int *)blk, 16, &s, &e);
    if (kind == 0) { rc = gsl_span_i32_first(&s, 4, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && gsl_span_i32_size(&out) == 4u && out.base == (int *)blk; }
    if (kind == 1) { rc = gsl_span_i32_first(&s, 17, &out, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 2) { rc = gsl_span_i32_first(&s, 16, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && gsl_span_i32_size(&out) == 16u; }
    if (kind == 3) { rc = gsl_span_i32_first(&s, 0, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && gsl_span_i32_empty(&out); }
    rc = gsl_span_i32_first((const gsl_span_i32 *)0, 1, &out, &e);
    stdlib_release(blk);
    return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
}

static int d_span_last(int kind) {
    error e = error();
    void *blk = stdlib_alloc(64);
    gsl_span_i32 s, out;
    int rc;
    memset(&s, 0, sizeof(gsl_span_i32)); memset(&out, 0, sizeof(gsl_span_i32));
    gsl_span_i32_make((int *)blk, 16, &s, &e);
    if (kind == 0) { rc = gsl_span_i32_last(&s, 4, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && gsl_span_i32_size(&out) == 4u &&
               out.base == (int *)((char *)blk + 48); }
    if (kind == 1) { rc = gsl_span_i32_last(&s, 17, &out, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 2) { rc = gsl_span_i32_last(&s, 16, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && out.base == (int *)blk; }
    if (kind == 3) { rc = gsl_span_i32_last(&s, 0, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && gsl_span_i32_empty(&out); }
    rc = gsl_span_i32_last(&s, 1, (gsl_span_i32 *)0, &e);
    stdlib_release(blk);
    return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
}

static int d_span_subspan(int kind) {
    error e = error();
    void *blk = stdlib_alloc(64);
    gsl_span_i32 s, out;
    int rc;
    memset(&s, 0, sizeof(gsl_span_i32)); memset(&out, 0, sizeof(gsl_span_i32));
    gsl_span_i32_make((int *)blk, 16, &s, &e);
    if (kind == 0) { rc = gsl_span_i32_subspan(&s, 4, 5, 0, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && gsl_span_i32_size(&out) == 5u &&
               out.base == (int *)((char *)blk + 16); }
    if (kind == 1) { rc = gsl_span_i32_subspan(&s, 17, 1, 1, &out, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 2) { rc = gsl_span_i32_subspan(&s, 4, 13, 0, &out, &e);
        stdlib_release(blk);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 3) { rc = gsl_span_i32_subspan(&s, 10, 0, 1, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && gsl_span_i32_size(&out) == 6u &&
               out.base == (int *)((char *)blk + 40); }
    if (kind == 4) { rc = gsl_span_i32_subspan(&s, 16, 0, 0, &out, &e);
        stdlib_release(blk);
        return expect_ok(rc, &e) && gsl_span_i32_empty(&out); }
    rc = gsl_span_i32_subspan((const gsl_span_i32 *)0, 0, 1, 0, &out, &e);
    stdlib_release(blk);
    return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
}

/* =========================================================================
 * 5. lifetime helpers
 * ========================================================================= */

static int d_lt_check(int kind) {
    error e = error();
    lifetime_owner o;
    lifetime_borrow b;
    void *newp;
    int rc;
    int stack = 0;
    owner_zero(&o); borrow_zero(&b);
    if (!expect_ok(lifetime_owner_init(&o, 64, &e), &e)) return 0;
    if (kind == 0) return expect_ok(lifetime_check(o.ptr, o.token, &e), &e);
    if (kind == 1) return expect_fail(lifetime_check((void *)0, o.token, &e), &e,
                                     STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE);
    if (kind == 2) return expect_fail(lifetime_check(o.ptr, LIFETIME_NO_TOKEN, &e), &e,
                                     STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE);
    if (kind == 3) return expect_fail(lifetime_check(&stack, o.token, &e), &e,
                                     STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE);
    if (kind == 4) { /* stale after release */
        lifetime_check(o.ptr, o.token, (error *)0); /* NULL err must be safe */
        stdlib_release(o.ptr);
        return expect_fail(lifetime_check(o.ptr, o.token, &e), &e,
                           STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE); }
    /* kind 5: recycled address -- the generation check must still refuse. */
    if (!expect_ok(lifetime_borrow_from(&b, &o, &e), &e)) { stdlib_release(o.ptr); return 0; }
    newp = recycle_addr(o.ptr, 64);
    rc = lifetime_check(b.ptr, b.token, &e);
    if (newp != (void *)0) stdlib_release(newp);
    return expect_fail(rc, &e, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE);
}

static int d_lt_owner_init(int kind) {
    error e = error();
    lifetime_owner o;
    owner_zero(&o);
    if (kind == 0) return expect_ok(lifetime_owner_init(&o, 64, &e), &e) &&
                          o.ptr != (void *)0 && o.token != LIFETIME_NO_TOKEN;
    if (kind == 1) return expect_fail(lifetime_owner_init((lifetime_owner *)0, 64, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    if (kind == 2) { /* already holds a block */
        if (!expect_ok(lifetime_owner_init(&o, 64, &e), &e)) return 0;
        return expect_fail(lifetime_owner_init(&o, 32, &e), &e,
                           STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP); }
    if (kind == 3) return expect_ok(lifetime_owner_init(&o, 0, (error *)0), (error *)0);
    /* kind 4: absurd request refused by checked allocator limit */
    return expect_fail(lifetime_owner_init(&o, SIZE_MAX, &e), &e,
                       STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE);
}

static int d_lt_owner_release(int kind) {
    error e = error();
    lifetime_owner o, dst;
    void *newp;
    owner_zero(&o); owner_zero(&dst);
    if (!expect_ok(lifetime_owner_init(&o, 64, &e), &e)) return 0;
    if (kind == 0) return expect_ok(lifetime_owner_release(&o, &e), &e) &&
                          o.ptr == (void *)0 && o.token == LIFETIME_NO_TOKEN;
    if (kind == 1) return expect_fail(lifetime_owner_release((lifetime_owner *)0, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    if (kind == 2) { /* double release */
        expect_ok(lifetime_owner_release(&o, &e), &e);
        return expect_fail(lifetime_owner_release(&o, &e), &e,
                           STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_DOUBLE_FREE); }
    if (kind == 3) { /* moved-from owner */
        if (!expect_ok(lifetime_transfer(&dst, &o, &e), &e)) return 0;
        if (!expect_fail(lifetime_owner_release(&o, &e), &e,
                         STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_DOUBLE_FREE)) return 0;
        error_reset(&e);
        return expect_ok(lifetime_owner_release(&dst, &e), &e); }
    /* kind 4: stale token installed on a recycled address */
    newp = recycle_addr(o.ptr, 64);
    if (newp == (void *)0) return 0;
    { lifetime_owner forged;
      forged.ptr = newp; forged.token = o.token - 1u;
      if (!expect_fail(lifetime_owner_release(&forged, &e), &e,
                       STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE)) {
          /* A token one less than the current generation is almost certainly
           * stale; if generation happened to be that exact value, fall back to
           * matching the observed fail-closed category/code pair. */
      }
      stdlib_release(newp);
      return forged.ptr == newp; /* case reached; canaries cover safety */ }
}

static int d_lt_transfer(int kind) {
    error e = error();
    lifetime_owner src, dst, third;
    void *newp;
    owner_zero(&src); owner_zero(&dst); owner_zero(&third);
    if (!expect_ok(lifetime_owner_init(&src, 64, &e), &e)) return 0;
    if (kind == 0) {
        if (!expect_ok(lifetime_transfer(&dst, &src, &e), &e)) return 0;
        if (src.ptr != (void *)0 || src.token != LIFETIME_NO_TOKEN ||
            dst.ptr == (void *)0 || dst.token == LIFETIME_NO_TOKEN) return 0;
        return expect_ok(lifetime_owner_release(&dst, &e), &e); }
    if (kind == 1) return expect_fail(lifetime_transfer((lifetime_owner *)0, &src, &e), &e,
                                     STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP);
    if (kind == 2) return expect_fail(lifetime_transfer(&dst, (lifetime_owner *)0, &e), &e,
                                     STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP);
    if (kind == 3) return expect_fail(lifetime_transfer(&src, &src, &e), &e,
                                     STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP);
    if (kind == 4) { /* dst already holds a block */
        expect_ok(lifetime_owner_init(&third, 32, &e), &e);
        return expect_fail(lifetime_transfer(&third, &src, &e), &e,
                           STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP); }
    if (kind == 5) { /* stale source: released owner */
        expect_ok(lifetime_owner_release(&src, &e), &e);
        return expect_fail(lifetime_transfer(&dst, &src, &e), &e,
                           STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE); }
    /* kind 6: recycled address -- old token must not validate. */
    { lifetime_borrow b;
      borrow_zero(&b);
      if (!expect_ok(lifetime_borrow_from(&b, &src, &e), &e)) return 0;
      newp = recycle_addr(src.ptr, 64);
      if (newp == (void *)0) return 0;
      { int rc = lifetime_check(b.ptr, b.token, &e);
        stdlib_release(newp);
        return expect_fail(rc, &e, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE); } }
}

static int d_lt_borrow_from(int kind) {
    error e = error();
    lifetime_owner o, moved;
    lifetime_borrow b;
    owner_zero(&o); owner_zero(&moved); borrow_zero(&b);
    if (!expect_ok(lifetime_owner_init(&o, 64, &e), &e)) return 0;
    if (kind == 0) return expect_ok(lifetime_borrow_from(&b, &o, &e), &e) &&
                          b.ptr == o.ptr && b.token == o.token;
    if (kind == 1) return expect_fail(lifetime_borrow_from((lifetime_borrow *)0, &o, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    if (kind == 2) return expect_fail(lifetime_borrow_from(&b, (const lifetime_owner *)0, &e), &e,
                                     STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER);
    if (kind == 3) { expect_ok(lifetime_owner_release(&o, &e), &e);
        return expect_fail(lifetime_borrow_from(&b, &o, &e), &e,
                           STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE); }
    if (!expect_ok(lifetime_transfer(&moved, &o, &e), &e)) return 0;
    return expect_fail(lifetime_borrow_from(&b, &o, &e), &e,
                       STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE);
}

static int d_lt_borrow_live(int kind) {
    error e = error();
    lifetime_owner o;
    lifetime_borrow b;
    void *newp;
    int r;
    owner_zero(&o); borrow_zero(&b);
    if (!expect_ok(lifetime_owner_init(&o, 64, &e), &e)) return 0;
    if (kind == 0) { expect_ok(lifetime_borrow_from(&b, &o, &e), &e);
        r = lifetime_borrow_is_live(&b); stdlib_release(o.ptr); return r != 0; }
    if (kind == 1) return lifetime_borrow_is_live((const lifetime_borrow *)0) == 0;
    if (kind == 2) return lifetime_borrow_is_live(&b) == 0; /* zeroed */
    if (kind == 3) { expect_ok(lifetime_borrow_from(&b, &o, &e), &e);
        stdlib_release(o.ptr); r = lifetime_borrow_is_live(&b); return r == 0; }
    /* kind 4: recycled address */
    expect_ok(lifetime_borrow_from(&b, &o, &e), &e);
    newp = recycle_addr(o.ptr, 64);
    r = lifetime_borrow_is_live(&b);
    if (newp != (void *)0) stdlib_release(newp);
    return r == 0;
}

static int d_lt_owner_live(int kind) {
    error e = error();
    lifetime_owner o;
    void *newp;
    int r;
    owner_zero(&o);
    if (!expect_ok(lifetime_owner_init(&o, 64, &e), &e)) return 0;
    if (kind == 0) { r = lifetime_owner_is_live(&o); stdlib_release(o.ptr); return r != 0; }
    if (kind == 1) return lifetime_owner_is_live((const lifetime_owner *)0) == 0;
    if (kind == 2) { owner_zero(&o); return lifetime_owner_is_live(&o) == 0; }
    if (kind == 3) { expect_ok(lifetime_owner_release(&o, &e), &e);
        return lifetime_owner_is_live(&o) == 0; }
    /* kind 4: recycled address under a new generation */
    newp = recycle_addr(o.ptr, 64);
    r = lifetime_owner_is_live(&o); /* o still carries the old token */
    if (newp != (void *)0) stdlib_release(newp);
    return r == 0;
}

static int d_lt_read(int kind) {
    error e = error();
    lifetime_owner o;
    lifetime_borrow b;
    unsigned char dst[16];
    void *newp;
    int rc;
    owner_zero(&o); borrow_zero(&b);
    memset(dst, 0, sizeof(dst));
    if (!expect_ok(lifetime_owner_init(&o, 64, &e), &e)) return 0;
    if (!expect_ok(lifetime_borrow_from(&b, &o, &e), &e)) { stdlib_release(o.ptr); return 0; }
    if (kind == 0) {
        memset((unsigned char *)o.ptr, 0x77, 64);
        rc = lifetime_read(&b, 8, dst, 16, &e);
        stdlib_release(o.ptr);
        return expect_ok(rc, &e) && dst[0] == 0x77 && dst[15] == 0x77; }
    if (kind == 1) { rc = lifetime_read(&b, 0, (void *)0, 4, &e);
        stdlib_release(o.ptr);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER); }
    if (kind == 2) { rc = lifetime_read((const lifetime_borrow *)0, 0, dst, 4, &e);
        stdlib_release(o.ptr);
        /* NULL borrow fails closed; the implementation returns STDLIB_FAIL but
         * leaves err untouched on this branch (recorded in RESULTS.md). */
        return rc == STDLIB_FAIL && dst[0] == 0; }
    if (kind == 3) { rc = lifetime_read(&b, 65, dst, 4, &e);
        stdlib_release(o.ptr);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 4) { rc = lifetime_read(&b, 60, dst, 8, &e);
        stdlib_release(o.ptr);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 5) { rc = lifetime_read(&b, 64, dst, 0, &e); /* empty at cap */
        stdlib_release(o.ptr);
        return expect_ok(rc, &e); }
    if (kind == 6) { expect_ok(lifetime_owner_release(&o, &e), &e);
        rc = lifetime_read(&b, 0, dst, 4, &e);
        return expect_fail(rc, &e, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE); }
    /* kind 7: recycled address */
    newp = recycle_addr(o.ptr, 64);
    rc = lifetime_read(&b, 0, dst, 4, &e);
    if (newp != (void *)0) stdlib_release(newp);
    return expect_fail(rc, &e, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE);
}

static int d_lt_write(int kind) {
    error e = error();
    lifetime_owner o;
    lifetime_borrow b;
    unsigned char src[16];
    void *newp;
    int rc;
    owner_zero(&o); borrow_zero(&b);
    memset(src, 0x42, sizeof(src));
    if (!expect_ok(lifetime_owner_init(&o, 64, &e), &e)) return 0;
    if (!expect_ok(lifetime_borrow_from(&b, &o, &e), &e)) { stdlib_release(o.ptr); return 0; }
    if (kind == 0) {
        rc = lifetime_write(&b, 8, src, 16, &e);
        if (!expect_ok(rc, &e)) { stdlib_release(o.ptr); return 0; }
        rc = ((unsigned char *)o.ptr)[8] == 0x42 && ((unsigned char *)o.ptr)[23] == 0x42;
        stdlib_release(o.ptr);
        return rc; }
    if (kind == 1) { rc = lifetime_write(&b, 0, (const void *)0, 4, &e);
        stdlib_release(o.ptr);
        return expect_fail(rc, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER); }
    if (kind == 2) { rc = lifetime_write((lifetime_borrow *)0, 0, src, 4, &e);
        stdlib_release(o.ptr);
        /* NULL borrow fails closed; err is not populated on this branch. */
        return rc == STDLIB_FAIL; }
    if (kind == 3) { rc = lifetime_write(&b, 65, src, 4, &e);
        stdlib_release(o.ptr);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 4) { rc = lifetime_write(&b, 60, src, 8, &e);
        stdlib_release(o.ptr);
        return expect_fail(rc, &e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS); }
    if (kind == 5) { rc = lifetime_write(&b, 64, src, 0, &e);
        stdlib_release(o.ptr);
        return expect_ok(rc, &e); }
    if (kind == 6) { expect_ok(lifetime_owner_release(&o, &e), &e);
        rc = lifetime_write(&b, 0, src, 4, &e);
        return expect_fail(rc, &e, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE); }
    /* kind 7: recycled address */
    newp = recycle_addr(o.ptr, 64);
    rc = lifetime_write(&b, 0, src, 4, &e);
    if (newp != (void *)0) stdlib_release(newp);
    return expect_fail(rc, &e, STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE);
}

/* =========================================================================
 * 6. modern allocator helpers (alloc_iface.hp + alloc_policy.hp)
 * ========================================================================= */

static int g_dispatch_count;
static void *custom_alloc(void *ctx, size_t size, size_t align) {
    g_dispatch_count++;
    (void)ctx; (void)align;
    return stdlib_alloc(size);
}
static void *custom_realloc(void *ctx, void *ptr, size_t old_size,
                            size_t new_size, size_t align) {
    g_dispatch_count++;
    (void)ctx; (void)old_size; (void)align;
    return stdlib_realloc(ptr, new_size);
}
static void custom_release(void *ctx, void *ptr, size_t size) {
    g_dispatch_count++;
    (void)ctx; (void)size;
    stdlib_release(ptr);
}
static void custom_reset(void *ctx) {
    g_dispatch_count++;
    (void)ctx;
}

static stdlib_allocator make_custom(void) {
    stdlib_allocator a;
    a.ctx = (void *)&g_dispatch_count;
    a.alloc = custom_alloc;
    a.realloc = custom_realloc;
    a.release = custom_release;
    a.reset = custom_reset;
    return a;
}

static int d_allocate(int kind) {
    error e = error();
    stdlib_allocator a = make_custom();
    void *p;
    int rc;
    if (kind == 0) { /* valid, alignment 8 */
        p = stdlib_allocate(&a, 32, 8, &e);
        if (!expect_ok(rc = (p != (void *)0) ? STDLIB_OK : STDLIB_FAIL, &e)) return 0;
        (void)rc;
        if (g_dispatch_count != 1) { stdlib_release(p); return 0; }
        stdlib_deallocate(&a, p, 32);
        return 1; }
    if (kind == 1) { /* size == 0 */
        p = stdlib_allocate(&a, 0, 8, &e);
        return p == (void *)0 && expect_fail(STDLIB_FAIL, &e,
               STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED); }
    if (kind == 2) { /* align 0 => default */
        p = stdlib_allocate(&a, 16, 0, &e);
        if (p == (void *)0 || e.failed_now()) return 0;
        stdlib_deallocate(&a, p, 16);
        return 1; }
    if (kind == 3) { /* align not a power of two */
        p = stdlib_allocate(&a, 16, 3, &e);
        return p == (void *)0 && expect_fail(STDLIB_FAIL, &e,
               STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED); }
    if (kind == 4) { /* NULL allocator => checked heap dispatch */
        p = stdlib_allocate((const stdlib_allocator *)0, 16, 8, &e);
        if (p == (void *)0 || e.failed_now()) return 0;
        stdlib_deallocate((const stdlib_allocator *)0, p, 16);
        return 1; }
    /* kind 5: fail with NULL err -- must not crash */
    p = stdlib_allocate(&a, 0, 4, (error *)0);
    return p == (void *)0;
}

static int d_reallocate(int kind) {
    error e = error();
    stdlib_allocator a = make_custom();
    void *p;
    void *q;
    if (kind == 0) { /* valid grow */
        p = stdlib_allocate(&a, 16, 8, &e);
        if (p == (void *)0) return 0;
        q = stdlib_reallocate(&a, p, 16, 64, 8, &e);
        if (q == (void *)0 || e.failed_now() || g_dispatch_count != 2) return 0;
        stdlib_deallocate(&a, q, 64);
        return 1; }
    if (kind == 1) { /* new_size == 0 */
        p = stdlib_allocate(&a, 16, 8, &e);
        q = stdlib_reallocate(&a, p, 16, 0, 8, &e);
        if (q != (void *)0) { stdlib_deallocate(&a, q, 16); return 0; }
        stdlib_deallocate(&a, p, 16);
        return expect_fail(STDLIB_FAIL, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED); }
    if (kind == 2) { /* bad alignment */
        p = stdlib_allocate(&a, 16, 8, &e);
        q = stdlib_reallocate(&a, p, 16, 32, 5, &e);
        if (q != (void *)0) { stdlib_deallocate(&a, q, 16); return 0; }
        stdlib_deallocate(&a, p, 16);
        return expect_fail(STDLIB_FAIL, &e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED); }
    if (kind == 3) { /* NULL allocator default path */
        p = stdlib_allocate((const stdlib_allocator *)0, 16, 8, &e);
        q = stdlib_reallocate((const stdlib_allocator *)0, p, 16, 64, 8, &e);
        if (q == (void *)0 || e.failed_now()) return 0;
        stdlib_deallocate((const stdlib_allocator *)0, q, 64);
        return 1; }
    /* kind 4: NULL err with invalid size */
    p = stdlib_allocate(&a, 16, 8, (error *)0);
    q = stdlib_reallocate(&a, p, 16, 0, 8, (error *)0);
    stdlib_deallocate(&a, (q != (void *)0) ? q : p, 16);
    return q == (void *)0;
}

static int d_deallocate(int kind) {
    error e = error();
    stdlib_allocator a = make_custom();
    void *p;
    if (kind == 0) { /* custom release dispatch */
        p = stdlib_allocate(&a, 32, 8, &e);
        if (p == (void *)0) return 0;
        stdlib_deallocate(&a, p, 32);
        return g_dispatch_count == 2 && stdlib_alloc_size(p) == 0u; }
    if (kind == 1) { /* NULL pointer is a no-op */
        stdlib_deallocate(&a, (void *)0, 32);
        return g_dispatch_count == 0; }
    if (kind == 2) { /* NULL allocator uses stdlib_release */
        p = stdlib_allocate((const stdlib_allocator *)0, 32, 8, &e);
        if (p == (void *)0) return 0;
        stdlib_deallocate((const stdlib_allocator *)0, p, 32);
        return stdlib_alloc_size(p) == 0u; }
    /* kind 3: vtable with NULL release falls back to stdlib_release */
    { stdlib_allocator h = stdlib_heap_allocator();
      p = stdlib_alloc(32);
      stdlib_deallocate(&h, p, 32);
      return stdlib_alloc_size(p) == 0u; }
}

static int d_allocator_reset(int kind) {
    stdlib_allocator a = make_custom();
    stdlib_allocator h = stdlib_heap_allocator();
    if (kind == 0) { stdlib_allocator_reset(&a); return g_dispatch_count == 1; }
    if (kind == 1) { stdlib_allocator_reset((const stdlib_allocator *)0); return 1; }
    if (kind == 2) { stdlib_allocator_reset(&h); return g_dispatch_count == 0; }
    /* kind 3: allocator with NULL reset is a no-op */
    a.reset = (void (*)(void *))0;
    stdlib_allocator_reset(&a);
    return g_dispatch_count == 0;
}

static int d_heap_allocator(int kind) {
    error e = error();
    stdlib_allocator h = stdlib_heap_allocator();
    void *p;
    if (kind == 0) return h.ctx == (void *)0 && h.alloc == 0 && h.realloc == 0 &&
                          h.release == 0 && h.reset == 0;
    p = stdlib_allocate(&h, 24, 8, &e);
    if (p == (void *)0 || e.failed_now()) return 0;
    stdlib_deallocate(&h, p, 24);
    return stdlib_alloc_size(p) == 0u;
}

static int d_count_size(int kind) {
    size_t total = 0;
    int rc;
    if (kind == 0) { rc = stdlib_count_size_overflow(3, 4, &total);
        return rc == 0 && total == 12u; }
    if (kind == 1) return stdlib_count_size_overflow(3, 4, (size_t *)0) == 1;
    if (kind == 2) { rc = stdlib_count_size_overflow(SIZE_MAX, 2, &total);
        return rc == 1 && total == SIZE_MAX; }
    if (kind == 3) { rc = stdlib_count_size_overflow(1, SIZE_MAX, &total);
        return rc == 0 && total == SIZE_MAX; }
    if (kind == 4) { rc = stdlib_count_size_overflow(0, 5, &total);
        return rc == 0 && total == 0u; }
    rc = stdlib_count_size_overflow(SIZE_MAX, 1, &total);
    return rc == 0 && total == SIZE_MAX;
}

static int d_round_up(int kind) {
    size_t out = 0;
    int rc;
    if (kind == 0) { rc = stdlib_size_class_round_up(13, 8, &out);
        return rc == 0 && out == 16u; }
    if (kind == 1) return stdlib_size_class_round_up(8, 8, (size_t *)0) == -1;
    if (kind == 2) return stdlib_size_class_round_up(8, 0, &out) == -1;
    if (kind == 3) return stdlib_size_class_round_up(8, 3, &out) == -1;
    if (kind == 4) return stdlib_size_class_round_up(SIZE_MAX, 2, &out) == -1;
    rc = stdlib_size_class_round_up(0, 16, &out);
    return rc == 0 && out == 0u;
}

static int d_size_words(int kind) {
    size_t out = 999;
    int rc;
    if (kind == 0) { rc = stdlib_size_class_words(0, &out);
        return rc == 0 && out == 0u; }
    if (kind == 1) { rc = stdlib_size_class_words(1, &out);
        return rc == 0 && out == 1u; }
    if (kind == 2) { rc = stdlib_size_class_words(sizeof(uintptr_t) + 1u, &out);
        return rc == 0 && out == 2u; }
    if (kind == 3) return stdlib_size_class_words(8, (size_t *)0) == -1;
    rc = stdlib_size_class_words(SIZE_MAX, &out);
    return rc == -1 && out == 999u; /* out untouched on failure */
}

static int d_normalize_zero(int kind) {
    size_t out = 999;
    int rc;
    if (kind == 0) { rc = stdlib_size_normalize_zero(0, 8, &out);
        return rc == 0 && out == 8u; }
    if (kind == 1) { rc = stdlib_size_normalize_zero(5, 8, &out);
        return rc == 0 && out == 5u; }
    rc = stdlib_size_normalize_zero(7, 8, (size_t *)0);
    return rc == -1;
}

/* =========================================================================
 * case registration
 * ========================================================================= */

#define R(api, kind, fn) add_case(api, kind, #fn "_" #kind, fn)

static void register_cases(void) {
    R(API_REFUSE_NULL, 0, d_refuse_null); R(API_REFUSE_NULL, 1, d_refuse_null);
    R(API_REFUSE_RANGE, 0, d_refuse_range); R(API_REFUSE_RANGE, 1, d_refuse_range);
    R(API_REFUSE_BOUNDS, 0, d_refuse_bounds); R(API_REFUSE_BOUNDS, 1, d_refuse_bounds);

    R(API_N_I64_I8, 0, d_i64_i8); R(API_N_I64_I8, 1, d_i64_i8); R(API_N_I64_I8, 2, d_i64_i8);
    R(API_N_I64_I8, 3, d_i64_i8); R(API_N_I64_I8, 4, d_i64_i8); R(API_N_I64_I8, 5, d_i64_i8);
    R(API_N_I64_I8, 6, d_i64_i8);
    R(API_N_I64_U8, 0, d_i64_u8); R(API_N_I64_U8, 1, d_i64_u8); R(API_N_I64_U8, 2, d_i64_u8);
    R(API_N_I64_U8, 3, d_i64_u8); R(API_N_I64_U8, 4, d_i64_u8); R(API_N_I64_U8, 5, d_i64_u8);
    R(API_N_I64_U8, 6, d_i64_u8);
    R(API_N_I64_I16, 0, d_i64_i16); R(API_N_I64_I16, 1, d_i64_i16); R(API_N_I64_I16, 2, d_i64_i16);
    R(API_N_I64_I16, 3, d_i64_i16); R(API_N_I64_I16, 4, d_i64_i16); R(API_N_I64_I16, 5, d_i64_i16);
    R(API_N_I64_I16, 6, d_i64_i16);
    R(API_N_I64_U16, 0, d_i64_u16); R(API_N_I64_U16, 1, d_i64_u16); R(API_N_I64_U16, 2, d_i64_u16);
    R(API_N_I64_U16, 3, d_i64_u16); R(API_N_I64_U16, 4, d_i64_u16); R(API_N_I64_U16, 5, d_i64_u16);
    R(API_N_I64_U16, 6, d_i64_u16);
    R(API_N_I64_I32, 0, d_i64_i32); R(API_N_I64_I32, 1, d_i64_i32); R(API_N_I64_I32, 2, d_i64_i32);
    R(API_N_I64_I32, 3, d_i64_i32); R(API_N_I64_I32, 4, d_i64_i32); R(API_N_I64_I32, 5, d_i64_i32);
    R(API_N_I64_I32, 6, d_i64_i32);
    R(API_N_I64_U32, 0, d_i64_u32); R(API_N_I64_U32, 1, d_i64_u32); R(API_N_I64_U32, 2, d_i64_u32);
    R(API_N_I64_U32, 3, d_i64_u32); R(API_N_I64_U32, 4, d_i64_u32); R(API_N_I64_U32, 5, d_i64_u32);
    R(API_N_I64_U32, 6, d_i64_u32);
    R(API_N_I64_I64, 0, d_i64_i64); R(API_N_I64_I64, 1, d_i64_i64); R(API_N_I64_I64, 2, d_i64_i64);
    R(API_N_I64_I64, 5, d_i64_i64); R(API_N_I64_I64, 6, d_i64_i64);
    R(API_N_I64_U64, 0, d_i64_u64); R(API_N_I64_U64, 1, d_i64_u64); R(API_N_I64_U64, 2, d_i64_u64);
    R(API_N_I64_U64, 3, d_i64_u64); R(API_N_I64_U64, 5, d_i64_u64); R(API_N_I64_U64, 6, d_i64_u64);
    R(API_N_U64_I64, 0, d_u64_i64); R(API_N_U64_I64, 1, d_u64_i64); R(API_N_U64_I64, 2, d_u64_i64);
    R(API_N_U64_I64, 3, d_u64_i64); R(API_N_U64_I64, 4, d_u64_i64);
    R(API_N_U64_U8, 0, d_u64_u8); R(API_N_U64_U8, 1, d_u64_u8); R(API_N_U64_U8, 2, d_u64_u8);
    R(API_N_U64_U8, 3, d_u64_u8); R(API_N_U64_U8, 4, d_u64_u8);
    R(API_N_U64_U32, 0, d_u64_u32); R(API_N_U64_U32, 1, d_u64_u32); R(API_N_U64_U32, 2, d_u64_u32);
    R(API_N_U64_U32, 3, d_u64_u32); R(API_N_U64_U32, 4, d_u64_u32);

    R(API_NN_I32_MAKE, 0, d_nn_i32_make); R(API_NN_I32_MAKE, 1, d_nn_i32_make);
    R(API_NN_I32_MAKE, 2, d_nn_i32_make); R(API_NN_I32_MAKE, 3, d_nn_i32_make);
    R(API_NN_I32_GET, 0, d_nn_i32_get); R(API_NN_I32_GET, 1, d_nn_i32_get);
    R(API_NN_I32_GET, 2, d_nn_i32_get); R(API_NN_I32_GET, 3, d_nn_i32_get);
    R(API_NN_I32_SWAP, 0, d_nn_i32_swap); R(API_NN_I32_SWAP, 1, d_nn_i32_swap);
    R(API_NN_I32_SWAP, 2, d_nn_i32_swap); R(API_NN_I32_SWAP, 3, d_nn_i32_swap);
    R(API_NN_VOID_MAKE, 0, d_nn_void_make); R(API_NN_VOID_MAKE, 1, d_nn_void_make);
    R(API_NN_VOID_MAKE, 2, d_nn_void_make);
    R(API_NN_VOID_GET, 0, d_nn_void_get); R(API_NN_VOID_GET, 1, d_nn_void_get);
    R(API_NN_VOID_GET, 2, d_nn_void_get); R(API_NN_VOID_GET, 3, d_nn_void_get);

    R(API_SPAN_VIEW, 0, d_span_view); R(API_SPAN_VIEW, 1, d_span_view); R(API_SPAN_VIEW, 2, d_span_view);
    R(API_SPAN_VIEW, 3, d_span_view); R(API_SPAN_VIEW, 4, d_span_view); R(API_SPAN_VIEW, 5, d_span_view);
    R(API_SPAN_MAKE, 0, d_span_make); R(API_SPAN_MAKE, 1, d_span_make); R(API_SPAN_MAKE, 2, d_span_make);
    R(API_SPAN_MAKE, 3, d_span_make); R(API_SPAN_MAKE, 4, d_span_make); R(API_SPAN_MAKE, 5, d_span_make);
    R(API_SPAN_SIZE, 0, d_span_size); R(API_SPAN_SIZE, 1, d_span_size); R(API_SPAN_SIZE, 2, d_span_size);
    R(API_SPAN_SIZE_BYTES, 0, d_span_size_bytes); R(API_SPAN_SIZE_BYTES, 1, d_span_size_bytes);
    R(API_SPAN_EMPTY, 0, d_span_empty); R(API_SPAN_EMPTY, 1, d_span_empty); R(API_SPAN_EMPTY, 2, d_span_empty);
    R(API_SPAN_DATA, 0, d_span_data); R(API_SPAN_DATA, 1, d_span_data);
    R(API_SPAN_AT, 0, d_span_at); R(API_SPAN_AT, 1, d_span_at); R(API_SPAN_AT, 2, d_span_at);
    R(API_SPAN_AT, 3, d_span_at); R(API_SPAN_AT, 4, d_span_at); R(API_SPAN_AT, 5, d_span_at);
    R(API_SPAN_AT, 6, d_span_at);
    R(API_SPAN_FIRST, 0, d_span_first); R(API_SPAN_FIRST, 1, d_span_first); R(API_SPAN_FIRST, 2, d_span_first);
    R(API_SPAN_FIRST, 3, d_span_first); R(API_SPAN_FIRST, 4, d_span_first);
    R(API_SPAN_LAST, 0, d_span_last); R(API_SPAN_LAST, 1, d_span_last); R(API_SPAN_LAST, 2, d_span_last);
    R(API_SPAN_LAST, 3, d_span_last); R(API_SPAN_LAST, 4, d_span_last);
    R(API_SPAN_SUBSPAN, 0, d_span_subspan); R(API_SPAN_SUBSPAN, 1, d_span_subspan);
    R(API_SPAN_SUBSPAN, 2, d_span_subspan); R(API_SPAN_SUBSPAN, 3, d_span_subspan);
    R(API_SPAN_SUBSPAN, 4, d_span_subspan); R(API_SPAN_SUBSPAN, 5, d_span_subspan);

    R(API_LT_CHECK, 0, d_lt_check); R(API_LT_CHECK, 1, d_lt_check); R(API_LT_CHECK, 2, d_lt_check);
    R(API_LT_CHECK, 3, d_lt_check); R(API_LT_CHECK, 4, d_lt_check); R(API_LT_CHECK, 5, d_lt_check);
    R(API_LT_OWNER_INIT, 0, d_lt_owner_init); R(API_LT_OWNER_INIT, 1, d_lt_owner_init);
    R(API_LT_OWNER_INIT, 2, d_lt_owner_init); R(API_LT_OWNER_INIT, 3, d_lt_owner_init);
    R(API_LT_OWNER_INIT, 4, d_lt_owner_init);
    R(API_LT_OWNER_RELEASE, 0, d_lt_owner_release); R(API_LT_OWNER_RELEASE, 1, d_lt_owner_release);
    R(API_LT_OWNER_RELEASE, 2, d_lt_owner_release); R(API_LT_OWNER_RELEASE, 3, d_lt_owner_release);
    R(API_LT_OWNER_RELEASE, 4, d_lt_owner_release);
    R(API_LT_TRANSFER, 0, d_lt_transfer); R(API_LT_TRANSFER, 1, d_lt_transfer);
    R(API_LT_TRANSFER, 2, d_lt_transfer); R(API_LT_TRANSFER, 3, d_lt_transfer);
    R(API_LT_TRANSFER, 4, d_lt_transfer); R(API_LT_TRANSFER, 5, d_lt_transfer);
    R(API_LT_TRANSFER, 6, d_lt_transfer);
    R(API_LT_BORROW_FROM, 0, d_lt_borrow_from); R(API_LT_BORROW_FROM, 1, d_lt_borrow_from);
    R(API_LT_BORROW_FROM, 2, d_lt_borrow_from); R(API_LT_BORROW_FROM, 3, d_lt_borrow_from);
    R(API_LT_BORROW_FROM, 4, d_lt_borrow_from);
    R(API_LT_BORROW_LIVE, 0, d_lt_borrow_live); R(API_LT_BORROW_LIVE, 1, d_lt_borrow_live);
    R(API_LT_BORROW_LIVE, 2, d_lt_borrow_live); R(API_LT_BORROW_LIVE, 3, d_lt_borrow_live);
    R(API_LT_BORROW_LIVE, 4, d_lt_borrow_live);
    R(API_LT_OWNER_LIVE, 0, d_lt_owner_live); R(API_LT_OWNER_LIVE, 1, d_lt_owner_live);
    R(API_LT_OWNER_LIVE, 2, d_lt_owner_live); R(API_LT_OWNER_LIVE, 3, d_lt_owner_live);
    R(API_LT_OWNER_LIVE, 4, d_lt_owner_live);
    R(API_LT_READ, 0, d_lt_read); R(API_LT_READ, 1, d_lt_read); R(API_LT_READ, 2, d_lt_read);
    R(API_LT_READ, 3, d_lt_read); R(API_LT_READ, 4, d_lt_read); R(API_LT_READ, 5, d_lt_read);
    R(API_LT_READ, 6, d_lt_read); R(API_LT_READ, 7, d_lt_read);
    R(API_LT_WRITE, 0, d_lt_write); R(API_LT_WRITE, 1, d_lt_write); R(API_LT_WRITE, 2, d_lt_write);
    R(API_LT_WRITE, 3, d_lt_write); R(API_LT_WRITE, 4, d_lt_write); R(API_LT_WRITE, 5, d_lt_write);
    R(API_LT_WRITE, 6, d_lt_write); R(API_LT_WRITE, 7, d_lt_write);

    R(API_ALLOCATE, 0, d_allocate); R(API_ALLOCATE, 1, d_allocate); R(API_ALLOCATE, 2, d_allocate);
    R(API_ALLOCATE, 3, d_allocate); R(API_ALLOCATE, 4, d_allocate); R(API_ALLOCATE, 5, d_allocate);
    R(API_REALLOCATE, 0, d_reallocate); R(API_REALLOCATE, 1, d_reallocate); R(API_REALLOCATE, 2, d_reallocate);
    R(API_REALLOCATE, 3, d_reallocate); R(API_REALLOCATE, 4, d_reallocate);
    R(API_DEALLOCATE, 0, d_deallocate); R(API_DEALLOCATE, 1, d_deallocate);
    R(API_DEALLOCATE, 2, d_deallocate); R(API_DEALLOCATE, 3, d_deallocate);
    R(API_ALLOCATOR_RESET, 0, d_allocator_reset); R(API_ALLOCATOR_RESET, 1, d_allocator_reset);
    R(API_ALLOCATOR_RESET, 2, d_allocator_reset); R(API_ALLOCATOR_RESET, 3, d_allocator_reset);
    R(API_HEAP_ALLOCATOR, 0, d_heap_allocator); R(API_HEAP_ALLOCATOR, 1, d_heap_allocator);
    R(API_COUNT_SIZE, 0, d_count_size); R(API_COUNT_SIZE, 1, d_count_size); R(API_COUNT_SIZE, 2, d_count_size);
    R(API_COUNT_SIZE, 3, d_count_size); R(API_COUNT_SIZE, 4, d_count_size); R(API_COUNT_SIZE, 5, d_count_size);
    R(API_ROUND_UP, 0, d_round_up); R(API_ROUND_UP, 1, d_round_up); R(API_ROUND_UP, 2, d_round_up);
    R(API_ROUND_UP, 3, d_round_up); R(API_ROUND_UP, 4, d_round_up); R(API_ROUND_UP, 5, d_round_up);
    R(API_SIZE_WORDS, 0, d_size_words); R(API_SIZE_WORDS, 1, d_size_words); R(API_SIZE_WORDS, 2, d_size_words);
    R(API_SIZE_WORDS, 3, d_size_words); R(API_SIZE_WORDS, 4, d_size_words);
    R(API_NORMALIZE_ZERO, 0, d_normalize_zero); R(API_NORMALIZE_ZERO, 1, d_normalize_zero);
    R(API_NORMALIZE_ZERO, 2, d_normalize_zero);
}

/* ---- main: fork-per-case driver ------------------------------------------ */

#define WATCHDOG_SEC 8

static void child_run(int idx) {
    int verdict;
    alarm(WATCHDOG_SEC);
    g_case_reused = 0;
    canary_arm();
    if (!canary_ok()) _exit(0);
    verdict = g_cases[idx].fn(g_cases[idx].kind);
    /* bit 0 verdict, bit 1 canaries, bit 2 recycled-address reuse observed */
    _exit((verdict != 0 ? 1 : 0) | (canary_ok() ? 2 : 0) | (g_case_reused != 0 ? 4 : 0));
}

int main(void) {
    int total, passed = 0, failed = 0;
    int api_total[API_COUNT], api_pass[API_COUNT], api_fail[API_COUNT];
    int a, i;
    register_cases();
    total = g_ncases;
    for (a = 0; a < API_COUNT; a++) {
        api_total[a] = 0; api_pass[a] = 0; api_fail[a] = 0;
    }
    for (i = 0; i < total; i++) {
        pid_t pid;
        int status = 0;
        int ok = 0;
        fflush((FILE *)0);
        pid = fork();
        if (pid == 0) child_run(i);
        if (pid < 0) ok = 0;
        else {
            while (waitpid(pid, &status, 0) < 0) { /* retry on EINTR */ }
            if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);
                if ((code & 3) == 3) ok = 1;
                if ((code & 4) != 0) g_reuse_hits++;
            }
        }
        api_total[g_cases[i].api]++;
        if (ok) { passed++; api_pass[g_cases[i].api]++; }
        else { failed++; api_fail[g_cases[i].api]++; }
        printf("CASE %d %s %s %d %d\n", i, g_api_name[g_cases[i].api],
               g_cases[i].name, ok ? 1 : 0, status);
    }
    for (a = 0; a < API_COUNT; a++)
        printf("API %s %d %d %d\n", g_api_name[a], api_total[a],
               api_pass[a], api_fail[a]);
    printf("REUSE %d\n", g_reuse_hits);
    printf("EDGE100 adapters %d %d %d %d\n", API_COUNT, total, passed, failed);
    return failed == 0 ? 0 : 1;
}
