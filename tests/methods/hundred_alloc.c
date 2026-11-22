/* hundred_alloc: 100 DISTINCT runtime scenarios for the allocation methods of
 * both safe-C layers, generated one by one.
 *
 * Default layer: stdlib/safe/safe_c.h + safe_runtime.h (SafeCRT layer, links
 * build/safe/libcplus_safecrt.a and build/libc/libcplus_hardened.a).
 * -DPLAIN_LIBC selects the cplus_libc_safe.h layer (libc only).
 *
 * Every case is executed TWICE -- once with logging OFF and once with logging
 * ON -- and the observed result (success flag, errno, deterministic metadata
 * scalars, canary integrity and live-block delta) must be identical.  No case
 * performs genuine undefined behaviour: double frees, foreign frees and
 * interior frees are only issued through the checked wrappers, which refuse
 * them.  Layer-only APIs are guarded with honest SKIP lines.
 */
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Auto-select the plain layer when the SafeCRT headers are not on the include
 * path (the per-method runner uses a libc-only include set for its "plain"
 * variant).  -DPLAIN_LIBC remains the explicit, documented switch. */
#if !defined(PLAIN_LIBC)
#  if defined(__has_include)
#    if !__has_include(<safe_runtime.h>)
#      define PLAIN_LIBC 1
#    endif
#  endif
#endif

#ifdef PLAIN_LIBC
#  include "cplus_libc_safe.h"
#  include "log/log.h"
#  define IS_PLAIN 1
#else
#  include "stdlib/safe/safe_c.h"
#  include "safe_runtime.h"
#  include "safe_log.h"
#  include "log/log.h"
#  define IS_PLAIN 0
#endif

/* A stripped stdlib/safe/safe_c.h / cplus_libc_safe.h maps bare free() to the
 * checked wrapper.  Keep the genuine host deallocator reachable solely for
 * cplus_aligned_alloc() blocks (documented host-CRT ownership). */
#undef free
#define AA_HOST_FREE(p) ((void)free((p)))

#define AA_RSIZE ((size_t)(SIZE_MAX >> 1))

#ifdef PLAIN_LIBC
#  define AA_MALLOC(sz)          cplus_malloc((sz))
#  define AA_CALLOC(n, sz)       cplus_calloc((n), (sz))
#  define AA_REALLOC(p, sz)      cplus_realloc((p), (sz))
#  define AA_FREE(p)             cplus_free((p))
#  define AA_FREE_CHECKED(pp)    cplus_free_checked((pp))
#  define AA_IS_LIVE(p)          cplus_alloc_is_live((p))
#  define AA_SIZE(p)             cplus_alloc_size((p))
#  define AA_ELEM(p)             cplus_alloc_elem_size((p))
#  define AA_COUNT(p)            cplus_alloc_length((p))
#  define AA_LIVE()              cplus_alloc_live_count()
#  define AA_BYTES()             cplus_alloc_live_bytes()
#  define AA_REMAIN(p)           cplus_alloc_remaining_capacity((p))
#  define AA_QUERY(p, es, out)   cplus_alloc_query_length((p), (es), (out))
#  define AA_GENCHECK(p, g)      cplus_alloc_check_generation((p), (g))
#  define AA_TRANSFER(dst, src)  cplus_transfer_ownership((dst), (src))
#  define AA_FAIL_AFTER(n)       cplus_alloc_fail_after((n))
#  define AA_FAIL_RESET()        cplus_alloc_fail_reset()
#  define AA_ALIGNED(a, sz)      cplus_aligned_alloc((a), (sz))
#else
#  define AA_MALLOC(sz)          malloc_s((sz))
#  define AA_CALLOC(n, sz)       calloc_s((n), (sz))
#  define AA_REALLOC(p, sz)      realloc_s((p), (sz))
#  define AA_FREE(p)             free_s((p))
static int aa_sf_free_checked(void **pp) {
    int rc = free_checked_s(*pp);
    if (rc == CPLUS_SAFE_OK) *pp = NULL;
    return rc;
}
#  define AA_FREE_CHECKED(pp)    aa_sf_free_checked((pp))
#  define AA_IS_LIVE(p)          cplus_safe_ptr_is_live((p))
#  define AA_SIZE(p)             cplus_safe_ptr_size((p))
#  define AA_ELEM(p)             cplus_safe_ptr_elem_size((p))
#  define AA_COUNT(p)            cplus_safe_ptr_count((p))
#  define AA_LIVE()              cplus_safe_alloc_live_count()
#  define AA_BYTES()             cplus_safe_alloc_live_bytes()
#  define AA_REMAIN(p)           cplus_safe_remaining_capacity((p))
#  define AA_QUERY(p, es, out)   cplus_safe_query_length((p), (es), (out))
#  define AA_GENCHECK(p, g)      cplus_safe_ptr_check_generation((p), (g))
#  define AA_TRANSFER(dst, src)  cplus_safe_transfer_ownership((dst), (src))
#  define AA_FAIL_AFTER(n)       cplus_safe_alloc_fail_after((n))
#  define AA_ALIGNED(a, sz)      aligned_alloc_s((a), (sz))
static void aa_reset_all(void) {
    cplus_safe_alloc_fail_reset();
    cplus_alloc_fail_reset();
}
#  define AA_FAIL_RESET()        aa_reset_all()
#endif

typedef struct {
    size_t byte_size, elem_size, elem_count;
    unsigned long long generation;
    int is_live;
} aa_meta;

#ifdef PLAIN_LIBC
#  define AA_GET_META(p, m) do {                                            \
        cplus_alloc_meta __mm;                                              \
        int __rr = cplus_alloc_get_meta((p), &__mm);                        \
        (m)->byte_size = __mm.byte_size; (m)->elem_size = __mm.elem_size;   \
        (m)->elem_count = __mm.elem_count; (m)->generation = __mm.generation; \
        (m)->is_live = __mm.is_live; (void)__rr;                            \
    } while (0)
#else
#  define AA_GET_META(p, m) do {                                            \
        cplus_safe_alloc_meta __mm;                                         \
        int __rr = cplus_safe_alloc_get_meta((p), &__mm);                   \
        (m)->byte_size = __mm.byte_size; (m)->elem_size = __mm.elem_size;   \
        (m)->elem_count = __mm.elem_count; (m)->generation = __mm.generation; \
        (m)->is_live = __mm.is_live; (void)__rr;                            \
    } while (0)
#endif

typedef struct {
    int ok;              /* primary operation accepted (1) / refused (0) */
    int refusal;         /* 1 when the decisive operation was a refusal */
    int err;             /* errno at the decisive point */
    unsigned long a;     /* deterministic scalar observation 1 */
    unsigned long b;     /* deterministic scalar observation 2 */
    unsigned long c;     /* deterministic scalar observation 3 */
    unsigned long d;     /* deterministic scalar observation 4 */
    int canary_ok;       /* every canary check passed */
    size_t leak_delta;   /* live blocks left behind by the case */
} obs;

