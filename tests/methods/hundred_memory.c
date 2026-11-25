/* hundred_memory.c -- 100 DISTINCT runtime scenarios for memory methods.
 *
 * Exercises BOTH safe-C layers of this repository:
 *   (1) SafeCRT layer   : stdlib/safe/safe_c.h + safe_memory.h (safeclib-backed,
 *                         cplus_*_s and cplus_*_s_auto checked APIs).
 *   (2) Plain libc layer: libc/install-include/cplus_libc_safe.h
 *                         (cplus_*_bounded APIs behind unchanged C spellings).
 *
 * The run_hundred.sh harness builds this file in a "safecrt" variant (both
 * libraries) and a "plain" variant (hardened libc only), each plain and UBSan.
 * Layer detection uses __has_include(<safe_c.h>), which is only on the
 * SafeCRT include path.  Cases that need a SafeCRT-only binding report SKIP
 * in the plain build instead of referencing an unresolved symbol.
 *
 * Output contract (one line per case, then a summary):
 *   hundred_memory: case <n> <method> <scenario> <ACCEPTED|REFUSED|SKIP>
 *   hundred_memory: <N>/100 scenarios ok
 * A mismatch between observed and expected behaviour also prints:
 *   hundred_memory: FINDING case <n> <method> <scenario>: <detail>
 *
 * Safety: no genuine UB is ever executed.  Overlapping raw memcpy is SKIPped
 * (never called); all over-capacity / NULL / SIZE_MAX calls are stopped by a
 * checked wrapper before any host operation; host-library differential calls
 * are made only on valid, non-overlapping ranges through function pointers
 * captured before the safe-layer headers redefine the standard names.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Capture the real host implementations before any safe layer remaps names. */
static void *(*const host_memcpy_fn)(void *, const void *, size_t) = memcpy;
static void *(*const host_memmove_fn)(void *, const void *, size_t) = memmove;
static void *(*const host_memset_fn)(void *, int, size_t) = memset;
static int (*const host_memcmp_fn)(const void *, const void *, size_t) = memcmp;
/* Untracked allocation: its capacity is genuinely unknown to both layers. */
static void *(*const host_malloc_fn)(size_t) = malloc;
static void (*const host_free_fn)(void *) = free;

#if __has_include(<safe_c.h>)
#define HUNDRED_SAFECRT 1
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_memory.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_error.h"
#else
#define HUNDRED_PLAIN 1
#include "cplus_libc_safe.h"
#include "libc/log/log.h"
#endif
/* cplus_memchr/memrchr/memccpy/memmem live in the hardened libc, linked by
 * both variants, and are declared by cplus_libc.h (pulled in above). */
#include "cplus_libc.h"

#ifdef HUNDRED_SAFECRT
#define H_MALLOC(n)     malloc_s(n)
#define H_FREE(p)       free_s(p)
#define H_REMAIN(p)     cplus_safe_remaining_capacity(p)
#define H_OBJ(p)        CPLUS_SAFE_OBJ_SIZE(p)
#define H_SET_STRICT(v) cplus_safe_set_unknown_capacity_mode(v)
#define H_LOG_OFF()     cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF)
#define H_LOG_ON()      cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG)
#define H_UNSAFE_SKIP(r, method, scen) \
    SKIP((r), (method), (scen), "checked _s binding is SafeCRT-only")
#else
#define H_MALLOC(n)     cplus_malloc(n)
#define H_FREE(p)       cplus_free(p)
#define H_REMAIN(p)     cplus_alloc_remaining_capacity(p)
#define H_OBJ(p)        CPLUS_LIBC_OBJ_SIZE(p)
#define H_SET_STRICT(v) cplus_libc_set_unknown_capacity_mode(v)
#define H_LOG_OFF()     cplus_libc_log_set_enabled(0)
#define H_LOG_ON()      cplus_libc_log_set_enabled(1)
#define H_UNSAFE_SKIP(r, method, scen) \
    SKIP((r), (method), (scen), "checked _s binding absent in plain layer")
#endif

#define H_UNKNOWN ((size_t)-1)

/* ------------------------------------------------------------------------- */
/* Result plumbing                                                           */

typedef struct hcase {
    const char *method;
    const char *scenario;
    const char *verdict;
    int ok;
    const char *why;
} hcase;

static void hset(hcase *r, const char *m, const char *s, const char *v,
                 int ok, const char *why) {
    r->method = m;
    r->scenario = s;
    r->verdict = v;
    r->ok = ok ? 1 : 0;
    r->why = ok ? (const char *)NULL : why;
}

#define ACCEPT(r, m, s, ok, why) hset((r), (m), (s), "ACCEPTED", (ok), (why))
#define REFUSE(r, m, s, ok, why) hset((r), (m), (s), "REFUSED", (ok), (why))
#define SKIP(r, m, s, why)       hset((r), (m), (s), "SKIP", 1, (const char *)NULL)
#define EXPECT(r, m, s, v, ok, why) hset((r), (m), (s), (v), (ok), (why))

static int g_ok;
static int g_n = 1;

static void report(const hcase *r) {
    int n = g_n++;
    printf("hundred_memory: case %d %s %s %s\n",
           n, r->method, r->scenario, r->verdict);
    if (r->ok) {
        g_ok++;
    } else {
        printf("hundred_memory: FINDING case %d %s %s: %s\n",
               n, r->method, r->scenario,
               r->why ? r->why : "unexpected result");
    }
}

/* ------------------------------------------------------------------------- */
/* Small independent helpers (byte loops; no library calls)                   */

static void hfill(unsigned char *p, size_t n, unsigned seed) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)(seed + i * 13u);
}

static int heq(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}

static int hall(const void *a, size_t n, unsigned char v) {
    const unsigned char *p = (const unsigned char *)a;
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != v) return 0;
    return 1;
}

struct hcanary {
    unsigned char pre[4];
    unsigned char body[16];
    unsigned char post[4];
};

static void canary_init(struct hcanary *c) {
    size_t i;
    for (i = 0; i < 4; ++i) { c->pre[i] = (unsigned char)(0xA0 + i); c->post[i] = (unsigned char)(0xD0 + i); }
    for (i = 0; i < 16; ++i) c->body[i] = 0x5C;
}

static int canary_ok(const struct hcanary *c) {
    size_t i;
    for (i = 0; i < 4; ++i)
        if (c->pre[i] != (unsigned char)(0xA0 + i) ||
            c->post[i] != (unsigned char)(0xD0 + i)) return 0;
    return 1;
}

static unsigned char g_global[32];

static void set_both_strict(int strict) {
    H_SET_STRICT(strict);
#ifdef HUNDRED_SAFECRT
    /* The plain _bounded functions have their own flag; both libs are linked. */
    cplus_libc_set_unknown_capacity_mode(strict);
#endif
}

/* ========================================================================= */
/* 1..12  memset (ordinary spelling, both layers)                             */

static void c01_memset_stack_exact(hcase *r) {
    unsigned char b[16], ref[16];
    int ok;
    hfill(b, 16, 1);
    host_memset_fn(ref, 0x41, 16);
    errno = 0;
    memset(b, 0x41, 16);
    ok = (errno == 0) && heq(b, ref, 16);
    ACCEPT(r, "memset", "stack exact fit", ok, "stack exact fill mismatch");
}

static void c02_memset_stack_zero(hcase *r) {
    unsigned char b[16];
    int ok;
    hfill(b, 16, 2);
    errno = 0;
    memset(b, 0x42, 0);
    ok = (errno == 0) && b[0] == (unsigned char)(2) && b[15] == (unsigned char)(2 + 15 * 13);
    ACCEPT(r, "memset", "stack zero length", ok, "zero-length memset changed data or errno");
}

