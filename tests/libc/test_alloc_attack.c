/* Adversarial attack test for the C+ hardened allocator.
 *
 * Every interaction goes through a DEFINED public interface.  We never
 * dereference a freed pointer and never call cplus_free twice on the same
 * address (that would make the test itself UB).  Double-free / invalid-free
 * probes use cplus_free_checked() on a saved alias.
 *
 * Behaviors that MUST hold use assert().  Behaviors that are BROKEN are
 * recorded with a "FINDING:" comment plus a guarded runtime diagnostic and a
 * non-fatal expectation, so this file remains a reusable regression test.
 */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Local declarations only (the verify command does not add install-include
 * to the include path).  Layout must match libc/src/stdlib/alloc.c. */
typedef struct cplus_alloc_meta {
    size_t byte_size;
    size_t elem_size;
    size_t elem_count;
    unsigned long long generation;
    int is_live;
} cplus_alloc_meta;

extern void *cplus_malloc(size_t);
extern void *cplus_calloc(size_t, size_t);
extern void *cplus_realloc(void *, size_t);
extern void cplus_free(void *);
extern int  cplus_free_checked(void **);
extern int  cplus_alloc_get_meta(const void *, cplus_alloc_meta *);
extern int  cplus_alloc_is_live(const void *);
extern size_t cplus_alloc_size(const void *);
extern size_t cplus_alloc_elem_size(const void *);
extern size_t cplus_alloc_length(const void *);
extern int  cplus_alloc_query_length(const void *, size_t, size_t *);
extern int  cplus_alloc_check_generation(const void *, unsigned long long);
extern int  cplus_transfer_ownership(void **, void **);
extern size_t cplus_alloc_live_count(void);
extern size_t cplus_alloc_live_bytes(void);
extern unsigned long cplus_alloc_double_free_count(void);
extern unsigned long cplus_alloc_invalid_free_count(void);
extern void cplus_alloc_fail_after(long);
extern void cplus_alloc_fail_reset(void);
extern void cplus_libc_log_set_enabled(int);

static size_t base_live;
static size_t base_bytes;
static size_t stale_live;   /* accounting left behind by Finding C */
static size_t stale_bytes;
static int findings;

#define FINDING(msg) do {                                                     \
    ++findings;                                                               \
    fprintf(stderr, "FINDING: %s\n", (msg));                                  \
} while (0)

/* ------------------------------------------------------------------------ */
/* 1. Overflow shapes: graceful NULL + errno, no crash.                      */
/* ------------------------------------------------------------------------ */
static void attack_overflow(void) {
    void *p;
    void *keep;
    size_t n = 999;

    errno = 0;
    p = cplus_malloc(SIZE_MAX);
    assert(p == NULL);
    assert(errno == ENOMEM);

    errno = 0;
    p = cplus_calloc(SIZE_MAX, 2);
    assert(p == NULL);
    assert(errno == ENOMEM);

    errno = 0;
    p = cplus_calloc(2, SIZE_MAX);
    assert(p == NULL);
    assert(errno == ENOMEM);

    /* Also probe the boundary where the multiply is representable but the
     * host allocator must still refuse it. */
    errno = 0;
    p = cplus_calloc(SIZE_MAX, 1);
    assert(p == NULL);
    assert(errno == ENOMEM);

    keep = cplus_malloc(64);
    assert(keep != NULL);
    memset(keep, 0x5A, 64);
    errno = 0;
    p = cplus_realloc(keep, SIZE_MAX);
    assert(p == NULL);
    assert(errno == ENOMEM);
    /* Original pointer and its metadata/contents survive a failed realloc. */
    assert(cplus_alloc_is_live(keep) == 1);
    assert(cplus_alloc_size(keep) == 64);
    assert(cplus_alloc_query_length(keep, 1, &n) == 0 && n == 64);
    {
        size_t i;
        for (i = 0; i < 64; ++i) assert(((unsigned char *)keep)[i] == 0x5A);
    }
    cplus_free(keep);
}

