/* Per-method unit tests for EVERY allocation method in both ordinary-C safety
 * layers and the checked layer, with logging-does-not-change-semantics evidence.
 *
 * Build (SafeCRT / ordinary Safe-C layer, default):
 *   clang -O1 -g -std=c99 -Wall -Wextra -pedantic -I. -Istdlib/safe -Ilibc \
 *     -Ilibc/install-include -Ithird_party/safeclib/include \
 *     -Ithird_party/intel-safestringlib/include tests/methods/methods_alloc.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread \
 *     -o /tmp/m_alloc && /tmp/m_alloc
 * Build (plain libc layer): add -DPLAIN_LIBC and link ONLY libcplus_hardened.a.
 * Add -fsanitize=undefined -fno-sanitize-recover=all for the UBSan run.
 *
 * DESIGN
 * ------
 * Each method-scenario is a standalone function `m_alloc_<n>_<slug>` that
 * performs its allocation operations, frees everything it owns, and returns an
 * `obs_t` observation: `ret` is the (normalised) return value of the method
 * under test, `err` is the errno observed immediately after it, and `a`..`e`
 * are additional normalised observations (data readback, metadata values).
 * Pointer VALUES are never compared across runs (heap addresses legitimately
 * differ); only NULL-ness, sizes, contents and error codes are compared.
 *
 * The harness calls EVERY scenario TWICE: once with
 * `cplus_libc_log_set_enabled(0)` (+ `cplus_safe_log_set_level(OFF)` in the
 * SafeCRT layer) and once with logging enabled.  The two observations must be
 * bit-for-bit identical.  Each such pair counts as ONE check.
 *
 * NO GENUINE UB: invalid/double/foreign pointers are always passed to the
 * layer's *checked* free entry point (free_checked_s / cplus_free_checked) or
 * to realloc_s/cplus_realloc, which refuse the operation without touching the
 * raw allocator.  There is no raw double free, raw NULL deref, or
 * cross-allocator free.
 *
 * SENTINEL / LOGS: a fixed sentinel string is written into live buffers and
 * read back as full-write evidence.  stderr is captured through a tmpfile for
 * the whole run and the capture is asserted NOT to contain the sentinel.  (If
 * capture cannot be established, the fact is printed; both loggers only ever
 * emit fixed api/msg metadata -- see libc/log/log.c and stdlib/safe/src/
 * safe_log.c -- so a caller buffer can never be logged in the first place.)
 */
#define _POSIX_C_SOURCE 200112L

#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- layer abstraction --------------------------------------------------- */
#ifdef PLAIN_LIBC
#define L_aligned_alloc    cplus_aligned_alloc
#define L_meta_t           cplus_alloc_meta
#define L_get_meta         cplus_alloc_get_meta
#define L_is_live          cplus_alloc_is_live
#define L_ptr_size         cplus_alloc_size
#define L_ptr_elem_size    cplus_alloc_elem_size
#define L_ptr_count        cplus_alloc_length
#define L_check_generation cplus_alloc_check_generation
#define L_live_count       cplus_alloc_live_count
#define L_live_bytes       cplus_alloc_live_bytes
#define L_fail_after       cplus_alloc_fail_after
#define L_fail_reset       cplus_alloc_fail_reset
#define L_checked_free(p)  cplus_free_checked(&(p))
/* cplus_aligned_alloc forwards to the HOST allocator; its blocks must be
 * released with the host free(), never with cplus_free().  Temporarily undo
 * the ordinary-C safety macro for this one helper, then restore it. */
#undef free
static void aligned_free(void *p) { free(p); }
#define free(p) cplus_free(p)
#else
#define L_aligned_alloc    aligned_alloc_s
#define L_meta_t           cplus_safe_alloc_meta
#define L_get_meta         cplus_safe_alloc_get_meta
#define L_is_live          cplus_safe_ptr_is_live
#define L_ptr_size         cplus_safe_ptr_size
#define L_ptr_elem_size    cplus_safe_ptr_elem_size
#define L_ptr_count        cplus_safe_ptr_count
#define L_check_generation cplus_safe_ptr_check_generation
#define L_live_count       cplus_safe_alloc_live_count
#define L_live_bytes       cplus_safe_alloc_live_bytes
#define L_fail_after       cplus_safe_alloc_fail_after
#define L_fail_reset       cplus_safe_alloc_fail_reset
#define L_checked_free(p)  free_checked_s(p)
/* aligned_alloc_s blocks are tracked by the SafeCRT table; free_s releases
 * them with the matching host free() internally. */