static void c03_memset_global_exact(hcase *r) {
    int ok;
    hfill(g_global, sizeof g_global, 3);
    errno = 0;
    memset(g_global, 0x43, sizeof g_global);
    ok = (errno == 0) && hall(g_global, sizeof g_global, 0x43);
    ACCEPT(r, "memset", "global exact fit", ok, "global fill mismatch");
}

static void c04_memset_struct_member_canary(hcase *r) {
    struct hcanary c;
    int ok;
    canary_init(&c);
    errno = 0;
    memset(c.body, 0x7E, sizeof c.body);
    ok = (errno == 0) && hall(c.body, sizeof c.body, 0x7E) && canary_ok(&c);
    ACCEPT(r, "memset", "struct member + canary", ok, "struct member fill damaged canary");
}

static void c05_memset_heap_exact(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(48);
    int ok;
    if (!p) { ACCEPT(r, "memset", "heap exact fit", 0, "allocation failed"); return; }
    errno = 0;
    memset(p, 0x33, 48);
    ok = (errno == 0) && p[0] == 0x33 && p[24] == 0x33 && p[47] == 0x33;
    H_FREE(p);
    ACCEPT(r, "memset", "heap exact fit", ok, "heap exact fill mismatch");
}

static void c06_memset_heap_interior_exact(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(64);
    int ok;
    if (!p) { ACCEPT(r, "memset", "heap interior exact", 0, "allocation failed"); return; }
    memset(p, 0, 64);
    ok = (H_REMAIN(p + 48) == 16);
    errno = 0;
    memset(p + 48, 0x44, 16);
    ok = ok && (errno == 0) && hall(p + 48, 16, 0x44) && p[47] == 0;
    H_FREE(p);
    ACCEPT(r, "memset", "heap interior exact", ok, "interior remaining capacity not honoured");
}

static void c07_memset_heap_interior_over(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(64);
    int ok;
    if (!p) { REFUSE(r, "memset", "heap interior over capacity", 0, "allocation failed"); return; }
    memset(p, 0x11, 64);
    errno = 0;
    memset(p + 48, 0x45, 17);
    ok = (errno == ERANGE) && p[48] == 0x11 && p[63] == 0x11;
    H_FREE(p);
    REFUSE(r, "memset", "heap interior over capacity", ok, "over-capacity interior write was not refused cleanly");
}

static void c08_memset_one_past_end(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(32);
    int ok;
    if (!p) { REFUSE(r, "memset", "one-past-end", 0, "allocation failed"); return; }
    memset(p, 0x22, 32);
    errno = 0;
    memset(p + 32, 0x46, 1);
    ok = (errno == ERANGE) && p[31] == 0x22;
    H_FREE(p);
    REFUSE(r, "memset", "one-past-end", ok, "one-past-end write was not refused");
}

static void c09_memset_stack_over_cap(hcase *r) {
    unsigned char b[16];
    volatile size_t n = sizeof b + 1;
    int ok;
    hfill(b, 16, 9);
    errno = 0;
    memset(b, 0x47, n);
    ok = (errno == ERANGE) && b[0] == 9;
    REFUSE(r, "memset", "stack over capacity", ok,
           "over-capacity stack memset was not refused");
}

static void c10_memset_null_dest(hcase *r) {
    int ok;
    errno = 0;
    memset(NULL, 0x41, 4);
    ok = (errno == EINVAL);
    REFUSE(r, "memset", "NULL destination", ok, "NULL destination was not refused");
}

static void c11_memset_null_zero(hcase *r) {
    int ok;
    errno = 0;
    memset(NULL, 0x41, 0);
    ok = (errno == EINVAL);
    REFUSE(r, "memset", "NULL dest zero length", ok, "NULL/zero-length memset was not refused consistently");
}

static void c12_memset_size_max(hcase *r) {
    unsigned char b[8];
    int ok;
    hfill(b, 8, 12);
    errno = 0;
    memset(b, 0x41, SIZE_MAX);
    ok = (errno == ERANGE) && b[0] == 12;
    REFUSE(r, "memset", "SIZE_MAX length", ok, "SIZE_MAX length was not refused");
}

/* ========================================================================= */
/* 13..22  memcpy (ordinary spelling, both layers)                            */

static void c13_memcpy_stack_exact(hcase *r) {
    unsigned char s[16], d[16], ref[16];
    int ok;
    hfill(s, 16, 13);
    host_memset_fn(d, 0x9A, 16);
    host_memcpy_fn(ref, s, 16);
    errno = 0;
    memcpy(d, s, 16);
    ok = (errno == 0) && heq(d, ref, 16);
    ACCEPT(r, "memcpy", "stack exact + host diff", ok, "stack copy differs from host reference");
}

static void c14_memcpy_heap_exact(hcase *r) {
    unsigned char s[40], ref[40];
    unsigned char *d = (unsigned char *)H_MALLOC(40);
    int ok;
    if (!d) { ACCEPT(r, "memcpy", "heap exact + host diff", 0, "allocation failed"); return; }
    hfill(s, 40, 14);
    host_memcpy_fn(ref, s, 40);
    errno = 0;
    memcpy(d, s, 40);
    ok = (errno == 0) && heq(d, ref, 40);
    H_FREE(d);
    ACCEPT(r, "memcpy", "heap exact + host diff", ok, "heap copy differs from host reference");
}

static void c15_memcpy_zero_length(hcase *r) {
    unsigned char s[4], d[4];
    int ok;
    hfill(s, 4, 15);
    hfill(d, 4, 25);
    errno = 0;
    memcpy(d, s, 0);
    ok = (errno == 0) && d[0] == 25 && d[3] == (unsigned char)(25 + 3 * 13);
    ACCEPT(r, "memcpy", "zero length", ok, "zero-length copy changed destination");
}

static void c16_memcpy_interior_exact(hcase *r) {
    unsigned char *d = (unsigned char *)H_MALLOC(64);
    unsigned char s[16];
    int ok;
    if (!d) { ACCEPT(r, "memcpy", "heap interior exact", 0, "allocation failed"); return; }
    host_memset_fn(d, 0, 64);
    hfill(s, 16, 16);
    ok = (H_REMAIN(d + 48) == 16);
    errno = 0;
    memcpy(d + 48, s, 16);
    ok = ok && (errno == 0) && heq(d + 48, s, 16) && d[47] == 0;
    H_FREE(d);
    ACCEPT(r, "memcpy", "heap interior exact", ok, "interior copy failed or damaged prefix");
}

static void c17_memcpy_interior_over(hcase *r) {
    unsigned char *d = (unsigned char *)H_MALLOC(64);
    unsigned char s[20];
    int ok;
    if (!d) { REFUSE(r, "memcpy", "heap interior over capacity", 0, "allocation failed"); return; }
    host_memset_fn(d, 0x22, 64);
    hfill(s, 20, 17);
    errno = 0;
    memcpy(d + 48, s, 20);
    ok = (errno == ERANGE) && hall(d + 48, 16, 0x22);
    H_FREE(d);
    REFUSE(r, "memcpy", "heap interior over capacity", ok, "over-capacity interior copy was not refused cleanly");
}

static void c18_memcpy_one_past_end(hcase *r) {
    unsigned char *d = (unsigned char *)H_MALLOC(32);
    unsigned char s[4];
    int ok;
    if (!d) { REFUSE(r, "memcpy", "one-past-end dest", 0, "allocation failed"); return; }
    host_memset_fn(d, 0x33, 32);
    hfill(s, 4, 18);
    errno = 0;
    memcpy(d + 32, s, 4);
    ok = (errno == ERANGE) && d[31] == 0x33;
    H_FREE(d);
    REFUSE(r, "memcpy", "one-past-end dest", ok, "one-past-end copy was not refused");
}