/* ------------------------------------------------------------------------ */
/* 2. Failure injection: original preserved, counters reset.                 */
/* ------------------------------------------------------------------------ */
static void attack_failure_injection(void) {
    void *orig;
    void *r;
    void *c;
    size_t l0 = cplus_alloc_live_count();
    size_t b0 = cplus_alloc_live_bytes();
    size_t i;

    /* fail_after(0) == fail the very next allocation attempt. */
    orig = cplus_malloc(64);
    assert(orig != NULL && cplus_alloc_is_live(orig));
    memset(orig, 0x5A, 64);
    cplus_alloc_fail_after(0);
    errno = 0;
    r = cplus_realloc(orig, 4096);
    assert(r == NULL && errno == ENOMEM);
    assert(cplus_alloc_is_live(orig) == 1);
    assert(cplus_alloc_size(orig) == 64);
    for (i = 0; i < 64; ++i) assert(((unsigned char *)orig)[i] == 0x5A);
    assert(cplus_alloc_live_count() == l0 + 1);
    assert(cplus_alloc_live_bytes() == b0 + 64);
    cplus_alloc_fail_reset();

    /* fail_after(1) == allow one success, then fail. */
    cplus_alloc_fail_after(1);
    r = cplus_realloc(orig, 128); /* the allowed success (may move the block) */
    assert(r != NULL);
    assert(cplus_alloc_size(r) == 128);
    for (i = 0; i < 64; ++i) assert(((unsigned char *)r)[i] == 0x5A);
    errno = 0;
    {
        void *r2 = cplus_realloc(r, 8192); /* the injected failure */
        assert(r2 == NULL && errno == ENOMEM);
        assert(cplus_alloc_is_live(r) == 1);
        assert(cplus_alloc_size(r) == 128);
        for (i = 0; i < 64; ++i) assert(((unsigned char *)r)[i] == 0x5A);
    }
    cplus_alloc_fail_reset();

    /* calloc under injection: NULL, ENOMEM, and no live/byte accounting drift. */
    {
        size_t cl0 = cplus_alloc_live_count();
        size_t cb0 = cplus_alloc_live_bytes();
        cplus_alloc_fail_after(0);
        errno = 0;
        c = cplus_calloc(4, 16);
        assert(c == NULL && errno == ENOMEM);
        assert(cplus_alloc_live_count() == cl0);
        assert(cplus_alloc_live_bytes() == cb0);
        cplus_alloc_fail_reset();
        c = cplus_calloc(4, 16);
        assert(c != NULL && cplus_alloc_is_live(c) == 1);
        assert(cplus_alloc_size(c) == 64);
        assert(cplus_alloc_elem_size(c) == 16);
        assert(cplus_alloc_length(c) == 4);
        cplus_free(c);
    }

    cplus_free(r);
    assert(cplus_alloc_live_count() == l0);
    assert(cplus_alloc_live_bytes() == b0);
}

/* ------------------------------------------------------------------------ */
/* 3. Zero-size requests: consistent, no crash, no leak.                     */
/* ------------------------------------------------------------------------ */
static void attack_zero_size(void) {
    void *z1 = cplus_malloc(0);
    void *z2 = cplus_calloc(0, 100);
    void *tmp = cplus_malloc(10);
    void *z3;
    size_t s1, s2, s3;
    assert(z1 != NULL && z2 != NULL && tmp != NULL);
    z3 = cplus_realloc(tmp, 0);
    assert(z3 != NULL);

    assert(cplus_alloc_is_live(z1) == 1);
    assert(cplus_alloc_is_live(z2) == 1);
    assert(cplus_alloc_is_live(z3) == 1);
    s1 = cplus_alloc_size(z1);
    s2 = cplus_alloc_size(z2);
    s3 = cplus_alloc_size(z3);
    /* Observed: the implementation maps a 0-byte request to 1 backing byte.
     * Assert consistency between the three paths rather than a magic value. */
    assert(s1 == s2 && s2 == s3);
    assert(s1 == 1); /* documented here as observed behavior, not UB */

    cplus_free(z1);
    cplus_free(z2);
    cplus_free(z3);
}