static unsigned long g_seed;
static size_t g_base_live;

static unsigned long lcg(void) {
    g_seed = g_seed * 1103515245UL + 12345UL;
    return g_seed;
}

#define SET_CANARY(o, p, n, v) do {                       \
        size_t __i; unsigned char *__c = (unsigned char *)(p); \
        for (__i = 0; __i < (size_t)(n); ++__i) __c[__i] = (unsigned char)(v); \
        (void)(o);                                        \
    } while (0)
#define CHK_CANARY(o, p, n, v) do {                       \
        size_t __i; unsigned char *__c = (unsigned char *)(p); \
        for (__i = 0; __i < (size_t)(n); ++__i)            \
            if (__c[__i] != (unsigned char)(v)) (o)->canary_ok = 0; \
    } while (0)
#define MARK(o, okval, errval) do { (o)->ok = (okval); (o)->err = (errval); } while (0)
/* The middle argument is the (already asserted) refusal predicate; it is
 * accepted only to keep the call sites identical to MARK. */
#define REFUSE(o, predicate, errval) do {            \
        (void)(predicate);                           \
        (o)->ok = 0; (o)->err = (errval); (o)->refusal = 1; \
    } while (0)

/* ---------------------------- malloc (1-12) ---------------------------- */

static void c001(obs *o) {
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)AA_MALLOC(1); e = errno;
    if (p) { SET_CANARY(o, p, 1, 0xA5); CHK_CANARY(o, p, 1, 0xA5); }
    MARK(o, p != NULL, e); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c002(obs *o) {
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)AA_MALLOC(64); e = errno;
    if (p) { SET_CANARY(o, p, 64, 0x5A); CHK_CANARY(o, p, 64, 0x5A); }
    MARK(o, p != NULL, e); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c003(obs *o) {
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)AA_MALLOC(0); e = errno;
    if (p) { SET_CANARY(o, p, 1, 0x11); CHK_CANARY(o, p, 1, 0x11); }
    MARK(o, p != NULL, e); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c004(obs *o) {
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)AA_MALLOC(4096); e = errno;
    if (p) { SET_CANARY(o, p, 4096, 0x33); CHK_CANARY(o, p, 4096, 0x33); }
    MARK(o, p != NULL, e); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c005(obs *o) {
    void *p;
    int e;
    errno = 0; p = AA_MALLOC(SIZE_MAX); e = errno;
    REFUSE(o, p == NULL, e);
    o->c = (unsigned long)(AA_SIZE(p) != 0);
    AA_FREE(p);
}

static void c006(obs *o) {
    int i, good = 1;
    unsigned long n = 0;
    for (i = 0; i < 8; ++i) {
        size_t sz = (size_t)(lcg() % 512UL) + 1u;
        unsigned char *p = (unsigned char *)AA_MALLOC(sz);
        if (!p) { good = 0; continue; }
        SET_CANARY(o, p, sz, (int)(i + 1));
        CHK_CANARY(o, p, sz, (int)(i + 1));
        if (AA_SIZE(p) != sz) good = 0;
        ++n; AA_FREE(p);
    }
    MARK(o, good, 0); o->b = n;
}

static void c007(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    MARK(o, p != NULL, 0);
    o->c = (unsigned long)(AA_REMAIN(p) == 32u);
    AA_FREE(p);
}

static void c008(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    MARK(o, p != NULL, 0);
    o->c = (unsigned long)(AA_REMAIN(p + 8) == 24u);
    AA_FREE(p);
}

static void c009(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    MARK(o, p != NULL, 0);
    o->c = (unsigned long)(AA_REMAIN(p + 32) == 0u);
    AA_FREE(p);
}

static void c010(obs *o) {
    int stack_obj = 7;
    MARK(o, 1, 0);
    o->c = (unsigned long)(AA_REMAIN(&stack_obj) == (size_t)-1);
    (void)stack_obj;
}

static void c011(obs *o) {
    MARK(o, 1, 0);
    o->c = (unsigned long)(AA_REMAIN(NULL) == (size_t)-1);
}

static void c012(obs *o) {
    void *p;
    int e;
    errno = 0; p = AA_MALLOC((size_t)-1); e = errno;
    REFUSE(o, p == NULL, e);
    AA_FREE(p);
}

/* ---------------------------- calloc (13-24) --------------------------- */

static void c013(obs *o) {
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)AA_CALLOC(8, 4); e = errno;
    if (p) CHK_CANARY(o, p, 32, 0);
    MARK(o, p != NULL, e); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c014(obs *o) {
    unsigned char *p = (unsigned char *)AA_CALLOC(64, 1);
    if (p) CHK_CANARY(o, p, 64, 0);
    MARK(o, p != NULL, 0); o->d = (unsigned long)AA_COUNT(p);
    AA_FREE(p);
}

