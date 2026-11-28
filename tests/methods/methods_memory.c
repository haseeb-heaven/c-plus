/* Per-method unit + logging-parity tests for every memory operation.
 *
 * Default build: C+ SafeCRT ordinary-C layer (stdlib/safe/safe_c.h) plus the
 * checked (_s) and auto-bounds (_s_auto) APIs.  -DPLAIN_LIBC selects the
 * standalone hardened-libc layer (cplus_libc_safe.h); SafeCRT-only entry
 * points are reported as SKIP there.
 *
 * Every method runs twice, with logging OFF and ON.  The two runs must agree
 * on every encoded return value, errno and canary verdict; logging must never
 * change semantics.  Destinations are wrapped in pre/body/post canaries so a
 * refused call that overflows (or a buggy Sized API that zeroes the wrong
 * range) is detected.  No scenario performs genuine undefined behaviour:
 * oversized requests always target a canary body that is larger than the
 * declared capacity.
 *
 * Standalone C99: main returns 0 and prints
 *   methods_memory: <passed>/<total> checks ok
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_log.h"
#endif
#include "log/log.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static void skip_raw_unsupported(const char *name, const char *reason);

/* ------------------------------------------------------------------ */
/* Guarded destinations and deterministic source patterns             */
/* ------------------------------------------------------------------ */
#define CAN     0xC7u
#define PRE_N   8u
#define POST_N  8u
#define BODY_N  80u   /* physical body: larger than the declared capacity */
#define CAP_N   64u   /* declared destination capacity used by checked APIs */

typedef struct {
    unsigned char pre[PRE_N];
    unsigned char body[BODY_N];
    unsigned char post[POST_N];
} Guarded;

static unsigned char pat_at(size_t i) { return (unsigned char)((i * 7u + 3u) & 0xffu); }

static void pattern_init(unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = pat_at(i);
}
static int pattern_ok(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) if (p[i] != pat_at(i)) return 0;
    return 1;
}
static int fill_ok(const unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i) if (p[i] != v) return 0;
    return 1;
}
static void guarded_init(Guarded *g) {
    cplus_memset(g->pre,  CAN, sizeof g->pre);
    cplus_memset(g->body, CAN, sizeof g->body);
    cplus_memset(g->post, CAN, sizeof g->post);
}
/* Bytes [0,allowed) at the start of body may legitimately have been modified
 * (a successful write, or the Annex-K "zero dest on violation" behaviour).
 * Everything else must still be the canary. */