/* ------------------------------------------------------------------------ */
/* 4. Double free / invalid free: PROVE detection via checked API + counters. */
/* ------------------------------------------------------------------------ */
static void attack_free_detection(void) {
    unsigned long df0 = cplus_alloc_double_free_count();
    unsigned long iv0 = cplus_alloc_invalid_free_count();
    void *p = cplus_malloc(32);
    void *alias;
    int stack_obj = 7;
    void *bad = &stack_obj;
    void *h = (void *)malloc(24);

    assert(p != NULL && h != NULL);

    alias = p;
    assert(cplus_free_checked(&alias) == 0 && alias == NULL);
    assert(cplus_alloc_is_live(p) == 0);
    assert(cplus_alloc_size(p) == 0);

    /* Double free of the saved address: must be refused, counted, no crash. */
    errno = 0;
    alias = p;
    assert(cplus_free_checked(&alias) != 0);
    assert(errno == EINVAL && alias == NULL);
    assert(cplus_alloc_double_free_count() == df0 + 1);

    /* Invalid free of a stack address: counted separately, no crash. */
    errno = 0;
    assert(cplus_free_checked(&bad) != 0);
    assert(errno == EINVAL && bad == NULL);
    assert(cplus_alloc_invalid_free_count() == iv0 + 1);

    /* NULL inout / NULL pointee are defined no-ops / errors. */
    assert(cplus_free_checked(NULL) != 0);
    bad = NULL;
    assert(cplus_free_checked(&bad) == 0 && bad == NULL);

    free(h); /* host pointer was never touched by cplus_free_checked */
}