static void aligned_free(void *p) { free_s(p); }
#endif

static void set_logging(int on) {
    cplus_libc_log_set_enabled(on);
#ifndef PLAIN_LIBC
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
#endif
}

/* ---- observation / harness helpers -------------------------------------- */
typedef struct {
    long long ret;             /* normalised return value of the method */
    int err;                   /* errno right after the method call */
    unsigned long long a, b, c, d, e; /* additional normalised observations */
} obs_t;

static obs_t obs_make(long long ret, int err,
                      unsigned long long a, unsigned long long b,
                      unsigned long long c, unsigned long long d,
                      unsigned long long e) {
    obs_t o;
    o.ret = ret; o.err = err; o.a = a; o.b = b; o.c = c; o.d = d; o.e = e;
    return o;
}

static int obs_eq(const obs_t *x, const obs_t *y) {
    return x->ret == y->ret && x->err == y->err &&
           x->a == y->a && x->b == y->b && x->c == y->c &&
           x->d == y->d && x->e == y->e;
}

static const char SENTINEL[] = "METHODS_ALLOC_SENTINEL_c4a1d7e9";

static unsigned char pat(size_t i) {
    return (unsigned char)(0xA5u ^ (unsigned char)(i & 0xFFu));
}
static void fill_pattern(void *p, size_t n) {
    unsigned char *b = (unsigned char *)p;
    size_t i;
    if (!b) return;
    for (i = 0; i < n; ++i) b[i] = pat(i);
}
static int check_pattern(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    if (!b) return 0;
    for (i = 0; i < n; ++i)
        if (b[i] != pat(i)) return 0;
    return 1;
}
static int all_zero(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    if (!b) return 0;
    for (i = 0; i < n; ++i)
        if (b[i] != 0) return 0;
    return 1;
}
/* Write the sentinel across the whole block and read it back (full-write
 * evidence).  Returns 1 when a too-small block is skipped (no false claim). */
static int write_sentinel_readback(void *p, size_t cap) {
    unsigned char *b = (unsigned char *)p;
    if (!b) return 0;
    if (cap < sizeof(SENTINEL)) return 1;
    memcpy(b, SENTINEL, sizeof(SENTINEL));
    return memcmp(b, SENTINEL, sizeof(SENTINEL)) == 0;
}

/* ========================================================================= */
/* METHOD 1: malloc (valid, zero, SIZE_MAX, NULL-free, reuse)                 */
/* ========================================================================= */

/* METHOD: malloc / valid -- tracked allocation, full write, readback */
static obs_t m_alloc_1_valid(void) {
    errno = 0;
    void *p = malloc(64);
    int err = errno;
    long long ret = (p != NULL);
    int ok = write_sentinel_readback(p, 64);
    free(p);
    return obs_make(ret, err, 64, (unsigned long long)ok, 0, 0, 0);
}

/* METHOD: malloc / zero -- size 0 yields a defined 1-byte block */
static obs_t m_alloc_1_zero(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)malloc(0);
    int err = errno;
    int ok = 0;
    if (p) { *p = 0x5A; ok = (*p == 0x5A); }
    free(p);
    return obs_make(p != NULL, err, 1, (unsigned long long)ok, 0, 0, 0);
}

/* METHOD: malloc / SIZE_MAX -- refused, errno set */
static obs_t m_alloc_1_size_max(void) {
    errno = 0;
    void *p = malloc((size_t)-1);
    int err = errno;
    long long ret = (p != NULL);
    if (p) free(p);
    return obs_make(ret, err, (unsigned long long)(size_t)-1, 0, 0, 0, 0);
}

/* METHOD: malloc / NULL-free -- free(NULL) is a defined no-op */
static obs_t m_alloc_1_null_free(void) {
    errno = 0;
    free(NULL);
    return obs_make(1, errno, 0, 0, 0, 0, 0);
}

/* METHOD: malloc / reuse -- repeated alloc/write/free cycles */
static obs_t m_alloc_1_reuse(void) {
    int i, ok = 1;
    errno = 0;
    for (i = 0; i < 100; ++i) {
        unsigned char *p = (unsigned char *)malloc(32);
        if (!p) { ok = 0; break; }
        fill_pattern(p, 32);
        if (!check_pattern(p, 32)) ok = 0;
        free(p);
    }
    return obs_make(ok, errno, 100, (unsigned long long)ok, 0, 0, 0);
}