static void c19_memcpy_unaligned_offset(hcase *r) {
    unsigned char d[64], ref[64];
    unsigned char s[7];
    int ok;
    host_memset_fn(d, 0x6A, 64);
    host_memcpy_fn(ref, d, 64);
    hfill(s, 7, 19);
    host_memcpy_fn(ref + 3, s, 7);
    errno = 0;
    memcpy(d + 3, s, 7);
    ok = (errno == 0) && heq(d, ref, 64);
    ACCEPT(r, "memcpy", "unaligned offset + host diff", ok, "unaligned copy differs from host reference");
}

static void c20_memcpy_null_dest(hcase *r) {
    unsigned char s[4];
    int ok;
    hfill(s, 4, 20);
    errno = 0;
    memcpy(NULL, s, 4);
    ok = (errno == EINVAL);
    REFUSE(r, "memcpy", "NULL destination", ok, "NULL destination was not refused");
}

static void c21_memcpy_null_src(hcase *r) {
    unsigned char d[4];
    int ok;
    host_memset_fn(d, 0x77, 4);
    errno = 0;
    memcpy(d, NULL, 4);
    ok = (errno == EINVAL) && hall(d, 4, 0x77);
    REFUSE(r, "memcpy", "NULL source", ok, "NULL source was not refused cleanly");
}

static void c22_memcpy_size_max(hcase *r) {
    unsigned char s[4], d[4];
    int ok;
    hfill(s, 4, 22);
    host_memset_fn(d, 0x88, 4);
    errno = 0;
    memcpy(d, s, SIZE_MAX);
    ok = (errno == ERANGE) && hall(d, 4, 0x88);
    REFUSE(r, "memcpy", "SIZE_MAX length", ok, "SIZE_MAX copy was not refused");
}

/* ========================================================================= */
/* 23..31  memmove (ordinary spelling) + documented memcpy-overlap SKIP       */

static void c23_memmove_nonoverlap(hcase *r) {
    unsigned char s[12], d[12], ref[12];
    int ok;
    hfill(s, 12, 23);
    host_memset_fn(d, 0x99, 12);
    host_memcpy_fn(ref, s, 12);
    errno = 0;
    memmove(d, s, 12);
    ok = (errno == 0) && heq(d, ref, 12);
    ACCEPT(r, "memmove", "non-overlap + host diff", ok, "non-overlap move differs from host");
}

static void c24_memmove_overlap_forward(hcase *r) {
    unsigned char b[32], ref[32];
    int ok;
    host_memcpy_fn(b, "0123456789abcdef", 17);
    host_memcpy_fn(ref, b, 32);
    host_memmove_fn(ref + 2, ref, 10);
    errno = 0;
    memmove(b + 2, b, 10);
    ok = (errno == 0) && heq(b, ref, 32);
    ACCEPT(r, "memmove", "overlap forward", ok, "forward overlap move differs from host");
}

static void c25_memmove_overlap_backward(hcase *r) {
    unsigned char b[32], ref[32];
    int ok;
    host_memcpy_fn(b, "0123456789abcdef", 17);
    host_memcpy_fn(ref, b, 32);
    host_memmove_fn(ref, ref + 2, 10);
    errno = 0;
    memmove(b, b + 2, 10);
    ok = (errno == 0) && heq(b, ref, 32);
    ACCEPT(r, "memmove", "overlap backward", ok, "backward overlap move differs from host");
}

static void c26_memmove_zero(hcase *r) {
    unsigned char s[4], d[4];
    int ok;
    hfill(s, 4, 26);
    hfill(d, 4, 36);
    errno = 0;
    memmove(d, s, 0);
    ok = (errno == 0) && d[0] == 36 && d[3] == (unsigned char)(36 + 3 * 13);
    ACCEPT(r, "memmove", "zero length", ok, "zero-length move changed destination");
}

static void c27_memmove_interior_over(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(64);
    int ok;
    if (!p) { REFUSE(r, "memmove", "heap interior over capacity", 0, "allocation failed"); return; }
    host_memset_fn(p, 0x44, 64);
    errno = 0;
    memmove(p + 48, p, 20);
    ok = (errno == ERANGE) && hall(p + 48, 16, 0x44);
    H_FREE(p);
    REFUSE(r, "memmove", "heap interior over capacity", ok, "over-capacity move was not refused cleanly");
}

static void c28_memmove_null_dest(hcase *r) {
    unsigned char s[4];
    int ok;
    hfill(s, 4, 28);
    errno = 0;
    memmove(NULL, s, 4);
    ok = (errno == EINVAL);
    REFUSE(r, "memmove", "NULL destination", ok, "NULL destination was not refused");
}

static void c29_memmove_null_src(hcase *r) {
    unsigned char d[4];
    int ok;
    host_memset_fn(d, 0x55, 4);
    errno = 0;
    memmove(d, NULL, 4);
    ok = (errno == EINVAL) && hall(d, 4, 0x55);
    REFUSE(r, "memmove", "NULL source", ok, "NULL source was not refused cleanly");
}

static void c30_memmove_size_max(hcase *r) {
    unsigned char s[4], d[4];
    int ok;
    hfill(s, 4, 30);
    host_memset_fn(d, 0x66, 4);
    errno = 0;
    memmove(d, s, SIZE_MAX);
    ok = (errno == ERANGE) && hall(d, 4, 0x66);
    REFUSE(r, "memmove", "SIZE_MAX length", ok, "SIZE_MAX move was not refused");
}

static void c31_memcpy_overlap_skip(hcase *r) {
    /* Genuine UB by the C standard; documented, never invoked. */
    SKIP(r, "memcpy", "overlapping ranges (UB, not invoked)", "use memmove for overlap");
}

/* ========================================================================= */
/* 32..40  memcmp (ordinary spelling, both layers)                            */

static void c32_memcmp_equal(hcase *r) {
    unsigned char a[8], b[8];
    int v, ok;
    hfill(a, 8, 32);
    host_memcpy_fn(b, a, 8);
    errno = 0;
    v = memcmp(a, b, 8);
    ok = (errno == 0) && v == 0;
    ACCEPT(r, "memcmp", "equal buffers", ok, "memcmp did not report equality");
}

static void c33_memcmp_less(hcase *r) {
    unsigned char a[4] = {1, 2, 3, 4};
    unsigned char b[4] = {1, 2, 4, 4};
    int v, ok;
    errno = 0;
    v = memcmp(a, b, 4);
    ok = (errno == 0) && v < 0;
    ACCEPT(r, "memcmp", "lexicographically less", ok, "memcmp ordering (less) wrong");
}

static void c34_memcmp_greater(hcase *r) {
    unsigned char a[4] = {9, 2, 3, 4};
    unsigned char b[4] = {1, 2, 3, 4};
    int v, ok;
    errno = 0;
    v = memcmp(a, b, 4);
    ok = (errno == 0) && v > 0;
    ACCEPT(r, "memcmp", "lexicographically greater", ok, "memcmp ordering (greater) wrong");
}

static void c35_memcmp_zero_length(hcase *r) {
    unsigned char a[2] = {0xFF, 0x01};
    unsigned char b[2] = {0x00, 0xFF};
    int v, ok;
    errno = 0;
    v = memcmp(a, b, 0);
    ok = (errno == 0) && v == 0;
    ACCEPT(r, "memcmp", "zero length", ok, "zero-length memcmp was not equal/no-op");
}