/* ------------------------------------------------------------------------ */
/* 5. Metadata forgery / host pointers / generation / ownership / NULL args. */
/* ------------------------------------------------------------------------ */
static void attack_metadata_and_ownership(void) {
    cplus_alloc_meta m;
    size_t n = 777;
    void *h;
    void *r;
    void *f;
    void *fc;
    void *src;
    void *dst;
    void *src2;
    void *x;
    unsigned long long g0, g1;

    /* Host allocator pointer must never be claimed live. */
    h = malloc(128);
    assert(h != NULL);
    assert(cplus_alloc_is_live(h) == 0);
    assert(cplus_alloc_size(h) == 0);
    assert(cplus_alloc_elem_size(h) == 0);
    assert(cplus_alloc_length(h) == 0);
    memset(&m, 0x7F, sizeof(m));
    assert(cplus_alloc_get_meta(h, &m) != 0);
    assert(m.is_live == 0 && m.byte_size == 0 && m.elem_size == 0 &&
           m.elem_count == 0 && m.generation == 0);
    n = 777;
    assert(cplus_alloc_query_length(h, 1, &n) != 0 && n == 0);
    free(h);

    /* Interior pointer must not be claimed live. */
    {
        void *base = cplus_malloc(64);
        assert(base != NULL);
        assert(cplus_alloc_is_live((char *)base + 1) == 0);
        assert(cplus_alloc_size((char *)base + 1) == 0);
        cplus_free(base);
    }

    /* Freed pointer metadata is cleared. */
    f = cplus_malloc(40);
    assert(f != NULL);
    fc = f;
    assert(cplus_free_checked(&fc) == 0);
    assert(cplus_alloc_is_live(f) == 0);
    assert(cplus_alloc_size(f) == 0);
    memset(&m, 0x7F, sizeof(m));
    assert(cplus_alloc_get_meta(f, &m) != 0);
    assert(m.is_live == 0 && m.byte_size == 0);

    /* NULL pointers are tolerated by the scalar query helpers. */
    assert(cplus_alloc_is_live(NULL) == 0);
    assert(cplus_alloc_size(NULL) == 0);
    assert(cplus_alloc_elem_size(NULL) == 0);
    assert(cplus_alloc_length(NULL) == 0);

    /* NULL / bad out-params across the metadata API. */
    assert(cplus_alloc_get_meta(NULL, &m) != 0);
    assert(cplus_alloc_get_meta(f, NULL) != 0);
    assert(cplus_alloc_query_length(NULL, 1, &n) != 0 && n == 0);
    assert(cplus_alloc_check_generation(NULL, 1) == 0);
    assert(cplus_alloc_check_generation(f, 1) == 0);

    /* Generation + ownership transfer. */
    src = cplus_malloc(32);
    assert(src != NULL);
    assert(cplus_alloc_get_meta(src, &m) == 0 && m.generation > 0);
    g0 = m.generation;
    assert(cplus_alloc_check_generation(src, g0) == 1);
    assert(cplus_alloc_check_generation(src, g0 + 1) == 0);
    assert(cplus_alloc_check_generation(src, 0) == 0);

    dst = NULL;
    assert(cplus_transfer_ownership(&dst, &src) == 0);
    assert(src == NULL && dst != NULL);
    assert(cplus_alloc_check_generation(dst, g0) == 0); /* stale gen rejected */
    assert(cplus_alloc_get_meta(dst, &m) == 0);
    g1 = m.generation;
    assert(g1 > g0);
    assert(cplus_alloc_check_generation(dst, g1) == 1);

    /* Second transfer attempts. */
    assert(cplus_transfer_ownership(&dst, &src) != 0); /* src is NULL now */
    src2 = cplus_malloc(16);
    assert(src2 != NULL);
    assert(cplus_transfer_ownership(&dst, &src2) != 0); /* dst already set */
    assert(cplus_transfer_ownership(NULL, &src2) != 0);
    assert(cplus_transfer_ownership(&dst, NULL) != 0);
    x = src2;
    assert(cplus_transfer_ownership(&x, &x) != 0); /* dst == src */

    /* Free the transferred block, then a legal transfer of src2. */
    fc = dst;
    assert(cplus_free_checked(&fc) == 0);
    dst = NULL;
    assert(cplus_transfer_ownership(&dst, &src2) == 0);
    assert(src2 == NULL && dst != NULL);
    fc = dst;
    assert(cplus_free_checked(&fc) == 0);

    /* query_length with explicit element size, zero size, and NULL out. */
    {
        void *q = cplus_calloc(6, sizeof(double));
        size_t qn = 0;
        assert(q != NULL);
        assert(cplus_alloc_query_length(q, sizeof(double), &qn) == 0 && qn == 6);
        qn = 0;
        /* elem_size == 0 means "use the recorded element size" (8 here). */
        assert(cplus_alloc_query_length(q, 0, &qn) == 0 && qn == 6);
        assert(cplus_alloc_query_length(q, sizeof(double), NULL) != 0);
        cplus_free(q);
    }

    /* realloc(NULL, n) is malloc; realloc on a host pointer is refused
     * without freeing it (defined, safe to probe). */
    {
        unsigned long iv = cplus_alloc_invalid_free_count();
        unsigned long dv = cplus_alloc_double_free_count();
        unsigned long ivd, dvd;
        void *rn = cplus_realloc(NULL, 40);
        assert(rn != NULL && cplus_alloc_is_live(rn) == 1);
        cplus_free(rn);

        h = malloc(40);
        assert(h != NULL);
        errno = 0;
        r = cplus_realloc(h, 80);
        assert(r == NULL && errno == EINVAL);
        ivd = cplus_alloc_invalid_free_count() - iv;
        dvd = cplus_alloc_double_free_count() - dv;
        assert(ivd + dvd == 1); /* exactly one diagnostic counter moved */
        if (dvd == 1) {
            /* FINDING: same recent-cache false positive as Finding A, now
             * reached through cplus_realloc() on a legitimate host pointer. */
            FINDING("A2: cplus_realloc() on a fresh host pointer is counted "
                    "as a double free (recent-cache false positive, realloc path)");
        }
        assert(cplus_alloc_is_live(h) == 0);
        free(h); /* cplus did not touch it */
    }
}