/* ========================================================================= */
/* METHOD 2: calloc (valid, zero count/size, n*size overflow, NULL-free)      */
/* ========================================================================= */

/* METHOD: calloc / valid -- zeroed, full write, readback */
static obs_t m_alloc_2_valid(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)calloc(8, 8);
    int err = errno;
    int zero = all_zero(p, 64);
    int ok = write_sentinel_readback(p, 64);
    free(p);
    return obs_make(p != NULL, err, 64, (unsigned long long)zero,
                    (unsigned long long)ok, 0, 0);
}

/* METHOD: calloc / zero count -- defined zeroed 1-byte block */
static obs_t m_alloc_2_zero_count(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)calloc(0, 8);
    int err = errno;
    int zero = all_zero(p, 1);
    free(p);
    return obs_make(p != NULL, err, 1, (unsigned long long)zero, 0, 0, 0);
}

/* METHOD: calloc / zero size -- defined zeroed 1-byte block */
static obs_t m_alloc_2_zero_size(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)calloc(8, 0);
    int err = errno;
    int zero = all_zero(p, 1);
    free(p);
    return obs_make(p != NULL, err, 1, (unsigned long long)zero, 0, 0, 0);
}

/* METHOD: calloc / n*size overflow -- refused, errno set */
static obs_t m_alloc_2_overflow(void) {
    errno = 0;
    void *p = calloc((size_t)-1, (size_t)8);
    int err = errno;
    long long ret = (p != NULL);
    if (p) free(p);
    return obs_make(ret, err, 8, 0, 0, 0, 0);
}

/* METHOD: calloc / NULL-free after a calloc block */
static obs_t m_alloc_2_null_free(void) {
    void *p = calloc(4, 4);
    free(p);
    errno = 0;
    free(NULL);
    return obs_make(1, errno, 16, 0, 0, 0, 0);
}

/* ========================================================================= */
/* METHOD 3: realloc (grow, shrink, same, NULL, SIZE_MAX, invalid)            */
/* ========================================================================= */

/* METHOD: realloc / grow -- prefix preserved, full write to new size */
static obs_t m_alloc_3_grow(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)malloc(16);
    if (!p) return obs_make(0, errno, 256, 0, 0, 0, 0);
    fill_pattern(p, 16);
    p = (unsigned char *)realloc(p, 256);
    {
        int err = errno;
        long long ret = (p != NULL);
        int ok = check_pattern(p, 16) && write_sentinel_readback(p, 256);
        free(p);
        return obs_make(ret, err, 256, (unsigned long long)ok, 0, 0, 0);
    }
}

/* METHOD: realloc / shrink -- prefix preserved */
static obs_t m_alloc_3_shrink(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)malloc(256);
    if (!p) return obs_make(0, errno, 16, 0, 0, 0, 0);
    fill_pattern(p, 256);
    p = (unsigned char *)realloc(p, 16);
    {
        int err = errno;
        long long ret = (p != NULL);
        int ok = check_pattern(p, 16);
        free(p);
        return obs_make(ret, err, 16, (unsigned long long)ok, 0, 0, 0);
    }
}

/* METHOD: realloc / same size -- contents preserved */
static obs_t m_alloc_3_same(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)malloc(64);
    if (!p) return obs_make(0, errno, 64, 0, 0, 0, 0);
    fill_pattern(p, 64);
    p = (unsigned char *)realloc(p, 64);
    {
        int err = errno;
        long long ret = (p != NULL);
        int ok = check_pattern(p, 64);
        free(p);
        return obs_make(ret, err, 64, (unsigned long long)ok, 0, 0, 0);
    }
}

/* METHOD: realloc / NULL pointer -- behaves like malloc */
static obs_t m_alloc_3_null_ptr(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)realloc(NULL, 48);
    {
        int err = errno;
        long long ret = (p != NULL);
        int ok = write_sentinel_readback(p, 48);
        free(p);
        return obs_make(ret, err, 48, (unsigned long long)ok, 0, 0, 0);
    }
}