static void c36_memcmp_high_bit(hcase *r) {
    unsigned char a[3] = {0x00, 0x80, 0xFF};
    unsigned char b[3] = {0x00, 0x80, 0xFE};
    int v, host, ok;
    errno = 0;
    v = memcmp(a, b, 3);
    host = host_memcmp_fn(a, b, 3);
    ok = (errno == 0) && ((v > 0) == (host > 0)) && ((v == 0) == (host == 0));
    ACCEPT(r, "memcmp", "high-bit fill values", ok, "high-bit memcmp differs from host");
}

static void c37_memcmp_null_a(hcase *r) {
    unsigned char b[4] = {1, 2, 3, 4};
    errno = 0;
    (void)memcmp(NULL, b, 4);
    REFUSE(r, "memcmp", "NULL first buffer", (errno == EINVAL),
           "NULL first buffer was not refused");
}

static void c38_memcmp_null_b(hcase *r) {
    unsigned char a[4] = {1, 2, 3, 4};
    errno = 0;
    (void)memcmp(a, NULL, 4);
    REFUSE(r, "memcmp", "NULL second buffer", (errno == EINVAL),
           "NULL second buffer was not refused");
}

static void c39_memcmp_over_cap(hcase *r) {
    unsigned char a[4] = {1, 2, 3, 4};
    unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    errno = 0;
    (void)memcmp(a, b, 8);
    REFUSE(r, "memcmp", "stack over capacity", (errno == ERANGE),
           "over-capacity memcmp was not refused");
}

static void c40_memcmp_heap_interior(hcase *r) {
    unsigned char *a = (unsigned char *)H_MALLOC(32);
    unsigned char b[16];
    int v, ok;
    if (!a) { ACCEPT(r, "memcmp", "heap interior exact", 0, "allocation failed"); return; }
    host_memset_fn(a, 0, 32);
    hfill(b, 16, 40);
    host_memcpy_fn(a + 16, b, 16);
    ok = (H_REMAIN(a + 16) == 16);
    errno = 0;
    v = memcmp(a + 16, b, 16);
    ok = ok && (errno == 0) && v == 0;
    H_FREE(a);
    ACCEPT(r, "memcmp", "heap interior exact", ok, "interior memcmp mismatch");
}

/* ========================================================================= */
/* 41..52  memchr / memrchr / memccpy / memmem (hardened libc bindings)        */

static void c41_memchr_find(hcase *r) {
    const char *s = "abcdef";
    void *p;
    errno = 0;
    p = cplus_memchr(s, 'c', 6);
    ACCEPT(r, "memchr", "find", (errno == 0 && p == s + 2), "memchr find failed");
}

static void c42_memchr_miss(hcase *r) {
    const char *s = "abcdef";
    void *p;
    errno = 0;
    p = cplus_memchr(s, 'z', 6);
    ACCEPT(r, "memchr", "miss", (errno == 0 && p == NULL), "memchr miss should return NULL");
}

static void c43_memchr_zero_len(hcase *r) {
    const char *s = "abc";
    void *p;
    errno = 0;
    p = cplus_memchr(s, 'a', 0);
    ACCEPT(r, "memchr", "zero length", (errno == 0 && p == NULL), "memchr zero length should be NULL");
}

static void c44_memchr_high_bit(hcase *r) {
    unsigned char b[8] = {1, 2, 3, 0xFF, 5, 6, 7, 8};
    void *p;
    errno = 0;
    p = cplus_memchr(b, 0xFF, 8);
    ACCEPT(r, "memchr", "high-bit value", (errno == 0 && p == b + 3),
           "memchr did not match high-bit byte");
}

static void c45_memrchr_find_last(hcase *r) {
    const char *s = "abcabc";
    void *p;
    errno = 0;
    p = cplus_memrchr(s, 'c', 6);
    ACCEPT(r, "memrchr", "find last", (errno == 0 && p == s + 5), "memrchr last match wrong");
}

static void c46_memrchr_miss(hcase *r) {
    const char *s = "abcabc";
    void *p;
    errno = 0;
    p = cplus_memrchr(s, 'z', 6);
    ACCEPT(r, "memrchr", "miss", (errno == 0 && p == NULL), "memrchr miss should return NULL");
}

static void c47_memrchr_zero_len(hcase *r) {
    const char *s = "abc";
    void *p;
    errno = 0;
    p = cplus_memrchr(s, 'a', 0);
    ACCEPT(r, "memrchr", "zero length", (errno == 0 && p == NULL), "memrchr zero length should be NULL");
}

static void c48_memccpy_found(hcase *r) {
    const char *s = "abcXdef";
    unsigned char d[16];
    void *p;
    host_memset_fn(d, 0, 16);
    errno = 0;
    p = cplus_memccpy(d, s, 'X', 8);
    ACCEPT(r, "memccpy", "delimiter found",
           (errno == 0 && p == d + 4 && heq(d, "abcX", 4)),
           "memccpy found path wrong");
}

static void c49_memccpy_not_found(hcase *r) {
    const char *s = "abcdef";
    unsigned char d[16];
    void *p;
    host_memset_fn(d, 0, 16);
    errno = 0;
    p = cplus_memccpy(d, s, 'X', 6);
    ACCEPT(r, "memccpy", "delimiter absent",
           (errno == 0 && p == NULL && heq(d, "abcdef", 6)),
           "memccpy not-found path wrong");
}

static void c50_memccpy_zero_len(hcase *r) {
    unsigned char d[4] = {9, 9, 9, 9};
    void *p;
    errno = 0;
    p = cplus_memccpy(d, "abc", 'a', 0);
    ACCEPT(r, "memccpy", "zero length",
           (errno == 0 && p == NULL && hall(d, 4, 9)),
           "memccpy zero length changed destination");
}

static void c51_memmem_found(hcase *r) {
    const char *h = "abc---abc--";
    void *p;
    errno = 0;
    p = cplus_memmem(h, 11, "abc", 3);
    ACCEPT(r, "memmem", "needle found", (errno == 0 && p == h), "memmem first match wrong");
}

static void c52_memmem_needle_longer(hcase *r) {
    const char *h = "abc";
    void *p;
    errno = 0;
    p = cplus_memmem(h, 3, "abcdef", 6);
    ACCEPT(r, "memmem", "needle longer than haystack",
           (errno == 0 && p == NULL), "memmem should return NULL");
}

/* ========================================================================= */
/* 53..72  checked _s counterparts (SafeCRT binding; SKIP in plain layer)     */