/* ------------------------------------------------------------------------ */
/* 6. Concurrency-lite: 4 threads alloc/free/query; counters self-consistent. */
/* ------------------------------------------------------------------------ */
static void *worker(void *arg) {
    int id = *(int *)arg;
    int i;
    (void)id;
    for (i = 0; i < 1500; ++i) {
        void *p = cplus_malloc(32 + (size_t)(i & 7));
        void *c;
        if (p == NULL) return (void *)1;
        memset(p, 0xA5, 8);
        (void)cplus_alloc_live_count();
        (void)cplus_alloc_live_bytes();
        (void)cplus_alloc_is_live(p);
        c = p;
        if (cplus_free_checked(&c) != 0) return (void *)2;
    }
    return NULL;
}

static void attack_concurrency(void) {
    pthread_t th[4];
    int ids[4] = {0, 1, 2, 3};
    int created = 0;
    int k;
    void *rv;
    size_t l0 = cplus_alloc_live_count();
    size_t b0 = cplus_alloc_live_bytes();

    for (k = 0; k < 4; ++k) {
        if (pthread_create(&th[k], NULL, worker, &ids[k]) == 0) ++created;
        else break;
    }
    assert(created == 4);

    /* Observe counters while workers run: each worker holds at most one
     * live block, so live_count must stay within [l0, l0+4]. */
    for (k = 0; k < 20000; ++k) {
        size_t n = cplus_alloc_live_count();
        size_t b = cplus_alloc_live_bytes();
        assert(n >= l0 && n <= l0 + 4);
        assert(b >= b0 && b <= b0 + 4 * (32 + 7));
    }

    for (k = 0; k < created; ++k) {
        assert(pthread_join(th[k], &rv) == 0);
        assert(rv == NULL);
    }
    assert(cplus_alloc_live_count() == l0);
    assert(cplus_alloc_live_bytes() == b0);
}