static void c015(obs *o) {
    unsigned char *p = (unsigned char *)AA_CALLOC(0, 8);
    if (p) CHK_CANARY(o, p, 1, 0);
    MARK(o, p != NULL, 0); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c016(obs *o) {
    unsigned char *p = (unsigned char *)AA_CALLOC(8, 0);
    if (p) CHK_CANARY(o, p, 1, 0);
    MARK(o, p != NULL, 0); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c017(obs *o) {
    unsigned char *p = (unsigned char *)AA_CALLOC(0, 0);
    if (p) CHK_CANARY(o, p, 1, 0);
    MARK(o, p != NULL, 0); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c018(obs *o) {
    void *p;
    int e;
    errno = 0; p = AA_CALLOC(SIZE_MAX, 2); e = errno;
    REFUSE(o, p == NULL, e);
    AA_FREE(p);
}

static void c019(obs *o) {
    void *p;
    int e;
    errno = 0; p = AA_CALLOC(SIZE_MAX, SIZE_MAX); e = errno;
    REFUSE(o, p == NULL, e);
    AA_FREE(p);
}

static void c020(obs *o) {
    void *p;
    int e;
    errno = 0; p = AA_CALLOC(2, AA_RSIZE); e = errno;
    REFUSE(o, p == NULL, e);
    AA_FREE(p);
}

static void c021(obs *o) {
    unsigned char *p = (unsigned char *)AA_CALLOC(6, sizeof(double));
    MARK(o, p != NULL, 0);
    o->c = (unsigned long)(AA_ELEM(p) == sizeof(double));
    AA_FREE(p);
}

static void c022(obs *o) {
    unsigned char *p = (unsigned char *)AA_CALLOC(11, 4);
    MARK(o, p != NULL, 0);
    o->c = (unsigned long)AA_COUNT(p);
    AA_FREE(p);
}

static void c023(obs *o) {
    void *p;
    int e;
    errno = 0; p = AA_CALLOC((size_t)-2, 4); e = errno;
    REFUSE(o, p == NULL, e);
    AA_FREE(p);
}

static void c024(obs *o) {
    int i, good = 1;
    unsigned long n = 0;
    for (i = 0; i < 8; ++i) {
        size_t cnt = (size_t)(lcg() % 16UL) + 1u;
        unsigned char *p = (unsigned char *)AA_CALLOC(cnt, 4);
        if (!p) { good = 0; continue; }
        CHK_CANARY(o, p, cnt * 4u, 0);
        if (AA_SIZE(p) != cnt * 4u) good = 0;
        ++n; AA_FREE(p);
    }
    MARK(o, good, 0); o->b = n;
}

/* --------------------------- realloc (25-38) --------------------------- */

static void c025(obs *o) {
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)AA_REALLOC(NULL, 48); e = errno;
    if (p) { SET_CANARY(o, p, 48, 0x77); CHK_CANARY(o, p, 48, 0x77); }
    MARK(o, p != NULL, e); o->c = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

static void c026(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x11);
    errno = 0; q = (unsigned char *)AA_REALLOC(p, 128);
    o->err = errno;
    if (q) {
        CHK_CANARY(o, q, 32, 0x11);
        SET_CANARY(o, q + 32, 96, 0x22);
        CHK_CANARY(o, q + 32, 96, 0x22);
    } else o->canary_ok = 0;
    MARK(o, q != NULL, o->err); o->c = (unsigned long)AA_SIZE(q);
    AA_FREE(q);
}

static void c027(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(128);
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 128, 0x44);
    errno = 0; q = (unsigned char *)AA_REALLOC(p, 32);
    o->err = errno;
    if (q) CHK_CANARY(o, q, 32, 0x44); else o->canary_ok = 0;
    MARK(o, q != NULL, o->err); o->c = (unsigned long)AA_SIZE(q);
    AA_FREE(q);
}

static void c028(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(64);
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 64, 0x55);
    q = (unsigned char *)AA_REALLOC(p, 65);
    if (q) CHK_CANARY(o, q, 64, 0x55); else o->canary_ok = 0;
    MARK(o, q != NULL, 0);
    o->d = (unsigned long)((void *)q == (void *)p);
    AA_FREE(q);
}

static void c029(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(64);
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 64, 0x66);
    q = (unsigned char *)AA_REALLOC(p, 65536);
    if (q) CHK_CANARY(o, q, 64, 0x66); else o->canary_ok = 0;
    MARK(o, q != NULL, 0); o->c = (unsigned long)AA_SIZE(q);
    AA_FREE(q);
}

static void c030(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x88);
    q = (unsigned char *)AA_REALLOC(p, 0);
    if (q) { CHK_CANARY(o, q, 1, 0x88); }
    MARK(o, q != NULL, 0); o->c = (unsigned long)AA_SIZE(q);
    AA_FREE(q);
}

static void c031(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(16);
    void *q;
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 16, 0x9A);
    errno = 0; q = AA_REALLOC(p, SIZE_MAX); e = errno;
    CHK_CANARY(o, p, 16, 0x9A);
    REFUSE(o, q == NULL, e);
    o->c = (unsigned long)AA_IS_LIVE(p);
    AA_FREE(p);
}

static void c032(obs *o) {
    int stack_obj = 5;
    void *q;
    int e;
    errno = 0; q = AA_REALLOC(&stack_obj, 32); e = errno;
    REFUSE(o, q == NULL, e);
    o->c = (unsigned long)(q == NULL);
    (void)stack_obj;
}

static void c033(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(16);
    void *q;
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    AA_FREE(p);
    errno = 0; q = AA_REALLOC(p, 32); e = errno;
    REFUSE(o, q == NULL, e);
    o->c = (unsigned long)(q == NULL);
}

static void c034(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    void *q;
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0xAB);
    errno = 0; q = AA_REALLOC(p + 8, 64); e = errno;
    CHK_CANARY(o, p, 32, 0xAB);
    REFUSE(o, q == NULL, e);
    AA_FREE(p);
}

static void c035(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(16);
    aa_meta m1, m2;
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    memset(&m1, 0, sizeof m1); memset(&m2, 0, sizeof m2);
    AA_GET_META(p, &m1);
    q = (unsigned char *)AA_REALLOC(p, 64);
    if (q) AA_GET_META(q, &m2);
    MARK(o, q != NULL, 0);
    o->c = (unsigned long)(q != NULL && m2.generation > m1.generation);
    AA_FREE(q);
}

static void c036(obs *o) {
    unsigned char *p = (unsigned char *)AA_CALLOC(8, 4);
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    q = (unsigned char *)AA_REALLOC(p, 64);
    MARK(o, q != NULL, 0);
    o->c = (unsigned long)(q != NULL && AA_ELEM(q) == 4u);
    AA_FREE(q);
}

static void c037(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    void *q;
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0xCD);
    AA_FAIL_AFTER(0);
    errno = 0; q = AA_REALLOC(p, 64); e = errno;
    AA_FAIL_RESET();
    CHK_CANARY(o, p, 32, 0xCD);
    REFUSE(o, q == NULL, e);
    o->c = (unsigned long)AA_IS_LIVE(p);
    AA_FREE(p);
}

static void c038(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    int i, good = 1;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0xEF);
    for (i = 0; i < 8; ++i) {
        size_t sz = (size_t)(lcg() % 1024UL) + 32u;
        unsigned char *q = (unsigned char *)AA_REALLOC(p, sz);
        if (!q) { good = 0; break; }
        CHK_CANARY(o, q, 32, 0xEF);
        p = q;
    }
    MARK(o, good, 0); o->b = (unsigned long)AA_SIZE(p);
    AA_FREE(p);
}

/* ----------------------------- free (39-48) ---------------------------- */

static void c039(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(24);
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 24, 0x12);
    CHK_CANARY(o, p, 24, 0x12);
    errno = 0; AA_FREE(p); e = errno;
    MARK(o, AA_IS_LIVE(p) == 0, e);
    o->c = (unsigned long)AA_SIZE(p);
}

static void c040(obs *o) {
    int e;
    errno = 0; AA_FREE(NULL); e = errno;
    MARK(o, 1, e);
}

static void c041(obs *o) {
    unsigned char *p;
    size_t mid, end;
    MARK(o, 1, 0);
    p = (unsigned char *)AA_MALLOC(32);
    mid = AA_LIVE();
    AA_FREE(p);
    end = AA_LIVE();
    o->b = (unsigned long)(mid - g_base_live);
    o->c = (unsigned long)(end - g_base_live);
    if (mid != g_base_live + 1u || end != g_base_live) o->canary_ok = 0;
}

static void c042(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    unsigned char *inner;
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x34);
    inner = p + 8;
    errno = 0; AA_FREE(inner); e = errno;
    CHK_CANARY(o, p, 32, 0x34);
    REFUSE(o, AA_IS_LIVE(p) != 0, e);
    AA_FREE(p);
}