/* METHOD: realloc / SIZE_MAX failure -- original block preserved */
static obs_t m_alloc_3_size_max_preserve(void) {
    errno = 0;
    unsigned char *p = (unsigned char *)malloc(32);
    int err;
    void *q;
    int ok;
    if (!p) return obs_make(0, errno, 32, 0, 0, 0, 0);
    fill_pattern(p, 32);
    errno = 0;
    q = realloc(p, (size_t)-1);
    err = errno;
    if (q) { free(q); ok = 0; }
    else { ok = check_pattern(p, 32); free(p); }
    return obs_make(q != NULL, err, 32, (unsigned long long)ok, 0, 0, 0);
}

/* METHOD: realloc / invalid pointer -- checked wrapper refuses (no raw UB) */
static obs_t m_alloc_3_invalid(void) {
    char stack[16];
    void *q;
    errno = 0;
    q = realloc(stack, 32);
    {
        int err = errno;
        long long ret = (q != NULL);
        if (q) free(q);
        return obs_make(ret, err, 0, 0, 0, 0, 0);
    }
}

/* ========================================================================= */
/* METHOD 4: free (valid, NULL, double-free alias, foreign stack pointer)     */
/* ========================================================================= */

/* METHOD: free / valid -- released block */
static obs_t m_alloc_4_valid(void) {
    void *p = malloc(16);
    errno = 0;
    free(p);
    return obs_make(p != NULL, errno, 16, 0, 0, 0, 0);
}

/* METHOD: free / NULL -- no-op */
static obs_t m_alloc_4_null(void) {
    errno = 0;
    free(NULL);
    return obs_make(1, errno, 0, 0, 0, 0, 0);
}

/* METHOD: free / double-free alias -- refused via CHECKED free (no raw UB) */
static obs_t m_alloc_4_double_alias(void) {
    void *p = malloc(64);
    uintptr_t addr = (uintptr_t)p;
    void *dangling;
    int rc, err;
    if (!p) return obs_make(0, errno, 0, 0, 0, 0, 0);
    free(p);
    dangling = (void *)addr;
    errno = 0;
    rc = L_checked_free(dangling);
    err = errno;
    return obs_make(rc, err, 64, 0, 0, 0, 0);
}

/* METHOD: free / foreign stack pointer -- refused via CHECKED free */
static obs_t m_alloc_4_foreign(void) {
    char stack[16];
    void *foreign = (void *)stack;
    int rc, err;
    errno = 0;
    rc = L_checked_free(foreign);
    err = errno;
    return obs_make(rc, err, 0, 0, 0, 0, 0);
}

/* ========================================================================= */
/* METHOD 5: checked layer malloc_s/calloc_s/realloc_s/free_s/free_checked_s  */
/* (stdlib/safe/safe_alloc.h -- SafeCRT-only exact spelling)                  */
/* ========================================================================= */
#ifndef PLAIN_LIBC
/* METHOD: checked_s / full SafeCRT _s lifecycle via the checked entry points */
static obs_t m_alloc_5_checked_s(void) {
    errno = 0;
    void *p = malloc_s(64);
    int e1 = errno;
    errno = 0;
    void *c = calloc_s(4, 4);
    int e2 = errno;
    errno = 0;
    void *q = realloc_s(p, 128);
    int e3 = errno;
    int ok = write_sentinel_readback(q, 128);
    void *alias = q;
    int rc, e4;
    errno = 0;
    rc = free_checked_s(alias);      /* valid free through checked API */
    e4 = errno;
    free_s(c);
    return obs_make((p != NULL) + (c != NULL) + (q != NULL) +
                        (rc == CPLUS_SAFE_OK),
                    e4, 128, (unsigned long long)ok,
                    (unsigned long long)e1,
                    (unsigned long long)(e2 * 1000 + e3), 0);
}
#endif /* !PLAIN_LIBC */

/* ========================================================================= */
/* METHOD 6: aligned_alloc_s / cplus_aligned_alloc                            */
/* ========================================================================= */

/* METHOD: aligned_alloc / valid alignment, full write to whole block */
static obs_t m_alloc_6_valid_full_write(void) {
    errno = 0;
    void *p = L_aligned_alloc(64, 256);
    int err = errno;
    long long ret = (p != NULL);
    int aligned = p && (((uintptr_t)p & (uintptr_t)63u) == 0u);
    int ok = write_sentinel_readback(p, 256);
    if (p) aligned_free(p);
    return obs_make(ret, err, 256, (unsigned long long)aligned,
                    (unsigned long long)ok, 0, 0);
}