/* ------------------------------------------------------------------------ */
/* 7. Logging on/off around failure paths: return values must be identical.  */
/* ------------------------------------------------------------------------ */
static void attack_logging(void) {
    void *p;
    void *keep;
    void *r;
    void *alias;
    cplus_alloc_meta m;
    int rc_off, rc_on;
    int err_off, err_on;

    /* Overflow shape, logging off then on. */
    cplus_libc_log_set_enabled(0);
    errno = 0;
    p = cplus_malloc(SIZE_MAX);
    rc_off = (p == NULL);
    err_off = errno;
    cplus_libc_log_set_enabled(1);
    errno = 0;
    p = cplus_malloc(SIZE_MAX);
    rc_on = (p == NULL);
    err_on = errno;
    cplus_libc_log_set_enabled(0);
    assert(rc_off == 1 && rc_on == 1 && err_off == ENOMEM && err_on == ENOMEM);

    /* Injected calloc failure, logging off then on. */
    cplus_alloc_fail_after(0);
    errno = 0;
    p = cplus_calloc(2, SIZE_MAX);
    rc_off = (p == NULL); err_off = errno;
    cplus_alloc_fail_reset();
    cplus_alloc_fail_after(0);
    cplus_libc_log_set_enabled(1);
    errno = 0;
    p = cplus_calloc(2, SIZE_MAX);
    rc_on = (p == NULL); err_on = errno;
    cplus_libc_log_set_enabled(0);
    cplus_alloc_fail_reset();
    assert(rc_off == 1 && rc_on == 1 && err_off == ENOMEM && err_on == ENOMEM);

    /* Failed realloc (SIZE_MAX) preserves owner with logging either way. */
    keep = cplus_malloc(32);
    assert(keep != NULL);
    memset(keep, 0x33, 32);
    cplus_libc_log_set_enabled(1);
    errno = 0;
    r = cplus_realloc(keep, SIZE_MAX);
    assert(r == NULL && errno == ENOMEM);
    assert(cplus_alloc_is_live(keep) && cplus_alloc_size(keep) == 32);
    assert(((unsigned char *)keep)[0] == 0x33);
    cplus_libc_log_set_enabled(0);

    /* Double-free and invalid-free counters with logging toggled. */
    alias = keep;
    cplus_libc_log_set_enabled(0);
    assert(cplus_free_checked(&alias) == 0);
    alias = keep;
    rc_off = cplus_free_checked(&alias);
    cplus_libc_log_set_enabled(1);
    alias = keep;
    rc_on = cplus_free_checked(&alias);
    cplus_libc_log_set_enabled(0);
    assert(rc_off == EINVAL && rc_on == EINVAL);

    /* get_meta on a host pointer with logging toggled. */
    {
        void *h = malloc(16);
        assert(h != NULL);
        cplus_libc_log_set_enabled(0);
        rc_off = cplus_alloc_get_meta(h, &m);
        cplus_libc_log_set_enabled(1);
        rc_on = cplus_alloc_get_meta(h, &m);
        cplus_libc_log_set_enabled(0);
        assert(rc_off == EINVAL && rc_on == EINVAL);
        free(h);
    }

    /* transfer_ownership NULL params with logging toggled. */
    {
        void *d = NULL;
        cplus_libc_log_set_enabled(0);
        rc_off = cplus_transfer_ownership(&d, NULL);
        cplus_libc_log_set_enabled(1);
        rc_on = cplus_transfer_ownership(&d, NULL);
        cplus_libc_log_set_enabled(0);
        assert(rc_off == EINVAL && rc_on == EINVAL);
    }

    cplus_libc_log_set_enabled(0);
}

/* ------------------------------------------------------------------------ */
/* 8. Confirmed findings (guarded, non-fatal, fully documented).             */
/* ------------------------------------------------------------------------ */

/* Mirrors libc/src/stdlib/alloc.c: meta_hash(p) & (1024-1). */
static size_t recent_idx(const void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33; x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= (uintptr_t)0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x & 1023u;
}