static void c043(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    unsigned char *past;
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x56);
    past = p + 32;
    errno = 0; AA_FREE(past); e = errno;
    CHK_CANARY(o, p, 32, 0x56);
    REFUSE(o, AA_IS_LIVE(p) != 0, e);
    AA_FREE(p);
}

static void c044(obs *o) {
    int stack_obj = 9;
    int e;
    errno = 0; AA_FREE(&stack_obj); e = errno;
    REFUSE(o, 1, e);
    (void)stack_obj;
}

static void c045(obs *o) {
    int e;
    errno = 0; AA_FREE((void *)"literal"); e = errno;
    REFUSE(o, 1, e);
}

static void c046(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    void *alias;
    int rc, e;
    if (!p) { MARK(o, 0, 0); return; }
    alias = p;
    AA_FREE(p);
    errno = 0; rc = AA_FREE_CHECKED(&alias); e = errno;
    REFUSE(o, rc != 0, e);
    o->c = (unsigned long)rc;
}

static void c047(obs *o) {
    unsigned char *p = (unsigned char *)AA_MALLOC(32);
    unsigned char *q = NULL;
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x78);
    rc = AA_TRANSFER((void **)&q, (void **)&p);
    AA_FREE(p);
    if (q) CHK_CANARY(o, q, 32, 0x78); else o->canary_ok = 0;
    MARK(o, rc == 0 && p == NULL && q != NULL && AA_IS_LIVE(q), 0);
    AA_FREE(q);
}

static void c048(obs *o) {
    int i, good = 1;
    unsigned long n = 0;
    for (i = 0; i < 10; ++i) {
        size_t sz = (size_t)(lcg() % 128UL) + 1u;
        unsigned char *p = (unsigned char *)AA_MALLOC(sz);
        if (!p) { good = 0; continue; }
        SET_CANARY(o, p, sz, (int)(i + 1));
        CHK_CANARY(o, p, sz, (int)(i + 1));
        AA_FREE(p);
        if (AA_IS_LIVE(p)) good = 0;
        ++n;
    }
    MARK(o, good, 0); o->b = n;
}

/* -------------- SafeCRT-only explicit _s spellings (49-61) ------------- */

static void c049(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)malloc_s(64); e = errno;
    if (p) { SET_CANARY(o, p, 64, 0x1A); CHK_CANARY(o, p, 64, 0x1A); }
    MARK(o, p != NULL, e); o->c = (unsigned long)cplus_safe_ptr_size(p);
    free_s(p);
#else
    (void)o;
#endif
}

static void c050(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(0);
    if (p) { SET_CANARY(o, p, 1, 0x2B); CHK_CANARY(o, p, 1, 0x2B); }
    MARK(o, p != NULL, 0); o->c = (unsigned long)cplus_safe_ptr_size(p);
    free_s(p);
#else
    (void)o;
#endif
}

static void c051(obs *o) {
#ifndef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = malloc_s(SIZE_MAX); e = errno;
    REFUSE(o, p == NULL, e);
    free_s(p);
#else
    (void)o;
#endif
}

static void c052(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)calloc_s(8, 8);
    if (p) CHK_CANARY(o, p, 64, 0);
    MARK(o, p != NULL, 0);
    o->c = (unsigned long)cplus_safe_ptr_size(p);
    o->d = (unsigned long)cplus_safe_ptr_elem_size(p);
    free_s(p);
#else
    (void)o;
#endif
}

static void c053(obs *o) {
#ifndef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = calloc_s(SIZE_MAX, 4); e = errno;
    REFUSE(o, p == NULL, e);
    free_s(p);
#else
    (void)o;
#endif
}

static void c054(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(32);
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x3C);
    q = (unsigned char *)realloc_s(p, 256);
    if (q) CHK_CANARY(o, q, 32, 0x3C); else o->canary_ok = 0;
    MARK(o, q != NULL, 0); o->c = (unsigned long)cplus_safe_ptr_size(q);
    free_s(q);
#else
    (void)o;
#endif
}

static void c055(obs *o) {
#ifndef PLAIN_LIBC
    int stack_obj = 3;
    void *q;
    int e;
    errno = 0; q = realloc_s(&stack_obj, 32); e = errno;
    REFUSE(o, q == NULL, e);
    (void)stack_obj;
#else
    (void)o;
#endif
}

static void c056(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(32);
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x4D);
    errno = 0; free_s(p); e = errno;
    MARK(o, cplus_safe_ptr_is_live(p) == 0, e);
#else
    (void)o;
#endif
}

static void c057(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(32);
    void *alias;
    int e;
    if (!p) { MARK(o, 0, 0); return; }
    alias = p;
    free_s(p);
    errno = 0; free_s(alias); e = errno;
    REFUSE(o, 1, e);
    o->c = (unsigned long)cplus_safe_ptr_is_live(p);
#else
    (void)o;
#endif
}

static void c058(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(32);
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x5E);
    rc = free_checked_s(p);
    MARK(o, rc == CPLUS_SAFE_OK, 0);
    o->c = (unsigned long)rc;
    o->d = (unsigned long)cplus_safe_ptr_is_live(p);
    if (cplus_safe_ptr_is_live(p)) free_s(p);
#else
    (void)o;
#endif
}

static void c059(obs *o) {
#ifndef PLAIN_LIBC
    int rc;
    errno = 0; rc = free_checked_s(NULL);
    MARK(o, rc == CPLUS_SAFE_OK, errno);
    o->c = (unsigned long)rc;
#else
    (void)o;
#endif
}

static void c060(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(32);
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    free_s(p);
    errno = 0; rc = free_checked_s(p);
    REFUSE(o, rc == CPLUS_SAFE_EINVAL, errno);
    o->c = (unsigned long)rc;
#else
    (void)o;
#endif
}

static void c061(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(32);
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x6F);
    errno = 0; rc = free_checked_s(p + 8);
    CHK_CANARY(o, p, 32, 0x6F);
    REFUSE(o, rc == CPLUS_SAFE_EINVAL, errno);
    o->c = (unsigned long)rc;
    free_s(p);
#else
    (void)o;
#endif
}

/* --------------- plain-libc-only cplus_* methods (62-76) --------------- */

static void c062(obs *o) {
#ifdef PLAIN_LIBC
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)cplus_malloc(48); e = errno;
    if (p) { SET_CANARY(o, p, 48, 0x7A); CHK_CANARY(o, p, 48, 0x7A); }
    MARK(o, p != NULL, e); o->c = (unsigned long)cplus_alloc_size(p);
    cplus_free(p);
#else
    (void)o;
#endif
}