/* METHOD: aligned_alloc / invalid alignment refused */
static obs_t m_alloc_6_bad_alignment(void) {
    errno = 0;
    void *p = L_aligned_alloc(3, 64);   /* not a power of two */
    int err = errno;
    long long ret = (p != NULL);
    if (p) aligned_free(p);
    return obs_make(ret, err, 3, 0, 0, 0, 0);
}

/* METHOD: aligned_alloc / failure on huge (valid-multiple) size */
static obs_t m_alloc_6_failure(void) {
    size_t huge = ((size_t)-1 / 64u) * 64u;  /* multiple of alignment */
    errno = 0;
    void *p = L_aligned_alloc(64, huge);
    int err = errno;
    long long ret = (p != NULL);
    if (p) aligned_free(p);
    return obs_make(ret, err, 64, 0, 0, 0, 0);
}

/* ========================================================================= */
/* METHOD 7: metadata APIs (get_meta, is_live, size, length, gen, live_*)     */
/* ========================================================================= */

/* METHOD: metadata / get_meta on live then freed block */
static obs_t m_alloc_7_get_meta(void) {
    L_meta_t m, m2;
    void *p = calloc(4, 4);
    uintptr_t addr = (uintptr_t)p;
    int rc, rc2, err, err2;
    long long live, genok;
    errno = 0;
    rc = L_get_meta(p, &m);
    err = errno;
    live = (m.is_live == 1 && m.byte_size == 16 && m.elem_size == 4 &&
            m.elem_count == 4 && m.generation > 0);
    free(p);
    errno = 0;
    rc2 = L_get_meta((const void *)addr, &m2);
    err2 = errno;
    genok = (m2.is_live == 0 && m2.byte_size == 0);
    return obs_make(rc * 10 + rc2, err, (unsigned long long)live,
                    m.byte_size, m.elem_size, m.elem_count,
                    (unsigned long long)genok * 100u + (unsigned long long)(err2 > 0));
}

/* METHOD: metadata / is_live, size, elem_size, length before and after free */
static obs_t m_alloc_7_is_live_size_length(void) {
    void *p = malloc(48);
    uintptr_t addr = (uintptr_t)p;
    int live1 = L_is_live(p);
    size_t sz = L_ptr_size(p);
    size_t es = L_ptr_elem_size(p);
    size_t cnt = L_ptr_count(p);
    free(p);
    {
        int live2 = L_is_live((const void *)addr);
        size_t sz2 = L_ptr_size((const void *)addr);
        errno = 0;
        return obs_make(live1, errno, sz, es, cnt,
                        (unsigned long long)live2, (unsigned long long)sz2);
    }
}

/* METHOD: metadata / generation changes on realloc; check_generation */
static obs_t m_alloc_7_generation(void) {
    void *p = malloc(64);
    L_meta_t m, m2;
    unsigned long long g1, g2;
    void *q;
    int rc, rc2, match_old, match_new;
    errno = 0;
    rc = L_get_meta(p, &m);
    g1 = m.generation;
    q = realloc(p, 96);
    rc2 = L_get_meta(q, &m2);
    g2 = m2.generation;
    match_old = L_check_generation(q, g1);
    match_new = L_check_generation(q, g2);
    free(q);
    return obs_make(rc + rc2, errno, (unsigned long long)(g1 > 0),
                    (unsigned long long)(g2 > g1),
                    (unsigned long long)match_old,
                    (unsigned long long)match_new, 96);
}

/* METHOD: metadata / live_count and live_bytes before/during/after */
static obs_t m_alloc_7_live_count_bytes(void) {
    size_t c0 = L_live_count(), b0 = L_live_bytes();
    void *p1 = malloc(16);
    void *p2 = calloc(2, 8);
    void *p3 = malloc(32);
    size_t c1 = L_live_count(), b1 = L_live_bytes();
    free(p1);
    free(p2);
    free(p3);
    {
        size_t c2 = L_live_count(), b2 = L_live_bytes();
        errno = 0;
        return obs_make(0, errno, (unsigned long long)(c1 - c0),
                        (unsigned long long)(b1 - b0),
                        (unsigned long long)(c2 - c0),
                        (unsigned long long)(b2 - b0), 3);
    }
}

/* ========================================================================= */
/* METHOD 8: failure injection (fail_after / fail_reset)                      */
/* ========================================================================= */