static int guarded_ok(const Guarded *g, size_t allowed) {
    size_t i;
    if (allowed > BODY_N) allowed = BODY_N;
    for (i = 0; i < PRE_N; ++i)  if (g->pre[i]  != CAN) return 0;
    for (i = allowed; i < BODY_N; ++i) if (g->body[i] != CAN) return 0;
    for (i = 0; i < POST_N; ++i) if (g->post[i] != CAN) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Result accumulator.  retmix/errmix fold every sub-scenario so the   */
/* OFF/ON comparison covers all of them, not just one representative.  */
/* ------------------------------------------------------------------ */
typedef struct {
    unsigned long retmix;
    unsigned int  errmix;
    int  can_ok;
    int  sem_ok;
    const char *where;
} Res;
#define RES_INIT { 0UL, 0u, 1, 1, NULL }

static void note(Res *r, long ret, int err, int can_ok, int sem_ok, const char *where) {
    r->retmix = r->retmix * 131UL + (unsigned long)ret;
    r->errmix = r->errmix * 31u  + (unsigned int)err;
    if (!can_ok || !sem_ok) {
        if (!r->where) r->where = where;
        if (!can_ok) r->can_ok = 0;
        if (!sem_ok) r->sem_ok = 0;
    }
}
#define REC(w_, ret_, err_, can_, sem_) \
    note((r), (long)(ret_), (int)(err_), (can_), (sem_), (w_))

/* ------------------------------------------------------------------ */
/* Logging control: toggle BOTH the SafeCRT logger and the libc logger */
/* ------------------------------------------------------------------ */
static void set_logging(int on) {
    cplus_libc_log_set_enabled(on);
#ifndef PLAIN_LIBC
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
#endif
}

/* ------------------------------------------------------------------ */
/* Uniform operation adapters (encoded return: 1 ok, 0 refused)         */
/* ------------------------------------------------------------------ */
typedef long (*CopyOp)(void *dst, size_t cap, const void *src, size_t n, int *perr);
typedef long (*SetOp) (void *dst, size_t cap, int value, size_t n, int *perr);
typedef long (*CmpOp) (const void *a, size_t capa, const void *b, size_t capb,
                       size_t n, int *perr, long *diff);

static long cp_raw(void *dst, size_t cap, const void *src, size_t n, int *perr) {
    void *p; (void)cap;
    errno = 0; p = cplus_memcpy(dst, src, n); *perr = errno;
    return (p != NULL && p == dst) ? 1L : 0L;
}
static long mv_raw(void *dst, size_t cap, const void *src, size_t n, int *perr) {
    void *p; (void)cap;
    errno = 0; p = cplus_memmove(dst, src, n); *perr = errno;
    return (p != NULL && p == dst) ? 1L : 0L;
}
static long st_raw(void *dst, size_t cap, int value, size_t n, int *perr) {
    void *p; (void)cap;
    errno = 0; p = cplus_memset(dst, value, n); *perr = errno;
    return (p != NULL && p == dst) ? 1L : 0L;
}
static long cm_raw(const void *a, size_t capa, const void *b, size_t capb,
                   size_t n, int *perr, long *diff) {
    int r; (void)capa; (void)capb;
    errno = 0; r = cplus_memcmp(a, b, n); *perr = errno; *diff = r;
    return 1L; /* raw memcmp has no constraint-violation return code */
}
#ifndef PLAIN_LIBC
static long cp_s(void *dst, size_t cap, const void *src, size_t n, int *perr) {
    errno_t e;
    errno = 0; e = cplus_memcpy_s(dst, cap, src, n); *perr = errno;
    return e == 0 ? 1L : 0L;
}
static long cp_auto(void *dst, size_t cap, const void *src, size_t n, int *perr) {
    void *p;
    errno = 0; p = cplus_memcpy_s_auto(dst, src, n, cap); *perr = errno;
    return (p != NULL && p == dst) ? 1L : 0L;
}
static long mv_s(void *dst, size_t cap, const void *src, size_t n, int *perr) {
    errno_t e;
    errno = 0; e = cplus_memmove_s(dst, cap, src, n); *perr = errno;
    return e == 0 ? 1L : 0L;
}
static long mv_auto(void *dst, size_t cap, const void *src, size_t n, int *perr) {
    void *p;
    errno = 0; p = cplus_memmove_s_auto(dst, src, n, cap); *perr = errno;
    return (p != NULL && p == dst) ? 1L : 0L;
}
static long st_s(void *dst, size_t cap, int value, size_t n, int *perr) {
    errno_t e;
    errno = 0; e = cplus_memset_s(dst, cap, value, n); *perr = errno;
    return e == 0 ? 1L : 0L;
}
static long st_auto(void *dst, size_t cap, int value, size_t n, int *perr) {
    void *p;
    errno = 0; p = cplus_memset_s_auto(dst, value, n, cap); *perr = errno;
    return (p != NULL && p == dst) ? 1L : 0L;
}
static long cm_s(const void *a, size_t capa, const void *b, size_t capb,
                 size_t n, int *perr, long *diff) {
    int d = 77, r; (void)capb;
    errno = 0; r = cplus_memcmp_s(a, capa, b, n, &d); *perr = errno; *diff = d;
    return r == 0 ? 1L : 0L;
}
static long cm_auto(const void *a, size_t capa, const void *b, size_t capb,
                    size_t n, int *perr, long *diff) {
    int r;
    errno = 0; r = cplus_memcmp_s_auto(a, b, n, capa, capb); *perr = errno; *diff = r;
    return 1L;
}
#endif /* !PLAIN_LIBC */

/* ------------------------------------------------------------------ */
/* Reusable scenario bodies for copy / set families                    */
/* ------------------------------------------------------------------ */
static void t_copy_dests(Res *r, CopyOp op, const char *tag) {
    unsigned char src[BODY_N]; (void)tag; pattern_init(src, sizeof src);
    { Guarded g; int e = 0; long v; guarded_init(&g);
      v = op(g.body, CAP_N, src, CAP_N, &e);
      REC("stack-normal", v, e, guarded_ok(&g, CAP_N), v == 1 && pattern_ok(g.body, CAP_N)); }
    { Guarded *g = (Guarded *)malloc(sizeof *g); int e = 0; long v;
      if (!g) { REC("heap-alloc", 0, 0, 0, 0); }
      else { guarded_init(g); v = op(g->body, CAP_N, src, CAP_N, &e);
        REC("heap-normal", v, e, guarded_ok(g, CAP_N), v == 1 && pattern_ok(g->body, CAP_N));
        free(g); } }
    { struct { unsigned char a[4]; Guarded g; unsigned char b[4]; } s; int e = 0; long v;
      guarded_init(&s.g); v = op(s.g.body, CAP_N, src, CAP_N, &e);
      REC("struct-normal", v, e, guarded_ok(&s.g, CAP_N), v == 1 && pattern_ok(s.g.body, CAP_N)); }
}
static void t_copy_zero(Res *r, CopyOp op, const char *tag, int expect_ok) {
    Guarded g; unsigned char src[8]; int e = 0; long v;
    pattern_init(src, sizeof src); guarded_init(&g);
    v = op(g.body, CAP_N, src, 0, &e);
    REC(tag, v, e, guarded_ok(&g, 0), v == (expect_ok ? 1L : 0L));
}
static void t_copy_over(Res *r, CopyOp op, const char *tag, int expect_ok) {
    Guarded g; unsigned char src[BODY_N]; int e = 0; long v; size_t allowed;
    pattern_init(src, sizeof src); guarded_init(&g);
    v = op(g.body, CAP_N, src, BODY_N, &e);
    allowed = expect_ok ? BODY_N : CAP_N;
    REC(tag, v, e, guarded_ok(&g, allowed),
        expect_ok ? (v == 1 && pattern_ok(g.body, BODY_N))
                  : (v == 0 && guarded_ok(&g, CAP_N)));
}
static void t_copy_onepast(Res *r, CopyOp op, const char *tag, int expect_ok) {
    Guarded g; unsigned char src[8]; int e = 0; long v; size_t allowed;
    pattern_init(src, sizeof src); guarded_init(&g);
    v = op(g.body + CAP_N, 0, src, 1, &e);
    allowed = expect_ok ? (CAP_N + 1u) : 0u;
    REC(tag, v, e, guarded_ok(&g, allowed),
        expect_ok ? (v == 1 && g.body[CAP_N] == src[0]) : (v == 0 && guarded_ok(&g, 0)));
}
static void t_copy_nulldest(Res *r, CopyOp op, const char *tag) {
    unsigned char src[8]; int e = 0; long v;
    pattern_init(src, sizeof src);
    v = op(NULL, CAP_N, src, 4, &e);
    REC(tag, v, e, 1, v == 0);
}
static void t_copy_nullsrc(Res *r, CopyOp op, const char *tag, int raw_style) {
    Guarded g; int e = 0; long v;
    guarded_init(&g);
    v = op(g.body, CAP_N, NULL, 4, &e);
    REC(tag, v, e, guarded_ok(&g, CAP_N),
        raw_style ? (v == 1 && e != 0) : (v == 0));
}
#ifndef PLAIN_LIBC
static void t_copy_sizemax(Res *r, CopyOp op, const char *tag) {
    Guarded g; unsigned char src[8]; int e = 0; long v;
    pattern_init(src, sizeof src); guarded_init(&g);
    v = op(g.body, CAP_N, src, (size_t)-1, &e);
    REC(tag, v, e, guarded_ok(&g, CAP_N), v == 0);
}
#endif /* !PLAIN_LIBC */
/* Overlap through a checked API: refusal must not touch canary region. */
static void t_copy_overlap(Res *r, CopyOp op, const char *tag, int expect_ok) {
    Guarded g; int e = 0; long v; size_t i;
    guarded_init(&g); pattern_init(g.body, sizeof g.body);
    /* source overlaps destination: body[0..32) <- body[8..40) */
    v = op(g.body, CAP_N, g.body + 8, 32, &e);
    if (expect_ok) {
        int good = v == 1;
        for (i = 0; i < 32; ++i) if (g.body[i] != pat_at(i + 8)) good = 0;
        for (i = 32; i < BODY_N; ++i) if (g.body[i] != pat_at(i)) good = 0;
        for (i = 0; i < PRE_N; ++i) if (g.pre[i] != CAN) good = 0;
        for (i = 0; i < POST_N; ++i) if (g.post[i] != CAN) good = 0;
        REC(tag, v, e, 1, good);
    } else {
        int good = v == 0;
        for (i = CAP_N; i < BODY_N; ++i) if (g.body[i] != pat_at(i)) good = 0;
        for (i = 0; i < PRE_N; ++i) if (g.pre[i] != CAN) good = 0;
        for (i = 0; i < POST_N; ++i) if (g.post[i] != CAN) good = 0;
        REC(tag, v, e, 1, good);
    }
}

static void t_set_dests(Res *r, SetOp op, const char *tag) {
    (void)tag;
    { Guarded g; int e = 0; long v; guarded_init(&g);
      v = op(g.body, CAP_N, 0x5A, CAP_N, &e);
      REC("stack-normal", v, e, guarded_ok(&g, CAP_N), v == 1 && fill_ok(g.body, CAP_N, 0x5A)); }
    { Guarded *g = (Guarded *)malloc(sizeof *g); int e = 0; long v;
      if (!g) { REC("heap-alloc", 0, 0, 0, 0); }
      else { guarded_init(g); v = op(g->body, CAP_N, 0x5A, CAP_N, &e);
        REC("heap-normal", v, e, guarded_ok(g, CAP_N), v == 1 && fill_ok(g->body, CAP_N, 0x5A));
        free(g); } }
    { struct { unsigned char a[4]; Guarded g; unsigned char b[4]; } s; int e = 0; long v;
      guarded_init(&s.g); v = op(s.g.body, CAP_N, 0x5A, CAP_N, &e);
      REC("struct-normal", v, e, guarded_ok(&s.g, CAP_N), v == 1 && fill_ok(s.g.body, CAP_N, 0x5A)); }
}
static void t_set_zero(Res *r, SetOp op, const char *tag, int expect_ok) {
    Guarded g; int e = 0; long v;
    guarded_init(&g);
    v = op(g.body, CAP_N, 0x5A, 0, &e);
    REC(tag, v, e, guarded_ok(&g, 0), v == (expect_ok ? 1L : 0L));
}
static void t_set_over(Res *r, SetOp op, const char *tag, int expect_ok) {
    Guarded g; int e = 0; long v; size_t allowed;
    guarded_init(&g);
    v = op(g.body, CAP_N, 0x5A, BODY_N, &e);
    allowed = expect_ok ? BODY_N : CAP_N;
    REC(tag, v, e, guarded_ok(&g, allowed),
        expect_ok ? (v == 1 && fill_ok(g.body, BODY_N, 0x5A))
                  : (v == 0 && guarded_ok(&g, CAP_N)));
}
static void t_set_onepast(Res *r, SetOp op, const char *tag, int expect_ok) {
    Guarded g; int e = 0; long v; size_t allowed;
    guarded_init(&g);
    v = op(g.body + CAP_N, 0, 0x5A, 1, &e);
    allowed = expect_ok ? (CAP_N + 1u) : 0u;
    REC(tag, v, e, guarded_ok(&g, allowed),
        expect_ok ? (v == 1 && g.body[CAP_N] == 0x5A) : (v == 0 && guarded_ok(&g, 0)));
}
static void t_set_nulldest(Res *r, SetOp op, const char *tag) {
    int e = 0; long v;
    v = op(NULL, CAP_N, 0x5A, 4, &e);
    REC(tag, v, e, 1, v == 0);
}
#ifndef PLAIN_LIBC
static void t_set_sizemax(Res *r, SetOp op, const char *tag) {
    Guarded g; int e = 0; long v;
    guarded_init(&g);
    v = op(g.body, CAP_N, 0x5A, (size_t)-1, &e);
    REC(tag, v, e, guarded_ok(&g, CAP_N), v == 0);
}
#endif /* !PLAIN_LIBC */

/* ------------------------------------------------------------------ */
/* Reusable scenario body for compare family                           */
/* ------------------------------------------------------------------ */
static void t_cmp_cases(Res *r, CmpOp op, const char *tag, int zero_refused) {
    unsigned char a[BODY_N], b[BODY_N];
    long d = 123; int e = 0, ok; (void)tag;

    pattern_init(a, sizeof a); pattern_init(b, sizeof b);
    ok = op(a, CAP_N, b, CAP_N, CAP_N, &e, &d);
    REC("equal", ok, e, 1, ok == 1 && d == 0);

    pattern_init(a, sizeof a); pattern_init(b, sizeof b); b[CAP_N - 1]++;
    ok = op(a, CAP_N, b, CAP_N, CAP_N, &e, &d);
    REC("less", ok, e, 1, ok == 1 && d < 0);

    pattern_init(a, sizeof a); pattern_init(b, sizeof b); a[CAP_N - 1]++;
    ok = op(a, CAP_N, b, CAP_N, CAP_N, &e, &d);
    REC("greater", ok, e, 1, ok == 1 && d > 0);

    pattern_init(a, sizeof a); pattern_init(b, sizeof b);
    ok = op(a, CAP_N, b, CAP_N, 0, &e, &d);
    REC("zero-len", ok, e, 1, zero_refused ? (ok == 0) : (ok == 1 && d == 0));

    /* NULL operands must be refused without a crash; canaries n/a. */
    d = 123; ok = op(NULL, CAP_N, b, CAP_N, 4, &e, &d);
    REC("null-a", ok, e, 1, d == -1 || d == 123 || d == 1);
    d = 123; ok = op(a, CAP_N, NULL, CAP_N, 4, &e, &d);
    REC("null-b", ok, e, 1, d == -1 || d == 123 || d == 1);
}

/* ================================================================== */
/* METHODS                                                             */
/* ================================================================== */

/* METHOD: cplus_memcpy (ordinary-C layer, raw C spelling) */
static void m_mem_01_memcpy(Res *r) {
    t_copy_dests(r, cp_raw, "memcpy");
    t_copy_zero(r, cp_raw, "memcpy/zero", 1);
    /* raw: n == capacity succeeds without exceeding the canary body */
    { Guarded g; unsigned char src[BODY_N]; int e = 0; long v;
      pattern_init(src, sizeof src); guarded_init(&g);
      v = cp_raw(g.body, CAP_N, src, CAP_N, &e);
      REC("memcpy/exact", v, e, guarded_ok(&g, CAP_N), v == 1 && pattern_ok(g.body, CAP_N)); }
    t_copy_onepast(r, cp_raw, "memcpy/one-past", 1);
    t_copy_over(r, cp_raw, "memcpy/over-capacity", 1);
    t_copy_nulldest(r, cp_raw, "memcpy/null-dest");
    t_copy_nullsrc(r, cp_raw, "memcpy/null-src", 1);
    skip_raw_unsupported("memcpy/SIZE_MAX", "raw copy would loop; checked APIs cover this");
    skip_raw_unsupported("memcpy/overlap", "raw overlap is UB; use memmove/checked API");
}

#ifndef PLAIN_LIBC
/* METHOD: cplus_memcpy_s (checked SafeCRT API) */
static void m_mem_02_memcpy_s(Res *r) {
    t_copy_dests(r, cp_s, "memcpy_s");
    t_copy_zero(r, cp_s, "memcpy_s/zero", 1); /* C11: slen == 0 succeeds */
    { Guarded g; unsigned char src[BODY_N]; int e = 0; long v;
      pattern_init(src, sizeof src); guarded_init(&g);
      v = cp_s(g.body, CAP_N, src, CAP_N, &e);
      REC("memcpy_s/exact", v, e, guarded_ok(&g, CAP_N), v == 1 && pattern_ok(g.body, CAP_N)); }
    t_copy_onepast(r, cp_s, "memcpy_s/one-past", 0);
    t_copy_over(r, cp_s, "memcpy_s/over-capacity", 0);
    t_copy_nulldest(r, cp_s, "memcpy_s/null-dest");
    t_copy_nullsrc(r, cp_s, "memcpy_s/null-src", 0);
    t_copy_sizemax(r, cp_s, "memcpy_s/SIZE_MAX");
    t_copy_overlap(r, cp_s, "memcpy_s/overlap", 0); /* ESOVRLP */
}

/* METHOD: cplus_memcpy_s_auto (auto-bounds SafeCRT wrapper) */
static void m_mem_03_memcpy_s_auto(Res *r) {
    t_copy_dests(r, cp_auto, "memcpy_s_auto");
    t_copy_zero(r, cp_auto, "memcpy_s_auto/zero", 1);
    t_copy_onepast(r, cp_auto, "memcpy_s_auto/one-past", 0);
    t_copy_over(r, cp_auto, "memcpy_s_auto/over-capacity", 0);
    t_copy_nulldest(r, cp_auto, "memcpy_s_auto/null-dest");
    t_copy_nullsrc(r, cp_auto, "memcpy_s_auto/null-src", 0);
    t_copy_sizemax(r, cp_auto, "memcpy_s_auto/SIZE_MAX");
    t_copy_overlap(r, cp_auto, "memcpy_s_auto/overlap", 0);
}
#endif /* !PLAIN_LIBC */

/* METHOD: cplus_memmove (ordinary-C layer, overlap-safe) */
static void m_mem_04_memmove(Res *r) {
    t_copy_dests(r, mv_raw, "memmove");
    t_copy_zero(r, mv_raw, "memmove/zero", 1);
    t_copy_onepast(r, mv_raw, "memmove/one-past", 1);
    t_copy_over(r, mv_raw, "memmove/over-capacity", 1);
    t_copy_nulldest(r, mv_raw, "memmove/null-dest");
    t_copy_nullsrc(r, mv_raw, "memmove/null-src", 1);
    t_copy_overlap(r, mv_raw, "memmove/overlap", 1); /* memmove must handle overlap */
    skip_raw_unsupported("memmove/SIZE_MAX", "raw move would loop; checked APIs cover this");
}

#ifndef PLAIN_LIBC
/* METHOD: cplus_memmove_s (checked SafeCRT API) */
static void m_mem_05_memmove_s(Res *r) {
    t_copy_dests(r, mv_s, "memmove_s");
    t_copy_zero(r, mv_s, "memmove_s/zero", 1);
    t_copy_onepast(r, mv_s, "memmove_s/one-past", 0);
    t_copy_over(r, mv_s, "memmove_s/over-capacity", 0);
    t_copy_nulldest(r, mv_s, "memmove_s/null-dest");
    t_copy_nullsrc(r, mv_s, "memmove_s/null-src", 0);
    t_copy_sizemax(r, mv_s, "memmove_s/SIZE_MAX");
    t_copy_overlap(r, mv_s, "memmove_s/overlap", 1); /* memmove_s permits overlap */
}

/* METHOD: cplus_memmove_s_auto (auto-bounds SafeCRT wrapper) */
static void m_mem_06_memmove_s_auto(Res *r) {
    t_copy_dests(r, mv_auto, "memmove_s_auto");
    t_copy_zero(r, mv_auto, "memmove_s_auto/zero", 1);
    t_copy_onepast(r, mv_auto, "memmove_s_auto/one-past", 0);
    t_copy_over(r, mv_auto, "memmove_s_auto/over-capacity", 0);
    t_copy_nulldest(r, mv_auto, "memmove_s_auto/null-dest");
    t_copy_nullsrc(r, mv_auto, "memmove_s_auto/null-src", 0);
    t_copy_sizemax(r, mv_auto, "memmove_s_auto/SIZE_MAX");
    t_copy_overlap(r, mv_auto, "memmove_s_auto/overlap", 1);
}
#endif /* !PLAIN_LIBC */

/* METHOD: cplus_memset (ordinary-C layer, raw C spelling) */
static void m_mem_07_memset(Res *r) {
    t_set_dests(r, st_raw, "memset");
    t_set_zero(r, st_raw, "memset/zero", 1);
    { Guarded g; int e = 0; long v; guarded_init(&g);
      v = st_raw(g.body, CAP_N, 0x5A, CAP_N, &e);
      REC("memset/exact", v, e, guarded_ok(&g, CAP_N), v == 1 && fill_ok(g.body, CAP_N, 0x5A)); }
    t_set_onepast(r, st_raw, "memset/one-past", 1);
    t_set_over(r, st_raw, "memset/over-capacity", 1);
    t_set_nulldest(r, st_raw, "memset/null-dest");
    skip_raw_unsupported("memset/SIZE_MAX", "raw fill would loop; checked APIs cover this");
}

#ifndef PLAIN_LIBC
/* METHOD: cplus_memset_s (checked SafeCRT API) */
static void m_mem_08_memset_s(Res *r) {
    t_set_dests(r, st_s, "memset_s");
    t_set_zero(r, st_s, "memset_s/zero", 1); /* C11: n == 0 succeeds */
    { Guarded g; int e = 0; long v; guarded_init(&g);
      v = st_s(g.body, CAP_N, 0x5A, CAP_N, &e);
      REC("memset_s/exact", v, e, guarded_ok(&g, CAP_N), v == 1 && fill_ok(g.body, CAP_N, 0x5A)); }
    t_set_onepast(r, st_s, "memset_s/one-past", 0);
    t_set_over(r, st_s, "memset_s/over-capacity", 0);
    t_set_nulldest(r, st_s, "memset_s/null-dest");
    t_set_sizemax(r, st_s, "memset_s/SIZE_MAX");
}

/* METHOD: cplus_memset_s_auto (auto-bounds SafeCRT wrapper) */
static void m_mem_09_memset_s_auto(Res *r) {
    t_set_dests(r, st_auto, "memset_s_auto");
    t_set_zero(r, st_auto, "memset_s_auto/zero", 1);
    t_set_onepast(r, st_auto, "memset_s_auto/one-past", 0);
    t_set_over(r, st_auto, "memset_s_auto/over-capacity", 0);
    t_set_nulldest(r, st_auto, "memset_s_auto/null-dest");
    t_set_sizemax(r, st_auto, "memset_s_auto/SIZE_MAX");
}
#endif /* !PLAIN_LIBC */

/* METHOD: cplus_memcmp (ordinary-C layer, raw C spelling) */
static void m_mem_10_memcmp(Res *r) {
    t_cmp_cases(r, cm_raw, "memcmp", 0);
    skip_raw_unsupported("memcmp/SIZE_MAX", "raw compare would deref OOB; checked APIs cover this");
}

#ifndef PLAIN_LIBC
/* METHOD: cplus_memcmp_s (checked SafeCRT API) */
static void m_mem_11_memcmp_s(Res *r) {
    t_cmp_cases(r, cm_s, "memcmp_s", 1);
    { unsigned char a[BODY_N], b[BODY_N]; int e = 0, ok; long d = 123;
      pattern_init(a, sizeof a); pattern_init(b, sizeof b);
      ok = cm_s(a, CAP_N, b, CAP_N, (size_t)-1, &e, &d);
      REC("memcmp_s/SIZE_MAX", ok, e, 1, ok == 0); }
}

/* METHOD: cplus_memcmp_s_auto (auto-bounds SafeCRT wrapper) */
static void m_mem_12_memcmp_s_auto(Res *r) {
    t_cmp_cases(r, cm_auto, "memcmp_s_auto", 0);
    { unsigned char a[BODY_N], b[BODY_N]; int e = 0, ok; long d = 123;
      pattern_init(a, sizeof a); pattern_init(b, sizeof b);
      ok = cm_auto(a, CAP_N, b, CAP_N, (size_t)-1, &e, &d);
      REC("memcmp_s_auto/SIZE_MAX", ok, e, 1, d == -1); }
}

/* METHOD: cplus_memzero_s (checked SafeCRT API) */
static void m_mem_13_memzero_s(Res *r) {
    Guarded g; int e = 0; long v;
    /* full declared capacity */
    guarded_init(&g);
    errno = 0; v = cplus_memzero_s(g.body, CAP_N) == 0 ? 1 : 0; e = errno;
    REC("memzero_s/full", v, e, guarded_ok(&g, CAP_N), v == 1 && fill_ok(g.body, CAP_N, 0));
    /* prefix: tail beyond dmax must stay canary */
    guarded_init(&g);
    errno = 0; v = cplus_memzero_s(g.body, 16) == 0 ? 1 : 0; e = errno;
    REC("memzero_s/prefix", v, e, guarded_ok(&g, 16), v == 1 && fill_ok(g.body, 16, 0));
    /* size above declared capacity but inside canary body */
    guarded_init(&g);
    errno = 0; v = cplus_memzero_s(g.body, CAP_N + 6u) == 0 ? 1 : 0; e = errno;
    REC("memzero_s/over-capacity", v, e, guarded_ok(&g, CAP_N + 6u),
        v == 1 && fill_ok(g.body, CAP_N + 6u, 0));
    /* zero length is a constraint violation for memzero_s */
    guarded_init(&g);
    errno = 0; v = cplus_memzero_s(g.body, 0) == 0 ? 1 : 0; e = errno;
    REC("memzero_s/zero", v, e, guarded_ok(&g, 0), v == 0);
    /* NULL destination */
    errno = 0; v = cplus_memzero_s(NULL, CAP_N) == 0 ? 1 : 0; e = errno;
    REC("memzero_s/null-dest", v, e, 1, v == 0);
    /* SIZE_MAX */
    guarded_init(&g);
    errno = 0; v = cplus_memzero_s(g.body, (size_t)-1) == 0 ? 1 : 0; e = errno;
    REC("memzero_s/SIZE_MAX", v, e, guarded_ok(&g, 0), v == 0);
}
#endif /* !PLAIN_LIBC */

/* METHOD: cplus_memchr (ordinary-C layer) */
static void m_mem_14_memchr(Res *r) {
    Guarded g; unsigned char *p; size_t i;
    guarded_init(&g); for (i = 0; i < BODY_N; ++i) g.body[i] = pat_at(i);
    g.body[10] = 0xF0; g.body[14] = 0xF0; /* 0xF0 is not in the pattern */
    errno = 0; p = (unsigned char *)cplus_memchr(g.body, 0xF0, BODY_N);
    REC("memchr/first", p == g.body + 10 ? 1 : 0, errno, guarded_ok(&g, BODY_N),
        p == g.body + 10);
    errno = 0; p = (unsigned char *)cplus_memchr(g.body, 0xF0, 0);
    REC("memchr/zero-len", p == NULL ? 1 : 0, errno, guarded_ok(&g, BODY_N), p == NULL);
    errno = 0; p = (unsigned char *)cplus_memchr(g.body, 0xEE, BODY_N);
    REC("memchr/not-found", p == NULL ? 1 : 0, errno, guarded_ok(&g, BODY_N), p == NULL);
}

/* METHOD: cplus_memrchr (ordinary-C layer) */
static void m_mem_15_memrchr(Res *r) {
    Guarded g; unsigned char *p; size_t i;
    guarded_init(&g); for (i = 0; i < BODY_N; ++i) g.body[i] = pat_at(i);
    g.body[10] = 0xF0; g.body[14] = 0xF0; /* 0xF0 is not in the pattern */
    errno = 0; p = (unsigned char *)cplus_memrchr(g.body, 0xF0, BODY_N);
    REC("memrchr/last", p == g.body + 14 ? 1 : 0, errno, guarded_ok(&g, BODY_N),
        p == g.body + 14);
    errno = 0; p = (unsigned char *)cplus_memrchr(g.body, 0xF0, 0);
    REC("memrchr/zero-len", p == NULL ? 1 : 0, errno, guarded_ok(&g, BODY_N), p == NULL);
    errno = 0; p = (unsigned char *)cplus_memrchr(g.body, 0xEE, BODY_N);
    REC("memrchr/not-found", p == NULL ? 1 : 0, errno, guarded_ok(&g, BODY_N), p == NULL);
}

/* METHOD: cplus_memccpy (ordinary-C layer) */
static void m_mem_16_memccpy(Res *r) {
    Guarded g; unsigned char src[BODY_N]; unsigned char *p; int good;
    pattern_init(src, sizeof src); src[10] = 0xF0; src[14] = 0xF0;
    guarded_init(&g);
    errno = 0; p = (unsigned char *)cplus_memccpy(g.body, src, 0xF0, BODY_N);
    good = (p == g.body + 11) &&
           cplus_memcmp(g.body, src, 11) == 0 && guarded_ok(&g, 11);
    REC("memccpy/found", p == g.body + 11 ? 1 : 0, errno, guarded_ok(&g, 11), good);

    guarded_init(&g);
    errno = 0; p = (unsigned char *)cplus_memccpy(g.body, src, 0xF0, 0);
    REC("memccpy/zero-len", p == NULL ? 1 : 0, errno, guarded_ok(&g, 0), p == NULL);

    pattern_init(src, sizeof src); /* no 0xF0 now */
    guarded_init(&g);
    errno = 0; p = (unsigned char *)cplus_memccpy(g.body, src, 0xF0, CAP_N);
    good = (p == NULL) && pattern_ok(g.body, CAP_N) && guarded_ok(&g, CAP_N);
    REC("memccpy/not-found", p == NULL ? 1 : 0, errno, guarded_ok(&g, CAP_N), good);
}

/* METHOD: cplus_memmem (ordinary-C layer) */
static void m_mem_17_memmem(Res *r) {
    Guarded hay; unsigned char needle[3]; unsigned char *p; size_t i;
    unsigned char *body = hay.body;
    unsigned char snap[PRE_N + BODY_N + POST_N];
    guarded_init(&hay); for (i = 0; i < BODY_N; ++i) body[i] = pat_at(i);
    body[20] = 0x11; body[21] = 0x22; body[22] = 0x33;
    needle[0] = 0x11; needle[1] = 0x22; needle[2] = 0x33;
    cplus_memcpy(snap, &hay, sizeof snap);
    errno = 0; p = (unsigned char *)cplus_memmem(body, BODY_N, needle, 3);
    REC("memmem/found", p == body + 20 ? 1 : 0, errno,
        cplus_memcmp(snap, &hay, sizeof snap) == 0, p == body + 20);

    errno = 0; p = (unsigned char *)cplus_memmem(body, BODY_N, needle, 0);
    REC("memmem/empty-needle", p == body ? 1 : 0, errno,
        cplus_memcmp(snap, &hay, sizeof snap) == 0, p == body);

    needle[0] = 0x44; needle[1] = 0x55; needle[2] = 0x66;
    errno = 0; p = (unsigned char *)cplus_memmem(body, BODY_N, needle, 3);
    REC("memmem/not-found", p == NULL ? 1 : 0, errno,
        cplus_memcmp(snap, &hay, sizeof snap) == 0, p == NULL);

    errno = 0; p = (unsigned char *)cplus_memmem(body, 2, needle, 3);
    REC("memmem/needle-too-long", p == NULL ? 1 : 0, errno,
        cplus_memcmp(snap, &hay, sizeof snap) == 0, p == NULL);

    errno = 0; p = (unsigned char *)cplus_memmem(body, BODY_N, needle, 0);
    REC("memmem/zero-needle-len", p == body ? 1 : 0, errno,
        cplus_memcmp(snap, &hay, sizeof snap) == 0, p == body);
}

/* METHOD: cplus_swab (ordinary-C layer) */
static void m_mem_18_swab(Res *r) {
    Guarded g; unsigned char from[BODY_N]; size_t i; int good;
    for (i = 0; i < BODY_N; ++i) from[i] = (unsigned char)(i + 1);
    guarded_init(&g);
    errno = 0; cplus_swab(from, g.body, 16);
    good = guarded_ok(&g, 16);
    for (i = 0; i < 16; i += 2)
        if (g.body[i] != from[i + 1] || g.body[i + 1] != from[i]) good = 0;
    REC("swab/even", 1, errno, guarded_ok(&g, 16), good);

    guarded_init(&g);
    errno = 0; cplus_swab(from, g.body, 0);
    REC("swab/zero", 1, errno, guarded_ok(&g, 0), guarded_ok(&g, 0));

    guarded_init(&g);
    errno = 0; cplus_swab(from, g.body, 5);
    good = guarded_ok(&g, 4);
    for (i = 0; i < 4; i += 2)
        if (g.body[i] != from[i + 1] || g.body[i + 1] != from[i]) good = 0;
    REC("swab/odd", 1, errno, guarded_ok(&g, 4), good);
}

/* ------------------------------------------------------------------ */
/* Harness: run each method under logging OFF then ON                  */
/* ------------------------------------------------------------------ */
static int g_checks = 0, g_fails = 0;
static const char *g_cur = "?";

static void skip_raw_unsupported(const char *name, const char *reason) {
    printf("SKIP %s: %s\n", name, reason);
}
#ifdef PLAIN_LIBC
static void skip_method(const char *name, const char *reason) {
    printf("SKIP %s: %s\n", name, reason);
}
#endif

static void crash_handler(int sig) {
    char buf[192];
    int n = snprintf(buf, sizeof buf,
                     "FINDING %s: crash signal %d\n", g_cur, sig);
    if (n > 0) {
        if ((size_t)n > sizeof buf) n = (int)sizeof buf;
        (void)write(1, buf, (size_t)n);
    }
    _exit(90);
}
static void install_handlers(void) {
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS,  crash_handler);
    signal(SIGFPE,  crash_handler);
    signal(SIGILL,  crash_handler);
    signal(SIGABRT, crash_handler);
}