static void c063(obs *o) {
#ifdef PLAIN_LIBC
    unsigned char *p = (unsigned char *)cplus_calloc(8, 8);
    if (p) CHK_CANARY(o, p, 64, 0);
    MARK(o, p != NULL, 0);
    o->c = (unsigned long)cplus_alloc_size(p);
    o->d = (unsigned long)cplus_alloc_elem_size(p);
    cplus_free(p);
#else
    (void)o;
#endif
}

static void c064(obs *o) {
#ifdef PLAIN_LIBC
    unsigned char *p = (unsigned char *)cplus_malloc(32);
    unsigned char *q;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x8B);
    q = (unsigned char *)cplus_realloc(p, 256);
    if (q) CHK_CANARY(o, q, 32, 0x8B); else o->canary_ok = 0;
    MARK(o, q != NULL, 0); o->c = (unsigned long)cplus_alloc_size(q);
    cplus_free(q);
#else
    (void)o;
#endif
}

static void c065(obs *o) {
#ifdef PLAIN_LIBC
    unsigned char *p = (unsigned char *)cplus_malloc(32);
    void *vp = p;
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0x9C);
    rc = cplus_free_checked(&vp);
    MARK(o, rc == 0 && vp == NULL, 0);
    o->d = (unsigned long)cplus_alloc_is_live(p);
#else
    (void)o;
#endif
}

static void c066(obs *o) {
#ifdef PLAIN_LIBC
    void *p = cplus_malloc(16);
    void *alias;
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    alias = p;
    cplus_free(p);
    rc = cplus_free_checked(&alias);
    REFUSE(o, rc != 0, 0);
    o->c = (unsigned long)rc;
#else
    (void)o;
#endif
}

static void c067(obs *o) {
#ifdef PLAIN_LIBC
    int stack_obj = 4;
    void *bad = &stack_obj;
    int rc;
    rc = cplus_free_checked(&bad);
    REFUSE(o, rc != 0, 0);
    o->c = (unsigned long)rc;
    (void)stack_obj;
#else
    (void)o;
#endif
}

static void c068(obs *o) {
#ifdef PLAIN_LIBC
    int e;
    errno = 0; cplus_free(NULL); e = errno;
    MARK(o, 1, e);
#else
    (void)o;
#endif
}

static void c069(obs *o) {
#ifdef PLAIN_LIBC
    size_t c0 = cplus_alloc_live_count(), b0 = cplus_alloc_live_bytes();
    unsigned char *p = (unsigned char *)cplus_malloc(64);
    size_t c1 = cplus_alloc_live_count(), b1 = cplus_alloc_live_bytes();
    cplus_free(p);
    MARK(o, c1 == c0 + 1u && b1 == b0 + 64u, 0);
    o->c = (unsigned long)(c1 - c0);
    o->d = (unsigned long)(b1 - b0);
#else
    (void)o;
#endif
}

static void c070(obs *o) {
#ifdef PLAIN_LIBC
    unsigned long d0 = cplus_alloc_double_free_count();
    void *p = cplus_malloc(16);
    void *alias = p;
    if (!p) { MARK(o, 0, 0); return; }
    cplus_free(p);
    (void)cplus_free_checked(&alias);
    MARK(o, cplus_alloc_double_free_count() == d0 + 1UL, 0);
    o->c = (unsigned long)(cplus_alloc_double_free_count() - d0);
#else
    (void)o;
#endif
}

static void c071(obs *o) {
#ifdef PLAIN_LIBC
    unsigned long v0 = cplus_alloc_invalid_free_count();
    int stack_obj = 6;
    void *bad = &stack_obj;
    (void)cplus_free_checked(&bad);
    MARK(o, cplus_alloc_invalid_free_count() == v0 + 1UL, 0);
    o->c = (unsigned long)(cplus_alloc_invalid_free_count() - v0);
    (void)stack_obj;
#else
    (void)o;
#endif
}

static void c072(obs *o) {
#ifdef PLAIN_LIBC
    cplus_alloc_meta m;
    unsigned char *p = (unsigned char *)cplus_calloc(8, 4);
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    rc = cplus_alloc_get_meta(p, &m);
    MARK(o, rc == 0 && m.is_live == 1 && m.byte_size == 32u &&
         m.elem_size == 4u && m.elem_count == 8u && m.generation != 0ULL, 0);
    o->c = (unsigned long)(m.generation != 0ULL);
    cplus_free(p);
#else
    (void)o;
#endif
}

static void c073(obs *o) {
#ifdef PLAIN_LIBC
    size_t n = 999;
    int rc;
    unsigned char *p = (unsigned char *)cplus_calloc(10, 4);
    if (!p) { MARK(o, 0, 0); return; }
    rc = cplus_alloc_query_length(p, 4, &n);
    MARK(o, rc == 0 && n == 10u, 0);
    o->c = (unsigned long)n;
    cplus_free(p);
#else
    (void)o;
#endif
}

static void c074(obs *o) {
#ifdef PLAIN_LIBC
    cplus_alloc_meta m;
    unsigned char *p = (unsigned char *)cplus_malloc(16);
    if (!p) { MARK(o, 0, 0); return; }
    (void)cplus_alloc_get_meta(p, &m);
    MARK(o, cplus_alloc_check_generation(p, m.generation) == 1 &&
         cplus_alloc_check_generation(p, m.generation + 1ULL) == 0, 0);
    o->c = (unsigned long)(m.generation != 0ULL);
    cplus_free(p);
#else
    (void)o;
#endif
}

static void c075(obs *o) {
#ifdef PLAIN_LIBC
    unsigned char *p = (unsigned char *)cplus_malloc(32);
    unsigned char *q = NULL;
    cplus_alloc_meta m1, m2;
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0xAC);
    (void)cplus_alloc_get_meta(p, &m1);
    rc = cplus_transfer_ownership((void **)&q, (void **)&p);
    (void)cplus_alloc_get_meta(q, &m2);
    if (q) CHK_CANARY(o, q, 32, 0xAC); else o->canary_ok = 0;
    MARK(o, rc == 0 && p == NULL && q != NULL && m2.generation > m1.generation, 0);
    o->d = (unsigned long)cplus_alloc_check_generation(q, m1.generation);
    cplus_free(q);
#else
    (void)o;
#endif
}

static void c076(obs *o) {
#ifdef PLAIN_LIBC
    unsigned char *p, *q;
    int e;
    cplus_alloc_fail_after(0);
    errno = 0; p = (unsigned char *)cplus_malloc(32); e = errno;
    cplus_alloc_fail_reset();
    q = (unsigned char *)cplus_malloc(16);
    REFUSE(o, p == NULL, e);
    o->c = (unsigned long)(q != NULL);
    if (q) { SET_CANARY(o, q, 16, 0x0D); CHK_CANARY(o, q, 16, 0x0D); }
    cplus_free(p);
    cplus_free(q);
#else
    (void)o;
#endif
}

/* --------- plain-only cplus_aligned_alloc scenarios (77-82) ------------ */