/* METHOD: fail_injection / fail_after(2) -- two successes then ENOMEM fail */
static obs_t m_alloc_8_fail_after(void) {
    void *p1, *p2, *p3;
    int e1, e2, e3, ok;
    L_fail_reset();
    L_fail_after(2);
    errno = 0;
    p1 = malloc(16); e1 = errno;
    errno = 0;
    p2 = malloc(16); e2 = errno;
    errno = 0;
    p3 = malloc(16); e3 = errno;
    L_fail_reset();
    ok = (p1 != NULL && p2 != NULL && p3 == NULL);
    free(p1);
    free(p2);
    return obs_make(ok, e3, (unsigned long long)e1, (unsigned long long)e2,
                    p1 != NULL, p2 != NULL, p3 != NULL);
}

/* METHOD: fail_injection / fail_reset -- allocations succeed again */
static obs_t m_alloc_8_fail_reset(void) {
    void *a, *b;
    int e1, e2, ok;
    L_fail_reset();
    L_fail_after(0);
    errno = 0;
    a = malloc(16); e1 = errno;
    L_fail_reset();
    errno = 0;
    b = malloc(16); e2 = errno;
    ok = (a == NULL && b != NULL);
    free(b);
    return obs_make(ok, e2, (unsigned long long)e1,
                    a != NULL, b != NULL, 0, 0);
}

/* METHOD: fail_injection / calloc honours injection */
static obs_t m_alloc_8_calloc_fail(void) {
    void *p;
    int err;
    L_fail_reset();
    L_fail_after(0);
    errno = 0;
    p = calloc(4, 4);
    err = errno;
    L_fail_reset();
    return obs_make(p == NULL, err, 0, 0, 0, 0, 0);
}

/* METHOD: fail_injection / realloc failure preserves the original block */
static obs_t m_alloc_8_realloc_preserve(void) {
    unsigned char *p;
    void *q;
    int err, ok;
    L_fail_reset();
    p = (unsigned char *)malloc(32);
    if (!p) return obs_make(0, errno, 0, 0, 0, 0, 0);
    fill_pattern(p, 32);
    L_fail_after(0);
    errno = 0;
    q = realloc(p, 64);
    err = errno;
    L_fail_reset();
    ok = (q == NULL && check_pattern(p, 32));
    free(p);
    return obs_make(q == NULL, err, (unsigned long long)ok, 0, 0, 0, 0);
}

/* ---- case table & harness ------------------------------------------------ */
typedef struct {
    const char *method;
    const char *scenario;
    obs_t (*fn)(void);
    const char *skip_reason;   /* non-NULL => intentionally not run in this layer */
} Case;

#define C(m, s, f) { m, s, f, NULL }
static const Case CASES[] = {
    C("malloc","valid",            m_alloc_1_valid),
    C("malloc","zero",             m_alloc_1_zero),
    C("malloc","SIZE_MAX",         m_alloc_1_size_max),
    C("malloc","NULL-free",        m_alloc_1_null_free),
    C("malloc","reuse",            m_alloc_1_reuse),

    C("calloc","valid",            m_alloc_2_valid),
    C("calloc","zero-count",       m_alloc_2_zero_count),
    C("calloc","zero-size",        m_alloc_2_zero_size),
    C("calloc","overflow",         m_alloc_2_overflow),
    C("calloc","NULL-free",        m_alloc_2_null_free),

    C("realloc","grow",            m_alloc_3_grow),
    C("realloc","shrink",          m_alloc_3_shrink),
    C("realloc","same-size",       m_alloc_3_same),
    C("realloc","NULL-ptr",        m_alloc_3_null_ptr),
    C("realloc","SIZE_MAX-preserve",m_alloc_3_size_max_preserve),
    C("realloc","invalid-ptr",     m_alloc_3_invalid),

    C("free","valid",              m_alloc_4_valid),
    C("free","NULL",               m_alloc_4_null),
    C("free","double-alias",       m_alloc_4_double_alias),
    C("free","foreign-stack",      m_alloc_4_foreign),

#ifndef PLAIN_LIBC
    C("checked_s","full-lifecycle",m_alloc_5_checked_s),
#else
    { "checked_s","full-lifecycle",NULL,
      "stdlib/safe/safe_alloc.h _s APIs are SafeCRT-only; equivalent cplus_* "
      "checked entry points are exercised by methods 4 and 7 in this layer" },
#endif

    C("aligned_alloc","full-write", m_alloc_6_valid_full_write),
    C("aligned_alloc","bad-align",  m_alloc_6_bad_alignment),
    C("aligned_alloc","failure",    m_alloc_6_failure),

    C("metadata","get-meta",       m_alloc_7_get_meta),
    C("metadata","is-live-size-len",m_alloc_7_is_live_size_length),
    C("metadata","generation",     m_alloc_7_generation),
    C("metadata","live-count-bytes",m_alloc_7_live_count_bytes),

    C("fail_injection","fail-after",m_alloc_8_fail_after),
    C("fail_injection","fail-reset",m_alloc_8_fail_reset),
    C("fail_injection","calloc-fail",m_alloc_8_calloc_fail),
    C("fail_injection","realloc-preserve",m_alloc_8_realloc_preserve)
};
#undef C