#ifdef HUNDRED_SAFECRT
static void c53_s_memcpy_exact(hcase *r) {
    unsigned char d[16], s[8] = "abcdefg";
    errno_t e;
    host_memset_fn(d, 0x11, 16);
    e = cplus_memcpy_s(d, sizeof d, s, 7);
    ACCEPT(r, "memcpy_s", "exact fit", (e == 0 && heq(d, "abcdefg", 7)), "memcpy_s exact failed");
}
static void c54_s_memcpy_dmax_small(hcase *r) {
    unsigned char d[16], s[8] = "abcdefg";
    errno_t e;
    host_memset_fn(d, 0x5A, 16);
    e = cplus_memcpy_s(d, 3, s, 7);
    /* safeclib clears the first dmax bytes on a constraint violation. */
    REFUSE(r, "memcpy_s", "dmax too small", (e != 0 && d[3] == 0x5A && d[15] == 0x5A),
           "memcpy_s dmax-too-small not refused or canary damaged");
}
static void c55_s_memcpy_null_dest(hcase *r) {
    unsigned char s[4] = "abc";
    REFUSE(r, "memcpy_s", "NULL destination",
           (cplus_memcpy_s(NULL, 8, s, 3) != 0), "NULL destination accepted");
}
static void c56_s_memcpy_null_src(hcase *r) {
    unsigned char d[8];
    REFUSE(r, "memcpy_s", "NULL source",
           (cplus_memcpy_s(d, sizeof d, NULL, 3) != 0), "NULL source accepted");
}
static void c57_s_memcpy_sizemax(hcase *r) {
    unsigned char d[8], s[4] = "abc";
    REFUSE(r, "memcpy_s", "SIZE_MAX smax",
           (cplus_memcpy_s(d, sizeof d, s, SIZE_MAX) != 0), "SIZE_MAX smax accepted");
}
static void c58_s_memmove_overlap(hcase *r) {
    unsigned char b[16] = "0123456789", ref[16] = "0123456789";
    errno_t e;
    host_memmove_fn(ref + 2, ref, 8);
    e = cplus_memmove_s(b + 2, sizeof b - 2, b, 8);
    ACCEPT(r, "memmove_s", "overlap", (e == 0 && heq(b, ref, 16)),
           "memmove_s overlap differs from host");
}
static void c59_s_memmove_dmax_small(hcase *r) {
    unsigned char b[16] = "0123456789";
    REFUSE(r, "memmove_s", "dmax too small",
           (cplus_memmove_s(b, 3, b + 2, 5) != 0), "memmove_s dmax-too-small accepted");
}
static void c60_s_memset_exact(hcase *r) {
    unsigned char d[16], ref[16];
    errno_t e;
    host_memset_fn(d, 0x10, 16);
    host_memset_fn(ref, 0x5A, 5);
    host_memcpy_fn(ref + 5, d + 5, 11);
    e = cplus_memset_s(d, sizeof d, 0x5A, 5);
    ACCEPT(r, "memset_s", "exact fit", (e == 0 && heq(d, ref, 16)), "memset_s exact failed");
}
static void c61_s_memset_dmax_small(hcase *r) {
    unsigned char d[16];
    errno_t e;
    host_memset_fn(d, 0x77, 16);
    e = cplus_memset_s(d, 3, 0x5A, 8);
    REFUSE(r, "memset_s", "dmax too small", (e != 0), "memset_s dmax-too-small accepted");
}
static void c62_s_memcmp_equal(hcase *r) {
    int diff = 99;
    errno_t e = cplus_memcmp_s("abc", 3, "abc", 3, &diff);
    ACCEPT(r, "memcmp_s", "equal", (e == 0 && diff == 0), "memcmp_s equality wrong");
}
static void c63_s_memcmp_dmax_small(hcase *r) {
    int diff = 99;
    REFUSE(r, "memcmp_s", "dmax too small",
           (cplus_memcmp_s("abc", 2, "abc", 3, &diff) != 0), "memcmp_s dmax-too-small accepted");
}
static void c64_s_memzero_full(hcase *r) {
    unsigned char d[16];
    errno_t e;
    hfill(d, 16, 64);
    e = cplus_memzero_s(d, sizeof d);
    ACCEPT(r, "memzero_s", "full buffer", (e == 0 && hall(d, 16, 0)), "memzero_s did not zero buffer");
}
static void c65_s_memzero_null(hcase *r) {
    REFUSE(r, "memzero_s", "NULL destination",
           (cplus_memzero_s(NULL, 8) != 0), "memzero_s NULL accepted");
}
static void c66_s_memzero_zero_dmax(hcase *r) {
    unsigned char d[8];
    REFUSE(r, "memzero_s", "zero dmax",
           (cplus_memzero_s(d, 0) != 0), "memzero_s zero dmax accepted");
}
static void c67_s_memset_high_bit(hcase *r) {
    unsigned char d[8], ref[8];
    errno_t e;
    host_memset_fn(ref, 0xE3, 8);
    e = cplus_memset_s(d, sizeof d, 0xE3, 8);
    ACCEPT(r, "memset_s", "high-bit fill", (e == 0 && heq(d, ref, 8)),
           "memset_s high-bit fill differs from host");
}
static void c68_s_memcmp_ordering(hcase *r) {
    int a = 99, b = 99;
    errno_t e1 = cplus_memcmp_s("abc", 3, "abd", 3, &a);
    errno_t e2 = cplus_memcmp_s("abd", 3, "abc", 3, &b);
    ACCEPT(r, "memcmp_s", "ordering both signs",
           (e1 == 0 && e2 == 0 && a < 0 && b > 0), "memcmp_s ordering wrong");
}
static void c69_s_memcpy_one_past(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(32);
    unsigned char s[4] = "abc";
    int ok;
    if (!p) { REFUSE(r, "memcpy_s", "one-past-end dmax 0", 0, "allocation failed"); return; }
    ok = cplus_memcpy_s(p + 32, 0, s, 3) != 0;
    H_FREE(p);
    REFUSE(r, "memcpy_s", "one-past-end dmax 0", ok, "one-past-end copy accepted");
}
static void c70_s_memmove_interior_exact(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(32);
    errno_t e;
    int ok;
    if (!p) { ACCEPT(r, "memmove_s", "heap interior exact", 0, "allocation failed"); return; }
    hfill(p, 16, 70);
    host_memcpy_fn(p + 16, p, 16);
    e = cplus_memmove_s(p + 16, 16, p, 16);
    ok = (e == 0 && heq(p, p + 16, 16));
    H_FREE(p);
    ACCEPT(r, "memmove_s", "heap interior exact", ok, "memmove_s interior copy failed");
}
static void c71_s_memcpy_zero_len(hcase *r) {
    unsigned char d[8], s[4] = "abc";
    host_memset_fn(d, 0x33, 8);
    ACCEPT(r, "memcpy_s", "zero length",
           (cplus_memcpy_s(d, sizeof d, s, 0) == 0 && d[0] == 0x33),
           "memcpy_s zero length changed destination");
}
static void c72_s_memset_zero_len(hcase *r) {
    unsigned char d[8];
    host_memset_fn(d, 0x44, 8);
    ACCEPT(r, "memset_s", "zero length",
           (cplus_memset_s(d, sizeof d, 0x41, 0) == 0 && d[0] == 0x44),
           "memset_s zero length changed destination");
}
#else
static void c53_s_memcpy_exact(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s", "exact fit"); }
static void c54_s_memcpy_dmax_small(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s", "dmax too small"); }
static void c55_s_memcpy_null_dest(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s", "NULL destination"); }
static void c56_s_memcpy_null_src(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s", "NULL source"); }
static void c57_s_memcpy_sizemax(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s", "SIZE_MAX smax"); }
static void c58_s_memmove_overlap(hcase *r) { H_UNSAFE_SKIP(r, "memmove_s", "overlap"); }
static void c59_s_memmove_dmax_small(hcase *r) { H_UNSAFE_SKIP(r, "memmove_s", "dmax too small"); }
static void c60_s_memset_exact(hcase *r) { H_UNSAFE_SKIP(r, "memset_s", "exact fit"); }
static void c61_s_memset_dmax_small(hcase *r) { H_UNSAFE_SKIP(r, "memset_s", "dmax too small"); }
static void c62_s_memcmp_equal(hcase *r) { H_UNSAFE_SKIP(r, "memcmp_s", "equal"); }
static void c63_s_memcmp_dmax_small(hcase *r) { H_UNSAFE_SKIP(r, "memcmp_s", "dmax too small"); }
static void c64_s_memzero_full(hcase *r) { H_UNSAFE_SKIP(r, "memzero_s", "full buffer"); }
static void c65_s_memzero_null(hcase *r) { H_UNSAFE_SKIP(r, "memzero_s", "NULL destination"); }
static void c66_s_memzero_zero_dmax(hcase *r) { H_UNSAFE_SKIP(r, "memzero_s", "zero dmax"); }
static void c67_s_memset_high_bit(hcase *r) { H_UNSAFE_SKIP(r, "memset_s", "high-bit fill"); }
static void c68_s_memcmp_ordering(hcase *r) { H_UNSAFE_SKIP(r, "memcmp_s", "ordering both signs"); }
static void c69_s_memcpy_one_past(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s", "one-past-end dmax 0"); }
static void c70_s_memmove_interior_exact(hcase *r) { H_UNSAFE_SKIP(r, "memmove_s", "heap interior exact"); }
static void c71_s_memcpy_zero_len(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s", "zero length"); }
static void c72_s_memset_zero_len(hcase *r) { H_UNSAFE_SKIP(r, "memset_s", "zero length"); }
#endif

/* ========================================================================= */
/* 73..84  cplus_*_s_auto checked wrappers (SafeCRT only)                      */

#ifdef HUNDRED_SAFECRT
static void c73_auto_memcpy_stack_exact(hcase *r) {
    unsigned char s[8], d[8];
    void *out;
    hfill(s, 8, 73);
    host_memset_fn(d, 0x12, 8);
    errno = 0;
    out = cplus_memcpy_s_auto(d, s, 8, H_OBJ(d));
    ACCEPT(r, "memcpy_s_auto", "stack exact", (out == d && errno == 0 && heq(d, s, 8)),
           "auto memcpy stack exact failed");
}
static void c74_auto_memcpy_heap_interior(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(48);
    unsigned char s[16];
    void *out;
    int ok;
    if (!p) { ACCEPT(r, "memcpy_s_auto", "heap interior exact", 0, "allocation failed"); return; }
    host_memset_fn(p, 0, 48);
    hfill(s, 16, 74);
    ok = (H_REMAIN(p + 32) == 16);
    errno = 0;
    out = cplus_memcpy_s_auto(p + 32, s, 16, H_UNKNOWN);
    ok = ok && (out == p + 32 && errno == 0 && heq(p + 32, s, 16));
    H_FREE(p);
    ACCEPT(r, "memcpy_s_auto", "heap interior exact", ok, "auto memcpy interior failed");
}
static void c75_auto_memcpy_over(hcase *r) {
    unsigned char s[16], d[8];
    void *out;
    host_memset_fn(d, 0x6C, 8);
    hfill(s, 16, 75);
    errno = 0;
    out = cplus_memcpy_s_auto(d, s, 16, H_OBJ(d));
    REFUSE(r, "memcpy_s_auto", "over capacity",
           (out == NULL && errno == ERANGE && hall(d, 8, 0x6C)),
           "auto memcpy over-capacity not refused cleanly");
}
static void c76_auto_memmove_overlap(hcase *r) {
    unsigned char b[16] = "0123456789";
    unsigned char ref[16] = "0123456789";
    void *out;
    host_memmove_fn(ref + 2, ref, 8);
    errno = 0;
    out = cplus_memmove_s_auto(b + 2, b, 8, H_OBJ(b + 2));
    ACCEPT(r, "memmove_s_auto", "overlap",
           (out == b + 2 && errno == 0 && heq(b, ref, 16)),
           "auto memmove overlap differs from host");
}
static void c77_auto_memmove_over(hcase *r) {
    unsigned char b[16];
    void *out;
    host_memset_fn(b, 0x7B, 16);
    errno = 0;
    out = cplus_memmove_s_auto(b + 8, b, 16, H_OBJ(b + 8));
    REFUSE(r, "memmove_s_auto", "over capacity",
           (out == NULL && errno == ERANGE && hall(b, 16, 0x7B)),
           "auto memmove over-capacity not refused cleanly");
}
static void c78_auto_memset_stack_exact(hcase *r) {
    unsigned char d[10], ref[10];
    void *out;
    host_memset_fn(ref, 0xC3, 10);
    errno = 0;
    out = cplus_memset_s_auto(d, 0xC3, 10, H_OBJ(d));
    ACCEPT(r, "memset_s_auto", "stack exact",
           (out == d && errno == 0 && heq(d, ref, 10)), "auto memset stack exact failed");
}
static void c79_auto_memset_interior_exact(hcase *r) {
    unsigned char *p = (unsigned char *)H_MALLOC(64);
    void *out;
    int ok;
    if (!p) { ACCEPT(r, "memset_s_auto", "heap interior exact", 0, "allocation failed"); return; }
    host_memset_fn(p, 0, 64);
    errno = 0;
    out = cplus_memset_s_auto(p + 48, 0xD4, 16, H_UNKNOWN);
    ok = (out == p + 48 && errno == 0 && hall(p + 48, 16, 0xD4) && p[47] == 0);
    H_FREE(p);
    ACCEPT(r, "memset_s_auto", "heap interior exact", ok, "auto memset interior failed");
}
static void c80_auto_memset_over(hcase *r) {
    unsigned char d[8];
    void *out;
    host_memset_fn(d, 0x4D, 8);
    errno = 0;
    out = cplus_memset_s_auto(d, 0x41, 9, H_OBJ(d));
    REFUSE(r, "memset_s_auto", "over capacity",
           (out == NULL && errno == ERANGE && hall(d, 8, 0x4D)),
           "auto memset over-capacity not refused cleanly");
}
static void c81_auto_memcmp_equal(hcase *r) {
    unsigned char a[8], b[8];
    int v;
    hfill(a, 8, 81);
    host_memcpy_fn(b, a, 8);
    errno = 0;
    v = cplus_memcmp_s_auto(a, b, 8, H_OBJ(a), H_OBJ(b));
    ACCEPT(r, "memcmp_s_auto", "equal", (v == 0 && errno == 0), "auto memcmp equality wrong");
}
static void c82_auto_memcmp_differ(hcase *r) {
    unsigned char a[4] = {1, 2, 3, 4};
    unsigned char b[4] = {1, 2, 5, 4};
    int v;
    errno = 0;
    v = cplus_memcmp_s_auto(a, b, 4, H_OBJ(a), H_OBJ(b));
    ACCEPT(r, "memcmp_s_auto", "differ", (v < 0 && errno == 0), "auto memcmp ordering wrong");
}
static void c83_auto_memcmp_over(hcase *r) {
    unsigned char a[4] = {1, 2, 3, 4};
    unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int v;
    errno = 0;
    v = cplus_memcmp_s_auto(a, b, 8, H_OBJ(a), H_OBJ(b));
    REFUSE(r, "memcmp_s_auto", "over capacity", (v == -1 && errno == ERANGE),
           "auto memcmp over-capacity not refused");
}
static void c84_auto_memcmp_null(hcase *r) {
    unsigned char b[4] = {1, 2, 3, 4};
    int v;
    errno = 0;
    v = cplus_memcmp_s_auto(NULL, b, 4, H_UNKNOWN, H_OBJ(b));
    REFUSE(r, "memcmp_s_auto", "NULL first buffer", (v == -1 && errno == EINVAL),
           "auto memcmp NULL not refused");
}
#else
static void c73_auto_memcpy_stack_exact(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s_auto", "stack exact"); }
static void c74_auto_memcpy_heap_interior(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s_auto", "heap interior exact"); }
static void c75_auto_memcpy_over(hcase *r) { H_UNSAFE_SKIP(r, "memcpy_s_auto", "over capacity"); }
static void c76_auto_memmove_overlap(hcase *r) { H_UNSAFE_SKIP(r, "memmove_s_auto", "overlap"); }
static void c77_auto_memmove_over(hcase *r) { H_UNSAFE_SKIP(r, "memmove_s_auto", "over capacity"); }
static void c78_auto_memset_stack_exact(hcase *r) { H_UNSAFE_SKIP(r, "memset_s_auto", "stack exact"); }
static void c79_auto_memset_interior_exact(hcase *r) { H_UNSAFE_SKIP(r, "memset_s_auto", "heap interior exact"); }
static void c80_auto_memset_over(hcase *r) { H_UNSAFE_SKIP(r, "memset_s_auto", "over capacity"); }
static void c81_auto_memcmp_equal(hcase *r) { H_UNSAFE_SKIP(r, "memcmp_s_auto", "equal"); }
static void c82_auto_memcmp_differ(hcase *r) { H_UNSAFE_SKIP(r, "memcmp_s_auto", "differ"); }
static void c83_auto_memcmp_over(hcase *r) { H_UNSAFE_SKIP(r, "memcmp_s_auto", "over capacity"); }
static void c84_auto_memcmp_null(hcase *r) { H_UNSAFE_SKIP(r, "memcmp_s_auto", "NULL first buffer"); }
#endif

/* ========================================================================= */
/* 85..94  direct cplus_*_bounded calls (both layers)                          */

static void c85_bounded_memset_exact(hcase *r) {
    unsigned char d[12], ref[12];
    void *out;
    host_memset_fn(ref, 0x31, 12);
    errno = 0;
    out = cplus_memset_bounded(d, 0x31, 12, H_OBJ(d));
    ACCEPT(r, "memset_bounded", "exact fit",
           (out == d && errno == 0 && heq(d, ref, 12)), "bounded memset exact failed");
}
static void c86_bounded_memset_over(hcase *r) {
    unsigned char d[8];
    void *out;
    host_memset_fn(d, 0x32, 8);
    errno = 0;
    out = cplus_memset_bounded(d, 0x41, 9, H_OBJ(d));
    REFUSE(r, "memset_bounded", "over capacity",
           (out == NULL && errno == ERANGE && hall(d, 8, 0x32)),
           "bounded memset over-capacity not refused");
}
static void c87_bounded_memcpy_exact(hcase *r) {
    unsigned char s[10], d[10], ref[10];
    void *out;
    hfill(s, 10, 87);
    host_memcpy_fn(ref, s, 10);
    errno = 0;
    out = cplus_memcpy_bounded(d, s, 10, H_OBJ(d));
    ACCEPT(r, "memcpy_bounded", "exact fit",
           (out == d && errno == 0 && heq(d, ref, 10)), "bounded memcpy exact failed");
}
static void c88_bounded_memcpy_over(hcase *r) {
    unsigned char s[10], d[8];
    void *out;
    hfill(s, 10, 88);
    host_memset_fn(d, 0x52, 8);
    errno = 0;
    out = cplus_memcpy_bounded(d, s, 10, H_OBJ(d));
    REFUSE(r, "memcpy_bounded", "over capacity",
           (out == NULL && errno == ERANGE && hall(d, 8, 0x52)),
           "bounded memcpy over-capacity not refused");
}
static void c89_bounded_memmove_overlap(hcase *r) {
    unsigned char b[16] = "0123456789", ref[16] = "0123456789";
    void *out;
    host_memmove_fn(ref + 3, ref, 8);
    errno = 0;
    out = cplus_memmove_bounded(b + 3, b, 8, H_OBJ(b + 3));
    ACCEPT(r, "memmove_bounded", "overlap",
           (out == b + 3 && errno == 0 && heq(b, ref, 16)),
           "bounded memmove overlap differs from host");
}
static void c90_bounded_memmove_over(hcase *r) {
    unsigned char b[16];
    void *out;
    host_memset_fn(b, 0x63, 16);
    errno = 0;
    out = cplus_memmove_bounded(b + 8, b, 16, H_OBJ(b + 8));
    REFUSE(r, "memmove_bounded", "over capacity",
           (out == NULL && errno == ERANGE && hall(b, 16, 0x63)),
           "bounded memmove over-capacity not refused");
}
static void c91_bounded_memcmp_equal(hcase *r) {
    unsigned char a[8], b[8];
    int v;
    hfill(a, 8, 91);
    host_memcpy_fn(b, a, 8);
    errno = 0;
    v = cplus_memcmp_bounded(a, b, 8, H_OBJ(a), H_OBJ(b));
    ACCEPT(r, "memcmp_bounded", "equal", (v == 0 && errno == 0),
           "bounded memcmp equality wrong");
}
static void c92_bounded_memcmp_over(hcase *r) {
    unsigned char a[4] = {1, 2, 3, 4};
    unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    errno = 0;
    (void)cplus_memcmp_bounded(a, b, 8, H_OBJ(a), H_OBJ(b));
    REFUSE(r, "memcmp_bounded", "over capacity", (errno == ERANGE),
           "bounded memcmp over-capacity not refused");
}
static void c93_bounded_unknown_default(hcase *r) {
    volatile size_t sz = 16; /* opaque size keeps __builtin_object_size unknown */
    unsigned char *p = (unsigned char *)host_malloc_fn(sz);
    void *out;
    if (!p) { ACCEPT(r, "memset_bounded", "unknown capacity default", 0, "host allocation failed"); return; }
    set_both_strict(0);
    errno = 0;
    out = cplus_memset_bounded(p, 0x5E, 8, H_OBJ(p));
    ACCEPT(r, "memset_bounded", "unknown capacity default",
           (out == p && errno == 0 && hall(p, 8, 0x5E)),
           "unknown-capacity default mode did not proceed");
    host_free_fn(p);
}
static void c94_bounded_unknown_strict(hcase *r) {
    volatile size_t sz = 16; /* opaque size keeps __builtin_object_size unknown */
    unsigned char *p = (unsigned char *)host_malloc_fn(sz);
    void *out;
    if (!p) { REFUSE(r, "memset_bounded", "unknown capacity strict", 0, "host allocation failed"); return; }
    hfill(p, 16, 94);
    set_both_strict(1);
    errno = 0;
    out = cplus_memset_bounded(p, 0x5F, 8, H_OBJ(p));
    set_both_strict(0);
    REFUSE(r, "memset_bounded", "unknown capacity strict",
           (out == NULL && errno == EINVAL && p[0] == 94),
           "unknown-capacity strict mode did not refuse");
    host_free_fn(p);
}

/* ========================================================================= */
/* 95..100  logging parity, canary integrity, final differential coverage     */

static void c95_log_parity_memset(hcase *r) {
    unsigned char a[8], b[8];
    int eoff, eon;
    H_LOG_OFF();
    errno = 0;
    memset(a, 0x71, 8);
    eoff = errno;
    H_LOG_ON();
    errno = 0;
    memset(b, 0x71, 8);
    eon = errno;
    H_LOG_OFF();
    ACCEPT(r, "memset", "logging OFF/ON parity",
           (eoff == 0 && eon == 0 && heq(a, b, 8) && hall(a, 8, 0x71)),
           "memset result changed when logging was enabled");
}
static void c96_log_parity_memcpy(hcase *r) {
    unsigned char s[8], a[8], b[8];
    int eoff, eon;
    hfill(s, 8, 96);
    H_LOG_OFF();
    errno = 0;
    memcpy(a, s, 8);
    eoff = errno;
    H_LOG_ON();
    errno = 0;
    memcpy(b, s, 8);
    eon = errno;
    H_LOG_OFF();
    ACCEPT(r, "memcpy", "logging OFF/ON parity",
           (eoff == 0 && eon == 0 && heq(a, b, 8) && heq(a, s, 8)),
           "memcpy result changed when logging was enabled");
}
static void c97_canary_refused_memcpy(hcase *r) {
    struct hcanary c;
    unsigned char s[32];
    canary_init(&c);
    hfill(s, 32, 97);
    errno = 0;
    memcpy(c.body, s, sizeof s);
    REFUSE(r, "memcpy", "refused copy keeps canary",
           (errno == ERANGE && canary_ok(&c) && hall(c.body, 16, 0x5C)),
           "refused copy damaged canary or body");
}
static void c98_differential_memmove_overlap(hcase *r) {
    unsigned char b[24], ref[24];
    int i;
    for (i = 0; i < 24; ++i) b[i] = ref[i] = (unsigned char)('a' + i);
    host_memmove_fn(ref + 5, ref + 1, 12);
    errno = 0;
    memmove(b + 5, b + 1, 12);
    ACCEPT(r, "memmove", "overlap full differential",
           (errno == 0 && heq(b, ref, 24)),
           "overlapping memmove differs from host byte-for-byte");
}
static void c99_differential_memcmp_highbit(hcase *r) {
    unsigned char a[8] = {0, 1, 2, 3, 4, 5, 0x80, 0xFF};
    unsigned char b[8] = {0, 1, 2, 3, 4, 5, 0x80, 0x7F};
    int v, host;
    errno = 0;
    v = memcmp(a, b, 8);
    host = host_memcmp_fn(a, b, 8);
    ACCEPT(r, "memcmp", "high-bit differential",
           (errno == 0 && ((v > 0) == (host > 0)) && ((v < 0) == (host < 0))),
           "high-bit memcmp sign differs from host");
}
static void c100_canary_global_refused(hcase *r) {
    unsigned char guard[40];
    int ok;
    host_memset_fn(guard, 0xE7, 40);
    hfill(g_global, sizeof g_global, 100);
    errno = 0;
    memset(g_global, 0x41, SIZE_MAX);
    ok = (errno == ERANGE) && hall(guard, 40, 0xE7) &&
         g_global[0] == 100 && g_global[31] == (unsigned char)(100 + 31 * 13);
    REFUSE(r, "memset", "global SIZE_MAX keeps canary",
           ok, "refused global memset damaged neighbouring canary");
}

/* ------------------------------------------------------------------------- */

int main(void) {
#ifdef HUNDRED_SAFECRT
    /* Let refusals return through errno instead of terminating the process. */
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
#endif
    set_both_strict(0);
    H_LOG_OFF();
    setvbuf(stdout, NULL, _IONBF, 0);

#define RUN(fn) do { hcase r = {0, 0, 0, 0, 0}; (fn)(&r); report(&r); } while (0)

    RUN(c01_memset_stack_exact);
    RUN(c02_memset_stack_zero);
    RUN(c03_memset_global_exact);
    RUN(c04_memset_struct_member_canary);
    RUN(c05_memset_heap_exact);
    RUN(c06_memset_heap_interior_exact);
    RUN(c07_memset_heap_interior_over);
    RUN(c08_memset_one_past_end);
    RUN(c09_memset_stack_over_cap);
    RUN(c10_memset_null_dest);
    RUN(c11_memset_null_zero);
    RUN(c12_memset_size_max);

    RUN(c13_memcpy_stack_exact);
    RUN(c14_memcpy_heap_exact);
    RUN(c15_memcpy_zero_length);
    RUN(c16_memcpy_interior_exact);
    RUN(c17_memcpy_interior_over);
    RUN(c18_memcpy_one_past_end);
    RUN(c19_memcpy_unaligned_offset);
    RUN(c20_memcpy_null_dest);
    RUN(c21_memcpy_null_src);
    RUN(c22_memcpy_size_max);

    RUN(c23_memmove_nonoverlap);
    RUN(c24_memmove_overlap_forward);
    RUN(c25_memmove_overlap_backward);
    RUN(c26_memmove_zero);
    RUN(c27_memmove_interior_over);
    RUN(c28_memmove_null_dest);
    RUN(c29_memmove_null_src);
    RUN(c30_memmove_size_max);
    RUN(c31_memcpy_overlap_skip);

    RUN(c32_memcmp_equal);
    RUN(c33_memcmp_less);
    RUN(c34_memcmp_greater);
    RUN(c35_memcmp_zero_length);
    RUN(c36_memcmp_high_bit);
    RUN(c37_memcmp_null_a);
    RUN(c38_memcmp_null_b);
    RUN(c39_memcmp_over_cap);
    RUN(c40_memcmp_heap_interior);

    RUN(c41_memchr_find);
    RUN(c42_memchr_miss);
    RUN(c43_memchr_zero_len);
    RUN(c44_memchr_high_bit);
    RUN(c45_memrchr_find_last);
    RUN(c46_memrchr_miss);
    RUN(c47_memrchr_zero_len);
    RUN(c48_memccpy_found);
    RUN(c49_memccpy_not_found);
    RUN(c50_memccpy_zero_len);
    RUN(c51_memmem_found);
    RUN(c52_memmem_needle_longer);

    RUN(c53_s_memcpy_exact);
    RUN(c54_s_memcpy_dmax_small);
    RUN(c55_s_memcpy_null_dest);
    RUN(c56_s_memcpy_null_src);
    RUN(c57_s_memcpy_sizemax);
    RUN(c58_s_memmove_overlap);
    RUN(c59_s_memmove_dmax_small);
    RUN(c60_s_memset_exact);
    RUN(c61_s_memset_dmax_small);
    RUN(c62_s_memcmp_equal);
    RUN(c63_s_memcmp_dmax_small);
    RUN(c64_s_memzero_full);
    RUN(c65_s_memzero_null);
    RUN(c66_s_memzero_zero_dmax);
    RUN(c67_s_memset_high_bit);
    RUN(c68_s_memcmp_ordering);
    RUN(c69_s_memcpy_one_past);
    RUN(c70_s_memmove_interior_exact);
    RUN(c71_s_memcpy_zero_len);
    RUN(c72_s_memset_zero_len);

    RUN(c73_auto_memcpy_stack_exact);
    RUN(c74_auto_memcpy_heap_interior);
    RUN(c75_auto_memcpy_over);
    RUN(c76_auto_memmove_overlap);
    RUN(c77_auto_memmove_over);
    RUN(c78_auto_memset_stack_exact);
    RUN(c79_auto_memset_interior_exact);
    RUN(c80_auto_memset_over);
    RUN(c81_auto_memcmp_equal);
    RUN(c82_auto_memcmp_differ);
    RUN(c83_auto_memcmp_over);
    RUN(c84_auto_memcmp_null);

    RUN(c85_bounded_memset_exact);
    RUN(c86_bounded_memset_over);
    RUN(c87_bounded_memcpy_exact);
    RUN(c88_bounded_memcpy_over);
    RUN(c89_bounded_memmove_overlap);
    RUN(c90_bounded_memmove_over);
    RUN(c91_bounded_memcmp_equal);
    RUN(c92_bounded_memcmp_over);
    RUN(c93_bounded_unknown_default);
    RUN(c94_bounded_unknown_strict);

    RUN(c95_log_parity_memset);
    RUN(c96_log_parity_memcpy);
    RUN(c97_canary_refused_memcpy);
    RUN(c98_differential_memmove_overlap);
    RUN(c99_differential_memcmp_highbit);
    RUN(c100_canary_global_refused);

#undef RUN
    printf("hundred_memory: %d/100 scenarios ok\n", g_ok);
    return 0;
}