static void findings_section(void) {
    unsigned long df0, iv0, dfd, ivd;
    void *p, *alias, *hptr, *hc;
    enum { N = 8192 };
    void **v;
    int seen[1024];
    int a = -1, b = -1, i;

    /* FINDING A: a fresh host malloc() that reuses a just-freed cplus address
     * is misclassified by cplus_free_checked() as a DOUBLE FREE, and the host
     * pointer is not freed.  Root cause: the 1024-entry "recent" cache is keyed
     * only by hash index and is not invalidated when the address is recycled. */
    p = cplus_malloc(64);
    assert(p != NULL);
    alias = p;
    assert(cplus_free_checked(&alias) == 0);
    hptr = malloc(64);
    assert(hptr != NULL);
    if (hptr == p) {
        df0 = cplus_alloc_double_free_count();
        iv0 = cplus_alloc_invalid_free_count();
        hc = hptr;
        (void)cplus_free_checked(&hc);
        dfd = cplus_alloc_double_free_count() - df0;
        ivd = cplus_alloc_invalid_free_count() - iv0;
        if (dfd == 1 && ivd == 0) {
            /* FINDING: cplus_free_checked() reported a double free for a
             * brand-new host-allocation address (false positive). */
            FINDING("A: fresh host pointer reusing a freed cplus address is "
                    "counted as a double free (false positive)");
        }
    }
    /* cplus_free_checked() did not free hptr in either branch; host reclaims it. */
    free(hptr);

    /* FINDING B: two distinct freed pointers that collide in the 1024-bucket
     * recent cache cause the first one's double-free to be counted as an
     * INVALID free instead of a double free.  Deterministic and reproducible. */
    v = (void **)malloc((size_t)N * sizeof(void *));
    assert(v != NULL);
    for (i = 0; i < N; ++i) {
        v[i] = cplus_malloc(24);
        assert(v[i] != NULL);
    }
    for (i = 0; i < 1024; ++i) seen[i] = -1;
    for (i = 0; i < N; ++i) {
        int idx = (int)recent_idx(v[i]);
        if (seen[idx] >= 0) { a = seen[idx]; b = i; break; }
        seen[idx] = i;
    }
    if (a >= 0) {
        void *pa = v[a], *pb = v[b], *ca = pa, *cb = pb, *da = pa;
        assert(cplus_free_checked(&ca) == 0);
        assert(cplus_free_checked(&cb) == 0); /* overwrites recent slot */
        df0 = cplus_alloc_double_free_count();
        iv0 = cplus_alloc_invalid_free_count();
        (void)cplus_free_checked(&da);        /* double-free of pa */
        dfd = cplus_alloc_double_free_count() - df0;
        ivd = cplus_alloc_invalid_free_count() - iv0;
        if (dfd == 0 && ivd == 1) {
            /* FINDING: double free was silently misclassified as invalid
             * free; a caller relying on double_free_count misses it. */
            FINDING("B: recent-cache hash collision makes a real double free "
                    "increment invalid_free_count instead of double_free_count");
        }
    }
    for (i = 0; i < N; ++i) {
        if (i == a || i == b) continue;
        void *c = v[i];
        (void)cplus_free_checked(&c);
    }
    free(v);

    /* FINDING C: a cplus allocation freed directly by the host allocator is
     * still reported live (stale side-table entry).  Read-only queries are
     * safe here; we deliberately never free it through cplus again. */
    {
        void *q = cplus_malloc(48);
        assert(q != NULL && cplus_alloc_is_live(q) == 1);
        free(q); /* host free bypasses cplus bookkeeping (valid host call) */
        if (cplus_alloc_is_live(q) == 1) {
            cplus_alloc_meta qm;
            /* FINDING: false "live" claim for an address the host freed. */
            FINDING("C: cplus_alloc_is_live() still returns 1 after the "
                    "address was freed by the host allocator");
            stale_live = 1;
            stale_bytes = 48;
            /* FINDING: the stale slot permanently inflates the global
             * live_count/live_bytes accounting (a diagnostic memory leak). */
            FINDING("C2: stale metadata permanently inflates live_count and "
                    "live_bytes after a host-side free");
            (void)cplus_alloc_get_meta(q, &qm); /* read-only, no deref of q */
        }
    }
}

/* ------------------------------------------------------------------------ */
int main(void) {
    base_live = cplus_alloc_live_count();
    base_bytes = cplus_alloc_live_bytes();

    attack_overflow();
    attack_failure_injection();
    attack_zero_size();
    attack_free_detection();
    attack_metadata_and_ownership();
    attack_concurrency();
    attack_logging();

    assert(cplus_libc_log_set_enabled != NULL); /* link sanity */

    /* All non-finding scenarios must be leak-free before findings run. */
    assert(cplus_alloc_live_count() == base_live);
    assert(cplus_alloc_live_bytes() == base_bytes);

    findings_section();

    /* Findings clean up all of their own cplus allocations; the only residue
     * is the documented stale-slot leak from Finding C2. */
    assert(cplus_alloc_live_count() == base_live + stale_live);
    assert(cplus_alloc_live_bytes() == base_bytes + stale_bytes);

    if (findings > 0)
        fprintf(stderr, "alloc_attack: %d documented finding(s)\n", findings);
    puts("alloc_attack: ok");
    return 0;
}