/* Run one scenario twice (logging OFF then ON); return 1 iff identical. */
static int run_case(const Case *c) {
    obs_t off, on;
    L_fail_reset();
    set_logging(0);
    errno = 0;
    off = c->fn();
    L_fail_reset();
    set_logging(1);
    errno = 0;
    on = c->fn();
    L_fail_reset();
    set_logging(0);
    if (obs_eq(&off, &on)) return 1;
    printf("FINDING: %s %s ret expected(off)=%lld observed(on)=%lld "
           "err expected(off)=%d observed(on)=%d "
           "a %llu/%llu b %llu/%llu c %llu/%llu d %llu/%llu e %llu/%llu\n",
           c->method, c->scenario, off.ret, on.ret, off.err, on.err,
           off.a, on.a, off.b, on.b, off.c, on.c, off.d, on.d, off.e, on.e);
    return 0;
}

/* ---- stderr capture for the sentinel-leak assertion ---------------------- */
static FILE *g_cap = NULL;
static int g_saved_fd = -1;
static int g_capturing = 0;

static void capture_begin(void) {
    g_cap = tmpfile();
    if (!g_cap) {
        printf("methods_alloc: note stderr capture unavailable (tmpfile failed); "
               "both loggers emit fixed api/msg metadata only, never caller buffers\n");
        return;
    }
    g_saved_fd = dup(2);
    if (g_saved_fd < 0 || dup2(fileno(g_cap), 2) < 0) {
        if (g_saved_fd >= 0) close(g_saved_fd);
        fclose(g_cap);
        g_cap = NULL;
        g_saved_fd = -1;
        printf("methods_alloc: note stderr capture unavailable (dup failed); "
               "both loggers emit fixed api/msg metadata only, never caller buffers\n");
        return;
    }
    g_capturing = 1;
}

/* Returns 1 if the sentinel was found in captured stderr. */
static int capture_end(void) {
    int leaked = 0, lines = 0;
    char buf[512];
    if (!g_capturing) return 0;
    fflush(stderr);
    dup2(g_saved_fd, 2);
    close(g_saved_fd);
    g_saved_fd = -1;
    rewind(g_cap);
    while (fgets(buf, (int)sizeof(buf), g_cap)) {
        if (strstr(buf, SENTINEL)) ++leaked;
        if (strstr(buf, "[CPLUS-LIBC]") || strstr(buf, "[SAFECRT]")) ++lines;
    }
    fclose(g_cap);
    g_cap = NULL;
    g_capturing = 0;
    printf("methods_alloc: captured %d logger lines with logging enabled\n", lines);
    return leaked;
}

int main(void) {
    size_t i;
    int passed = 0, total = 0, skipped = 0;
#ifndef PLAIN_LIBC
    /* Keep violation reporting from terminating the process; our invalid
     * cases are routed through checked APIs and never perform raw UB. */
    cplus_runtime_set_violation_trap(0);
#endif
    capture_begin();
    for (i = 0; i < sizeof(CASES) / sizeof(CASES[0]); ++i) {
        const Case *c = &CASES[i];
        if (!c->fn) {
            printf("methods_alloc: SKIP %s %s (%s)\n",
                   c->method, c->scenario, c->skip_reason);
            ++skipped;
            continue;
        }
        ++total;
        if (run_case(c)) ++passed;
    }
    if (capture_end()) {
        printf("FINDING: logging sentinel appeared in captured stderr\n");
        return 1;
    }
    printf("methods_alloc: %d/%d checks ok\n", passed, total);
    if (skipped)
        printf("methods_alloc: %d check(s) skipped (layer-specific APIs)\n", skipped);
    return (passed == total) ? 0 : 1;
}