static void run_method(const char *name, void (*fn)(Res *)) {
    Res off = RES_INIT, on = RES_INIT;
    g_cur = name;
    set_logging(0); fn(&off);
    set_logging(1); fn(&on);
    ++g_checks;
    if (off.retmix != on.retmix || off.errmix != on.errmix ||
        off.can_ok != on.can_ok || off.sem_ok != on.sem_ok) {
        ++g_fails;
        printf("FINDING %s: logging parity mismatch "
               "(off ret=%lu err=%u can=%d sem=%d | on ret=%lu err=%u can=%d sem=%d)\n",
               name, off.retmix, off.errmix, off.can_ok, off.sem_ok,
               on.retmix, on.errmix, on.can_ok, on.sem_ok);
    } else if (!off.sem_ok || !off.can_ok) {
        ++g_fails;
        printf("FINDING %s: %s (ret=%lu err=%u can=%d sem=%d)\n",
               name, off.where ? off.where : "check failed",
               off.retmix, off.errmix, off.can_ok, off.sem_ok);
    }
}

int main(void) {
#ifndef PLAIN_LIBC
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
#endif
    install_handlers();

    run_method("m_mem_01_memcpy", m_mem_01_memcpy);
#ifndef PLAIN_LIBC
    run_method("m_mem_02_memcpy_s", m_mem_02_memcpy_s);
    run_method("m_mem_03_memcpy_s_auto", m_mem_03_memcpy_s_auto);
#else
    skip_method("m_mem_02_memcpy_s", "SafeCRT-only checked API");
    skip_method("m_mem_03_memcpy_s_auto", "SafeCRT-only auto-bounds API");
#endif
    run_method("m_mem_04_memmove", m_mem_04_memmove);
#ifndef PLAIN_LIBC
    run_method("m_mem_05_memmove_s", m_mem_05_memmove_s);
    run_method("m_mem_06_memmove_s_auto", m_mem_06_memmove_s_auto);
#else
    skip_method("m_mem_05_memmove_s", "SafeCRT-only checked API");
    skip_method("m_mem_06_memmove_s_auto", "SafeCRT-only auto-bounds API");
#endif
    run_method("m_mem_07_memset", m_mem_07_memset);
#ifndef PLAIN_LIBC
    run_method("m_mem_08_memset_s", m_mem_08_memset_s);
    run_method("m_mem_09_memset_s_auto", m_mem_09_memset_s_auto);
#else
    skip_method("m_mem_08_memset_s", "SafeCRT-only checked API");
    skip_method("m_mem_09_memset_s_auto", "SafeCRT-only auto-bounds API");
#endif
    run_method("m_mem_10_memcmp", m_mem_10_memcmp);
#ifndef PLAIN_LIBC
    run_method("m_mem_11_memcmp_s", m_mem_11_memcmp_s);
    run_method("m_mem_12_memcmp_s_auto", m_mem_12_memcmp_s_auto);
#else
    skip_method("m_mem_11_memcmp_s", "SafeCRT-only checked API");
    skip_method("m_mem_12_memcmp_s_auto", "SafeCRT-only auto-bounds API");
#endif
#ifndef PLAIN_LIBC
    run_method("m_mem_13_memzero_s", m_mem_13_memzero_s);
#else
    skip_method("m_mem_13_memzero_s", "SafeCRT-only checked API");
#endif
    run_method("m_mem_14_memchr", m_mem_14_memchr);
    run_method("m_mem_15_memrchr", m_mem_15_memrchr);
    run_method("m_mem_16_memccpy", m_mem_16_memccpy);
    run_method("m_mem_17_memmem", m_mem_17_memmem);
    run_method("m_mem_18_swab", m_mem_18_swab);

    printf("methods_memory: %d/%d checks ok\n", g_checks - g_fails, g_checks);
    return 0;
}