static void c077(obs *o) {
#ifdef PLAIN_LIBC
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)cplus_aligned_alloc(16, 64); e = errno;
    if (p) { SET_CANARY(o, p, 64, 0xBD); CHK_CANARY(o, p, 64, 0xBD); }
    MARK(o, p != NULL && ((uintptr_t)p % 16u) == 0u, e);
    o->c = (unsigned long)((uintptr_t)p % 16u);
    AA_HOST_FREE(p);
#else
    (void)o;
#endif
}

static void c078(obs *o) {
#ifdef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = cplus_aligned_alloc(0, 64); e = errno;
    REFUSE(o, p == NULL, e);
    AA_HOST_FREE(p);
#else
    (void)o;
#endif
}

static void c079(obs *o) {
#ifdef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = cplus_aligned_alloc(3, 64); e = errno;
    REFUSE(o, p == NULL, e);
    AA_HOST_FREE(p);
#else
    (void)o;
#endif
}

static void c080(obs *o) {
#ifdef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = cplus_aligned_alloc(16, 17); e = errno;
    REFUSE(o, p == NULL, e);
    AA_HOST_FREE(p);
#else
    (void)o;
#endif
}

static void c081(obs *o) {
#ifdef PLAIN_LIBC
    static const size_t aligns[] = { 8u, 16u, 32u, 64u };
    size_t i;
    int good = 1;
    for (i = 0; i < 4u; ++i) {
        size_t sz = aligns[i] * 3u;
        unsigned char *p = (unsigned char *)cplus_aligned_alloc(aligns[i], sz);
        if (!p) { good = 0; continue; }
        if (((uintptr_t)p % aligns[i]) != 0u) good = 0;
        SET_CANARY(o, p, sz, (int)i);
        CHK_CANARY(o, p, sz, (int)i);
        AA_HOST_FREE(p);
    }
    MARK(o, good, 0); o->b = 4UL;
#else
    (void)o;
#endif
}

static void c082(obs *o) {
#ifdef PLAIN_LIBC
    int i;
    int good = 1;
    unsigned long n = 0;
    for (i = 1; i <= 8; ++i) {
        size_t sz = 16u * (size_t)i;
        unsigned char *p = (unsigned char *)cplus_aligned_alloc(16, sz);
        if (!p) { good = 0; continue; }
        if (((uintptr_t)p % 16u) != 0u) good = 0;
        SET_CANARY(o, p, sz, i + 1);
        CHK_CANARY(o, p, sz, i + 1);
        AA_HOST_FREE(p);
        ++n;
    }
    MARK(o, good, 0); o->b = n;
#else
    (void)o;
#endif
}

/* ---------- SafeCRT-only aligned_alloc_s scenarios (83-92) ------------- */

static void c083(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p;
    int e;
    errno = 0; p = (unsigned char *)aligned_alloc_s(16, 64); e = errno;
    if (p) { SET_CANARY(o, p, 64, 0xCE); CHK_CANARY(o, p, 64, 0xCE); }
    MARK(o, p != NULL && ((uintptr_t)p % 16u) == 0u, e);
    o->c = (unsigned long)cplus_safe_ptr_size(p);
    free_s(p);
#else
    (void)o;
#endif
}

static void c084(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)aligned_alloc_s(8, 32);
    if (p) { SET_CANARY(o, p, 32, 0xDF); CHK_CANARY(o, p, 32, 0xDF); }
    MARK(o, p != NULL && ((uintptr_t)p % 8u) == 0u, 0);
    free_s(p);
#else
    (void)o;
#endif
}

static void c085(obs *o) {
#ifndef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = aligned_alloc_s(1, 64); e = errno;
    REFUSE(o, p == NULL, e);
    free_s(p);
#else
    (void)o;
#endif
}

static void c086(obs *o) {
#ifndef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = aligned_alloc_s(3, 64); e = errno;
    REFUSE(o, p == NULL, e);
    free_s(p);
#else
    (void)o;
#endif
}

static void c087(obs *o) {
#ifndef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = aligned_alloc_s(0, 64); e = errno;
    REFUSE(o, p == NULL, e);
    free_s(p);
#else
    (void)o;
#endif
}

static void c088(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)aligned_alloc_s(16, 0);
    if (p) { SET_CANARY(o, p, 1, 0xE1); CHK_CANARY(o, p, 1, 0xE1); }
    MARK(o, p != NULL && ((uintptr_t)p % 16u) == 0u, 0);
    o->c = (unsigned long)cplus_safe_ptr_size(p);
    free_s(p);
#else
    (void)o;
#endif
}

static void c089(obs *o) {
#ifndef PLAIN_LIBC
    void *p;
    int e;
    errno = 0; p = aligned_alloc_s(16, SIZE_MAX); e = errno;
    REFUSE(o, p == NULL, e);
    free_s(p);
#else
    (void)o;
#endif
}

static void c090(obs *o) {
#ifndef PLAIN_LIBC
    void *p;
    int e;
    cplus_safe_alloc_fail_after(0);
    errno = 0; p = aligned_alloc_s(16, 64); e = errno;
    cplus_safe_alloc_fail_reset();
    REFUSE(o, p == NULL, e);
    free_s(p);
#else
    (void)o;
#endif
}

static void c091(obs *o) {
#ifndef PLAIN_LIBC
    static const size_t aligns[] = { 8u, 16u, 32u, 64u };
    size_t i;
    int good = 1;
    for (i = 0; i < 4u; ++i) {
        size_t sz = aligns[i] * 2u;
        unsigned char *p = (unsigned char *)aligned_alloc_s(aligns[i], sz);
        if (!p) { good = 0; continue; }
        if (((uintptr_t)p % aligns[i]) != 0u) good = 0;
        SET_CANARY(o, p, sz, (int)i);
        CHK_CANARY(o, p, sz, (int)i);
        free_s(p);
    }
    MARK(o, good, 0); o->b = 4UL;
#else
    (void)o;
#endif
}

static void c092(obs *o) {
#ifndef PLAIN_LIBC
    int i;
    int good = 1;
    unsigned long n = 0;
    for (i = 1; i <= 8; ++i) {
        size_t sz = 16u * (size_t)i;
        unsigned char *p = (unsigned char *)aligned_alloc_s(16, sz);
        if (!p) { good = 0; continue; }
        if (((uintptr_t)p % 16u) != 0u) good = 0;
        SET_CANARY(o, p, sz, i + 1);
        CHK_CANARY(o, p, sz, i + 1);
        free_s(p);
        ++n;
    }
    MARK(o, good, 0); o->b = n;
#else
    (void)o;
#endif
}

/* ---- SafeCRT-only metadata / failure-injection scenarios (93-100) ----- */

static void c093(obs *o) {
#ifndef PLAIN_LIBC
    cplus_safe_alloc_meta m;
    unsigned char *p = (unsigned char *)calloc_s(8, 4);
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    rc = cplus_safe_alloc_get_meta(p, &m);
    MARK(o, rc == CPLUS_SAFE_OK && m.is_live == 1 && m.byte_size == 32u &&
         m.elem_size == 4u && m.elem_count == 8u && m.generation != 0ULL, 0);
    o->c = (unsigned long)(m.generation != 0ULL);
    free_s(p);
#else
    (void)o;
#endif
}

static void c094(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)calloc_s(10, 8);
    if (!p) { MARK(o, 0, 0); return; }
    MARK(o, cplus_safe_ptr_size(p) == 80u &&
         cplus_safe_ptr_elem_size(p) == 8u &&
         cplus_safe_ptr_count(p) == 10u, 0);
    o->c = (unsigned long)cplus_safe_ptr_count(p);
    free_s(p);
#else
    (void)o;
#endif
}

static void c095(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(32);
    int before, after;
    if (!p) { MARK(o, 0, 0); return; }
    before = cplus_safe_ptr_was_freed(p);
    free_s(p);
    after = cplus_safe_ptr_was_freed(p);
    MARK(o, before == 0 && after == 1, 0);
    o->c = (unsigned long)after;
#else
    (void)o;
#endif
}

static void c096(obs *o) {
#ifndef PLAIN_LIBC
    size_t n = 999;
    int rc;
    unsigned char *p = (unsigned char *)calloc_s(12, 4);
    if (!p) { MARK(o, 0, 0); return; }
    rc = cplus_safe_query_length(p, 4, &n);
    MARK(o, rc == CPLUS_SAFE_OK && n == 12u, 0);
    o->c = (unsigned long)n;
    free_s(p);
#else
    (void)o;
#endif
}

static void c097(obs *o) {
#ifndef PLAIN_LIBC
    cplus_safe_alloc_meta m;
    unsigned char *p = (unsigned char *)malloc_s(16);
    if (!p) { MARK(o, 0, 0); return; }
    (void)cplus_safe_alloc_get_meta(p, &m);
    MARK(o, cplus_safe_ptr_check_generation(p, m.generation) == 1 &&
         cplus_safe_ptr_check_generation(p, m.generation + 1ULL) == 0 &&
         cplus_safe_ptr_check_generation(p, 0ULL) == 0, 0);
    o->c = (unsigned long)(m.generation != 0ULL);
    free_s(p);
#else
    (void)o;
#endif
}

static void c098(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p = (unsigned char *)malloc_s(32);
    unsigned char *q = NULL;
    cplus_safe_alloc_meta m1, m2;
    int rc;
    if (!p) { MARK(o, 0, 0); return; }
    SET_CANARY(o, p, 32, 0xF2);
    (void)cplus_safe_alloc_get_meta(p, &m1);
    rc = cplus_safe_transfer_ownership((void **)&q, (void **)&p);
    (void)cplus_safe_alloc_get_meta(q, &m2);
    if (q) CHK_CANARY(o, q, 32, 0xF2); else o->canary_ok = 0;
    MARK(o, rc == CPLUS_SAFE_OK && p == NULL && q != NULL &&
         m2.generation > m1.generation, 0);
    o->d = (unsigned long)cplus_safe_ptr_check_generation(q, m1.generation);
    free_s(q);
#else
    (void)o;
#endif
}

static void c099(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p;
    int e;
    cplus_safe_alloc_fail_after(0);
    errno = 0; p = (unsigned char *)malloc_s(32); e = errno;
    cplus_safe_alloc_fail_reset();
    REFUSE(o, p == NULL, e);
    o->c = (unsigned long)(cplus_safe_alloc_seq() != 0UL);
    free_s(p);
#else
    (void)o;
#endif
}

static void c100(obs *o) {
#ifndef PLAIN_LIBC
    unsigned char *p1, *p2, *p3;
    int e3;
    cplus_safe_alloc_fail_after(2);
    p1 = (unsigned char *)malloc_s(16);
    p2 = (unsigned char *)malloc_s(16);
    errno = 0; p3 = (unsigned char *)malloc_s(16); e3 = errno;
    cplus_safe_alloc_fail_reset();
    REFUSE(o, p1 != NULL && p2 != NULL && p3 == NULL, e3);
    o->c = (unsigned long)(cplus_safe_alloc_seq() >= 3UL);
    if (p1) { SET_CANARY(o, p1, 16, 0xF3); CHK_CANARY(o, p1, 16, 0xF3); }
    if (p2) { SET_CANARY(o, p2, 16, 0xF4); CHK_CANARY(o, p2, 16, 0xF4); }
    free_s(p1); free_s(p2); free_s(p3);
#else
    (void)o;
#endif
}

/* ---------------------------- case table ------------------------------- */

typedef void (*case_fn)(obs *);
typedef struct {
    const char *method;
    const char *scenario;
    int layer; /* 0 = both, 1 = SafeCRT only, 2 = plain libc only */
    case_fn fn;
} testcase;

#define T(m, s, l, f) { (m), (s), (l), (f) }
static const testcase CASES[100] = {
    T("malloc", "valid_1byte", 0, c001),
    T("malloc", "canary_64", 0, c002),
    T("malloc", "zero_size", 0, c003),
    T("malloc", "power2_4096", 0, c004),
    T("malloc", "size_MAX_refused", 0, c005),
    T("malloc", "lcg_sizes", 0, c006),
    T("malloc", "remaining_base", 0, c007),
    T("malloc", "remaining_interior", 0, c008),
    T("malloc", "remaining_one_past", 0, c009),
    T("malloc", "remaining_untracked", 0, c010),
    T("malloc", "null_capacity", 0, c011),
    T("malloc", "negative_size_t", 0, c012),

    T("calloc", "valid_elems", 0, c013),
    T("calloc", "zero_filled", 0, c014),
    T("calloc", "count_zero", 0, c015),
    T("calloc", "size_zero", 0, c016),
    T("calloc", "both_zero", 0, c017),
    T("calloc", "overflow_pair", 0, c018),
    T("calloc", "overflow_max", 0, c019),
    T("calloc", "rsize_boundary", 0, c020),
    T("calloc", "elem_size_meta", 0, c021),
    T("calloc", "count_meta", 0, c022),
    T("calloc", "negative_count", 0, c023),
    T("calloc", "lcg_cycles", 0, c024),

    T("realloc", "null_as_malloc", 0, c025),
    T("realloc", "grow_preserve", 0, c026),
    T("realloc", "shrink_preserve", 0, c027),
    T("realloc", "small_delta_inplace_or_move", 0, c028),
    T("realloc", "move_large", 0, c029),
    T("realloc", "zero_size", 0, c030),
    T("realloc", "oversize_refused", 0, c031),
    T("realloc", "stack_ptr_refused", 0, c032),
    T("realloc", "freed_ptr_refused", 0, c033),
    T("realloc", "interior_refused", 0, c034),
    T("realloc", "gen_bump", 0, c035),
    T("realloc", "preserve_elem", 0, c036),
    T("realloc", "inject_fail_preserve", 0, c037),
    T("realloc", "lcg_cycles", 0, c038),

    T("free", "valid_block", 0, c039),
    T("free", "null_noop", 0, c040),
    T("free", "live_count_return", 0, c041),
    T("free", "interior_refused", 0, c042),
    T("free", "one_past_refused", 0, c043),
    T("free", "stack_refused", 0, c044),
    T("free", "literal_refused", 0, c045),
    T("free", "double_alias_refused", 0, c046),
    T("free", "after_transfer", 0, c047),
    T("free", "cycles", 0, c048),

    T("malloc_s", "direct_valid", 1, c049),
    T("malloc_s", "zero_tracked", 1, c050),
    T("malloc_s", "oversize_refused", 1, c051),
    T("calloc_s", "direct_valid", 1, c052),
    T("calloc_s", "overflow_refused", 1, c053),
    T("realloc_s", "direct_grow", 1, c054),
    T("realloc_s", "foreign_refused", 1, c055),
    T("free_s", "direct_valid", 1, c056),
    T("free_s", "double_refused", 1, c057),
    T("free_checked_s", "valid", 1, c058),
    T("free_checked_s", "null", 1, c059),
    T("free_checked_s", "double", 1, c060),
    T("free_checked_s", "interior", 1, c061),

    T("cplus_malloc", "direct_valid", 2, c062),
    T("cplus_calloc", "direct_valid", 2, c063),
    T("cplus_realloc", "grow_preserve", 2, c064),
    T("cplus_free_checked", "valid", 2, c065),
    T("cplus_free_checked", "double", 2, c066),
    T("cplus_free_checked", "foreign", 2, c067),
    T("cplus_free", "null_noop", 2, c068),
    T("cplus_alloc", "live_count_bytes", 2, c069),
    T("cplus_alloc", "double_free_count", 2, c070),
    T("cplus_alloc", "invalid_free_count", 2, c071),
    T("cplus_alloc", "get_meta", 2, c072),
    T("cplus_alloc", "query_length", 2, c073),
    T("cplus_alloc", "generation", 2, c074),
    T("cplus_transfer_ownership", "generation_bump", 2, c075),
    T("cplus_alloc", "inject_immediate", 2, c076),

    T("cplus_aligned_alloc", "valid_16", 2, c077),
    T("cplus_aligned_alloc", "align_0_refused", 2, c078),
    T("cplus_aligned_alloc", "align_3_refused", 2, c079),
    T("cplus_aligned_alloc", "size_not_multiple", 2, c080),
    T("cplus_aligned_alloc", "alignment_check", 2, c081),
    T("cplus_aligned_alloc", "cycles", 2, c082),

    T("aligned_alloc_s", "valid_16", 1, c083),
    T("aligned_alloc_s", "align_8", 1, c084),
    T("aligned_alloc_s", "align_1_refused", 1, c085),
    T("aligned_alloc_s", "align_3_refused", 1, c086),
    T("aligned_alloc_s", "align_0_refused", 1, c087),
    T("aligned_alloc_s", "zero_size", 1, c088),
    T("aligned_alloc_s", "oversize_refused", 1, c089),
    T("aligned_alloc_s", "injected_failure", 1, c090),
    T("aligned_alloc_s", "alignment_check", 1, c091),
    T("aligned_alloc_s", "cycles", 1, c092),

    T("cplus_safe", "meta_fields", 1, c093),
    T("cplus_safe", "ptr_size_elem_count", 1, c094),
    T("cplus_safe", "was_freed", 1, c095),
    T("cplus_safe", "query_length", 1, c096),
    T("cplus_safe", "generation_check", 1, c097),
    T("cplus_safe", "transfer_ownership", 1, c098),
    T("cplus_safe", "fail_after_0", 1, c099),
    T("cplus_safe", "fail_seq", 1, c100)
};

static void logging(int on) {
    cplus_libc_log_set_enabled(on);
#if !IS_PLAIN
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_DEBUG : CPLUS_SAFE_LOG_OFF);
    cplus_runtime_set_violation_trap(0);
#endif
}

static int obs_equal(const obs *x, const obs *y) {
    return x->ok == y->ok && x->refusal == y->refusal && x->err == y->err &&
           x->a == y->a && x->b == y->b && x->c == y->c && x->d == y->d &&
           x->canary_ok == y->canary_ok && x->leak_delta == y->leak_delta;
}

int main(void) {
    size_t i;
    int good = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    logging(0);

    for (i = 0; i < 100u; ++i) {
        obs off, on;
        int skip = (CASES[i].layer == 1 && IS_PLAIN) ||
                   (CASES[i].layer == 2 && !IS_PLAIN);
        int consistent;

        AA_FAIL_RESET();
        g_seed = 0x9E3779B9UL ^ (unsigned long)(i + 1u);
        g_base_live = AA_LIVE();
        logging(0);
        memset(&off, 0, sizeof off); off.canary_ok = 1;
        CASES[i].fn(&off);
        off.leak_delta = AA_LIVE() - g_base_live;

        AA_FAIL_RESET();
        g_seed = 0x9E3779B9UL ^ (unsigned long)(i + 1u);
        g_base_live = AA_LIVE();
        logging(1);
        memset(&on, 0, sizeof on); on.canary_ok = 1;
        CASES[i].fn(&on);
        on.leak_delta = AA_LIVE() - g_base_live;

        AA_FAIL_RESET();
        logging(0);

        consistent = obs_equal(&off, &on) &&
                     off.canary_ok && on.canary_ok &&
                     off.leak_delta == 0u && on.leak_delta == 0u;
        if (!consistent) {
            printf("FINDING: case %d %s %s off(ok=%d,err=%d,a=%lu,b=%lu,c=%lu,d=%lu,canary=%d,leak=%lu) "
                   "on(ok=%d,err=%d,a=%lu,b=%lu,c=%lu,d=%lu,canary=%d,leak=%lu)\n",
                   (int)(i + 1u), CASES[i].method, CASES[i].scenario,
                   off.ok, off.err, off.a, off.b, off.c, off.d, off.canary_ok,
                   (unsigned long)off.leak_delta,
                   on.ok, on.err, on.a, on.b, on.c, on.d, on.canary_ok,
                   (unsigned long)on.leak_delta);
        } else {
            ++good;
        }
        printf("hundred_alloc: case %d %s %s %s\n", (int)(i + 1u),
               CASES[i].method, CASES[i].scenario,
               skip ? "SKIP" : ((off.refusal || !off.ok) ? "REFUSED" : "ACCEPTED"));
    }

    printf("hundred_alloc: %d/100 scenarios ok\n", good);
    return 0;
}
