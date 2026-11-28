/* Red-team harness for the checked allocation APIs.
 *
 * Reads a deterministic manifest (gen_cases.py), runs EVERY case in a forked
 * child so a crash/corruption cannot taint later cases, and classifies the
 * outcome as REFUSED / CRASH / CORRUPT / UNEXPECTED_OK.
 *
 * Exit codes of a child:
 *   0  CLEAN   - all expected refusals happened, all invariants held
 *   10 UNEXPECTED_OK - an operation the contract says must be REFUSED succeeded
 *   11 CORRUPT - heap/metadata/atomicity invariant violated
 *   20 SELFERR - harness could not set up a case (must be zero in a valid run)
 * A non-zero wait status caused by a signal is reported by the parent as CRASH.
 */
#define _POSIX_C_SOURCE 200112L
#include "safe_alloc.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Hardened-archive equivalents (declared locally to avoid cplus_libc.h's
 * `malloc` macro, which would rewrite our safecrt calls). */
extern void  *cplus_malloc(size_t);
extern void  *cplus_realloc(void *, size_t);
extern void   cplus_free(void *);
extern int    cplus_free_checked(void **);
extern int    cplus_alloc_is_live(const void *);
extern size_t cplus_alloc_size(const void *);
extern size_t cplus_alloc_live_count(void);
extern void   cplus_alloc_fail_after(long);
extern void   cplus_alloc_fail_reset(void);

#define RSIZE_MAX_LOCAL ((size_t)(SIZE_MAX >> 1))

enum { V_CLEAN = 0, V_UNEXPECTED_OK = 10, V_CORRUPT = 11, V_SELFERR = 20 };

static char g_detail[768];
#define DET(...) do { snprintf(g_detail, sizeof g_detail, __VA_ARGS__); } while (0)

static const char *g_fam_names[] = {
    "OVR_MALLOC", "OVR_CALLOC", "OVR_REALLOC", "ZERO_SIZE", "ALIGN_BAD",
    "ALIGN_OK", "DOUBLE_FREE", "INVALID_PTR", "REALLOC_ATOMIC",
    "META_CONSISTENCY", "QUERY_NULL", "CYCLES", "STALE_GEN", "INJECT_ALL",
    "INTERIOR_CAP", "FORGED_BATCH", "MIXED",
    "REALLOC_ELEM", "CACHE_EXHAUST", "HARDENED"
};
#define NFAM ((int)(sizeof g_fam_names / sizeof g_fam_names[0]))

typedef struct {
    unsigned long idx;
    int fam;
    uint64_t seed;
    uint64_t p[4];
} Case;

static int g_static_canary = 0x5A5A5A5A; /* known non-allocator address */

/* ---------- helpers ------------------------------------------------------- */

static void fill_pattern(void *p, size_t n, unsigned char s) {
    unsigned char *b = (unsigned char *)p;
    for (size_t i = 0; i < n; ++i) b[i] = (unsigned char)(s + i * 31u);
}
static int check_pattern(const void *p, size_t n, unsigned char s) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; ++i)
        if (b[i] != (unsigned char)(s + i * 31u)) return 0;
    return 1;
}
static uint64_t mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* ---------- per-family scenarios ------------------------------------------ */

static int f_ovr_malloc(const Case *c) {
    size_t sz = (size_t)c->p[0];
    void *p = malloc_s(sz);
    if (p != NULL) {
        DET("UNEXPECTED_OK: malloc_s(0x%zx) with sz>RSIZE_MAX returned <ptr>", sz);
        return V_UNEXPECTED_OK;
    }
    DET("malloc_s(0x%zx) refused errno=%d", sz, errno);
    return V_CLEAN;
}

static int f_ovr_calloc(const Case *c) {
    size_t n = (size_t)c->p[0], s = (size_t)c->p[1];
    void *p;
    int overflow;
    if (n == 0 || s == 0) overflow = 0;
    else if (n > SIZE_MAX / s) overflow = 1;
    else overflow = (n * s > RSIZE_MAX_LOCAL);
    p = calloc_s(n, s);
    if (overflow && p != NULL) {
        DET("UNEXPECTED_OK: calloc_s(0x%zx,0x%zx) overflow returned <ptr>", n, s);
        return V_UNEXPECTED_OK;
    }
    DET("calloc_s(0x%zx,0x%zx) overflow=%d -> <ptr>", n, s, overflow);
    return V_CLEAN;
}

static int f_ovr_realloc(const Case *c) {
    size_t alloc_sz = (size_t)c->p[1] ? (size_t)c->p[1] : 64;
    size_t huge = (size_t)c->p[0];
    void *p = malloc_s(alloc_sz);
    void *q;
    if (!p) { DET("SELFERR: malloc_s(%zu) failed in setup", alloc_sz); return V_SELFERR; }
    fill_pattern(p, alloc_sz, 0x41);
    q = realloc_s(p, huge);
    if (q != NULL) {
        DET("UNEXPECTED_OK: realloc_s(<ptr>,0x%zx) with huge size returned <ptr>", huge);
        return V_UNEXPECTED_OK;
    }
    if (!cplus_safe_ptr_is_live(p) || cplus_safe_ptr_size(p) != alloc_sz ||
        !check_pattern(p, alloc_sz, 0x41)) {
        DET("CORRUPT: realloc_s(huge) did not preserve original block");
        return V_CORRUPT;
    }
    free_s(p);
    DET("realloc_s(<ptr>,0x%zx) refused; original preserved", huge);
    return V_CLEAN;
}

static int f_zero_size(const Case *c) {
    unsigned sub = (unsigned)(c->p[0] % 5);
    void *p = NULL;
    cplus_safe_alloc_meta m;
    size_t align = (size_t)c->p[1];
    if (!(align >= sizeof(void *) && (align & (align - 1)) == 0)) align = 16;
    switch (sub) {
        case 0: p = malloc_s(0); break;
        case 1: p = calloc_s(0, 0); break;
        case 2: p = calloc_s(0, 123); break;
        case 3: p = calloc_s(123, 0); break;
        case 4: p = aligned_alloc_s(align, 0); break;
    }
    if (!p) { DET("SELFERR: zero-size allocation sub=%u returned NULL", sub); return V_SELFERR; }
    if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK || m.byte_size != 1 ||
        !m.is_live) {
        DET("CORRUPT: zero-size block lacks defined 1-byte tracked metadata");
        return V_CORRUPT;
    }
    if (sub >= 1 && sub <= 3) {
        const unsigned char *b = (const unsigned char *)p;
        if (b[0] != 0) { DET("CORRUPT: calloc zero-size block not zeroed"); return V_CORRUPT; }
    }
    free_s(p);
    DET("zero-size sub=%u produced defined 1-byte block", sub);
    return V_CLEAN;
}

static int f_align_bad(const Case *c) {
    size_t align = (size_t)c->p[0], sz = (size_t)c->p[1] ? (size_t)c->p[1] : 32;
    void *p = aligned_alloc_s(align, sz);
    if (p != NULL) {
        DET("UNEXPECTED_OK: aligned_alloc_s(align=0x%zx,sz=%zu) returned <ptr>", align, sz);
        return V_UNEXPECTED_OK;
    }
    DET("aligned_alloc_s(align=0x%zx) refused errno=%d", align, errno);
    return V_CLEAN;
}

static int f_align_ok(const Case *c) {
    size_t align = (size_t)c->p[0], sz = (size_t)c->p[1] ? (size_t)c->p[1] : 32;
    void *p = aligned_alloc_s(align, sz);
    if (!p) { DET("SELFERR: aligned_alloc_s(0x%zx,%zu) failed", align, sz); return V_SELFERR; }
    if (((uintptr_t)p & (uintptr_t)(align - 1)) != 0) {
        DET("CORRUPT: aligned_alloc_s(%zu) returned misaligned <ptr>", align);
        return V_CORRUPT;
    }
    fill_pattern(p, sz, 0x5C);
    if (cplus_safe_ptr_size(p) != sz) {
        DET("CORRUPT: aligned block metadata size wrong");
        return V_CORRUPT;
    }
    free_s(p);
    DET("aligned_alloc_s(%zu,%zu) honoured (<ptr>)", align, sz);
    return V_CLEAN;
}

static int f_double_free(const Case *c) {
    size_t sz = (size_t)c->p[0] ? (size_t)c->p[0] : 64;
    unsigned sub = (unsigned)(c->p[1] % 3);
    void *p = malloc_s(sz);
    if (!p) { DET("SELFERR: setup malloc failed"); return V_SELFERR; }
    fill_pattern(p, sz, 0x33);
    if (sub == 0) {
        int r1 = free_checked_s(p);      /* first free: OK */
        int r2 = free_checked_s(p);      /* second free: must refuse */
        if (r1 != CPLUS_SAFE_OK || r2 == CPLUS_SAFE_OK) {
            DET("UNEXPECTED_OK: free_checked double-free r1=%d r2=%d", r1, r2);
            return V_UNEXPECTED_OK;
        }
    } else if (sub == 1) {
        free_s(p);                        /* first free via void API */
        int r2 = free_checked_s(p);
        if (r2 == CPLUS_SAFE_OK) {
            DET("UNEXPECTED_OK: double free after free_s accepted");
            return V_UNEXPECTED_OK;
        }
    } else {
        free_s(p);
        void *q = realloc_s(p, sz * 2);   /* realloc of freed ptr: must refuse */
        if (q != NULL) {
            DET("UNEXPECTED_OK: realloc_s on freed pointer returned <ptr>");
            return V_UNEXPECTED_OK;
        }
    }
    if (cplus_safe_ptr_is_live(p) || !cplus_safe_ptr_was_freed(p)) {
        DET("CORRUPT: freed block still reported live / not reported freed");
        return V_CORRUPT;
    }
    DET("double-free sub=%u refused", sub);
    return V_CLEAN;
}

static int f_invalid_ptr(const Case *c) {
    unsigned kind = (unsigned)(c->p[0] % 8);
    size_t sz = (size_t)c->p[1] ? (size_t)c->p[1] : 64;
    void *p = malloc_s(sz);
    volatile int local = 7;
    void *forged = NULL;
    if (!p) { DET("SELFERR: setup malloc failed"); return V_SELFERR; }
    switch (kind) {
        case 0: forged = (void *)(uintptr_t)1; break;         /* TOMB sentinel */
        case 1: forged = (void *)(uintptr_t)0xDEADBEEFULL; break;
        case 2: forged = (void *)&local; break;               /* stack address */
        case 3: forged = (void *)&g_static_canary; break;     /* static address */
        case 4: forged = (void *)((unsigned char *)p + 1); break; /* interior */
        case 5: forged = (void *)((unsigned char *)p + sz); break; /* one-past */
        case 6: forged = (void *)(uintptr_t)SIZE_MAX; break;  /* huge */
        case 7: forged = (void *)((unsigned char *)p + (sz > 1 ? sz - 1 : 1)); break;
    }
    /* Avoid false positives: skip if the forged value happens to be a live base. */
    if (cplus_safe_ptr_is_live(forged)) {
        DET("REFUSED(skip): forged value used by a live block");
        free_s(p);
        return V_CLEAN;
    }
    int rc = free_checked_s(forged);
    if (rc == CPLUS_SAFE_OK) {
        DET("UNEXPECTED_OK: free_checked_s accepted forged pointer kind=%u <ptr>", kind);
        return V_UNEXPECTED_OK;
    }
    void *q = realloc_s(forged, 8);
    if (q != NULL) {
        DET("UNEXPECTED_OK: realloc_s accepted forged pointer kind=%u", kind);
        return V_UNEXPECTED_OK;
    }
    /* The real block must be untouched. */
    if (!cplus_safe_ptr_is_live(p) || cplus_safe_ptr_size(p) != sz) {
        DET("CORRUPT: forged-pointer operation disturbed a real live block");
        return V_CORRUPT;
    }
    free_s(p);
    DET("invalid pointer kind=%u (<ptr>) refused", kind);
    return V_CLEAN;
}

static int f_realloc_atomic(const Case *c) {
    unsigned sub = (unsigned)(c->p[0] % 4);
    size_t sz = (size_t)c->p[1] ? (size_t)c->p[1] : 64;
    void *p = malloc_s(sz);
    void *guard = malloc_s(64);
    void *q;
    if (!p || !guard) { DET("SELFERR: setup failed"); return V_SELFERR; }
    fill_pattern(p, sz, 0x77);
    fill_pattern(guard, 64, 0x88);
    if (sub == 0) {
        cplus_safe_alloc_fail_after(0);       /* next allocation must fail */
        q = realloc_s(p, sz + 128);
        cplus_safe_alloc_fail_reset();
    } else if (sub == 1) {
        q = realloc_s(p, RSIZE_MAX_LOCAL + 1); /* invalid size */
    } else if (sub == 2) {
        q = realloc_s((void *)(uintptr_t)0xDEADBEEFULL, sz + 8); /* invalid ptr */
    } else {
        cplus_safe_alloc_fail_after(1);       /* first realloc succeeds, second fails */
        void *r = realloc_s(p, sz + 16);
        if (!r) { DET("SELFERR: first realloc unexpectedly failed"); return V_SELFERR; }
        p = r; sz += 16; fill_pattern(p, sz, 0x77);
        q = realloc_s(p, sz + 128);
        cplus_safe_alloc_fail_reset();
    }
    if (q != NULL) {
        DET("UNEXPECTED_OK: realloc_s(sub=%u) returned <ptr> though failure expected", sub);
        return V_UNEXPECTED_OK;
    }
    if (!cplus_safe_ptr_is_live(p) || cplus_safe_ptr_size(p) != sz ||
        !check_pattern(p, sz, 0x77) || !check_pattern(guard, 64, 0x88)) {
        DET("CORRUPT: realloc_s(sub=%u) lost atomicity / corrupted neighbour", sub);
        return V_CORRUPT;
    }
    free_s(p); free_s(guard);
    DET("realloc failure sub=%u preserved original bytes and neighbour", sub);
    return V_CLEAN;
}

static int f_meta_consistency(const Case *c) {
    unsigned sub = (unsigned)(c->p[0] % 4);
    size_t sz = (size_t)c->p[1] ? (size_t)c->p[1] : 32;
    size_t cnt = (size_t)c->p[2] ? (size_t)c->p[2] : 4;
    size_t esz = (size_t)c->p[3] ? (size_t)c->p[3] : 4;
    cplus_safe_alloc_meta m;
    void *p = NULL, *q;
    size_t exp_bytes = 0, exp_elem = 1, exp_count = 0;
    if (sub == 0) {
        p = malloc_s(sz); exp_bytes = sz; exp_elem = 1; exp_count = sz;
    } else if (sub == 1) {
        p = calloc_s(cnt, esz); exp_bytes = cnt * esz; exp_elem = esz; exp_count = cnt;
    } else if (sub == 2) {
        p = aligned_alloc_s(16, sz); exp_bytes = sz; exp_elem = 1; exp_count = sz;
    } else {
        p = malloc_s(sz);
        if (!p) { DET("SELFERR: setup failed"); return V_SELFERR; }
        memset(&m, 0, sizeof m);
        cplus_safe_alloc_get_meta(p, &m);
        unsigned long long oldgen = m.generation;
        q = realloc_s(p, sz * 2);
        if (!q) { DET("SELFERR: realloc failed"); return V_SELFERR; }
        p = q; sz *= 2; exp_bytes = sz; exp_elem = 1; exp_count = sz;
        if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK ||
            m.generation <= oldgen) {
            DET("CORRUPT: realloc did not bump generation old=%llu new=%llu",
                oldgen, m.generation);
            return V_CORRUPT;
        }
    }
    if (!p) { DET("SELFERR: allocation failed sub=%u", sub); return V_SELFERR; }
    memset(&m, 0xEE, sizeof m);
    if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK || !m.is_live ||
        m.byte_size != exp_bytes || m.elem_size != exp_elem ||
        m.elem_count != exp_count || m.generation == 0) {
        DET("CORRUPT: meta mismatch sub=%u bytes=%zu/%zu elem=%zu/%zu cnt=%zu/%zu",
            sub, m.byte_size, exp_bytes, m.elem_size, exp_elem, m.elem_count, exp_count);
        return V_CORRUPT;
    }
    if (cplus_safe_ptr_size(p) != exp_bytes ||
        cplus_safe_ptr_elem_size(p) != exp_elem ||
        cplus_safe_ptr_count(p) != exp_count) {
        DET("CORRUPT: scalar metadata query mismatch sub=%u", sub);
        return V_CORRUPT;
    }
    {
        size_t out = 12345;
        if (cplus_safe_query_length(p, 0, &out) != CPLUS_SAFE_OK || out != exp_count) {
            DET("CORRUPT: query_length mismatch sub=%u out=%zu exp=%zu", sub, out, exp_count);
            return V_CORRUPT;
        }
    }
    free_s(p);
    DET("metadata consistent sub=%u bytes=%zu elem=%zu cnt=%zu", sub, exp_bytes, exp_elem, exp_count);
    return V_CLEAN;
}

static int f_query_null(const Case *c) {
    unsigned sub = (unsigned)(c->p[0] % 6);
    void *p = malloc_s(16);
    cplus_safe_alloc_meta m;
    size_t out = 12345;
    int rc = 0, bad = 0;
    if (!p) { DET("SELFERR: setup failed"); return V_SELFERR; }
    switch (sub) {
        case 0: rc = cplus_safe_alloc_get_meta(p, NULL); bad = (rc != CPLUS_SAFE_EINVAL); break;
        case 1: rc = cplus_safe_alloc_get_meta(NULL, &m); bad = (rc != CPLUS_SAFE_EINVAL); break;
        case 2: rc = cplus_safe_query_length(p, 4, NULL); bad = (rc != CPLUS_SAFE_EINVAL); break;
        case 3: rc = cplus_safe_query_length(NULL, 4, &out); bad = (rc != CPLUS_SAFE_EINVAL); break;
        case 4: cplus_safe_ptr_size(NULL); cplus_safe_ptr_is_live(NULL);
                cplus_safe_ptr_was_freed(NULL); cplus_safe_ptr_check_generation(NULL, 1);
                bad = 0; break;
        case 5: bad = (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK ||
                       cplus_safe_ptr_check_generation(p, m.generation) != 1); break;
    }
    if (bad) {
        DET("UNEXPECTED_OK: query_null sub=%u returned rc=%d", sub, rc);
        return V_UNEXPECTED_OK;
    }
    free_s(p);
    DET("null/out-param query sub=%u handled", sub);
    return V_CLEAN;
}

static int f_cycles(const Case *c) {
    unsigned long n = (unsigned long)c->p[0];
    size_t base = (size_t)c->p[1] ? (size_t)c->p[1] : 32;
    if (n > 2000) n = 2000;
    for (unsigned long i = 0; i < n; ++i) {
        void *p;
        size_t sz = base + (i % 7);
        switch (i % 3) {
            case 0: p = malloc_s(sz); break;
            case 1: p = calloc_s((sz / 4) + 1, 4); sz = ((sz / 4) + 1) * 4; break;
            default: p = aligned_alloc_s(16, sz); break;
        }
        if (!p) { DET("SELFERR: cycle %lu allocation failed", i); return V_SELFERR; }
        fill_pattern(p, sz, (unsigned char)(i & 0xFF));
        if (!cplus_safe_ptr_is_live(p) || cplus_safe_ptr_size(p) != sz ||
            !check_pattern(p, sz, (unsigned char)(i & 0xFF))) {
            DET("CORRUPT: cycle %lu block not tracked correctly", i);
            return V_CORRUPT;
        }
        free_s(p);
        if (cplus_safe_ptr_is_live(p)) {
            DET("CORRUPT: cycle %lu block still live after free", i);
            return V_CORRUPT;
        }
        if (free_checked_s(p) == CPLUS_SAFE_OK) {
            DET("UNEXPECTED_OK: cycle %lu double free accepted", i);
            return V_UNEXPECTED_OK;
        }
    }
    if (cplus_safe_alloc_live_count() != 0 || cplus_safe_alloc_live_bytes() != 0) {
        DET("CORRUPT: after %lu cycles live_count=%zu live_bytes=%zu", n,
            cplus_safe_alloc_live_count(), cplus_safe_alloc_live_bytes());
        return V_CORRUPT;
    }
    DET("%lu alloc/free cycles, live_count=0 (recent cache exercised)", n);
    return V_CLEAN;
}

static int f_stale_gen(const Case *c) {
    size_t sz = (size_t)c->p[0] ? (size_t)c->p[0] : 32;
    size_t newsz = (size_t)c->p[1] ? (size_t)c->p[1] : 64;
    cplus_safe_alloc_meta m1, m2;
    void *p = calloc_s(4, 8);
    void *q;
    if (!p) { DET("SELFERR: setup failed"); return V_SELFERR; }
    if (cplus_safe_alloc_get_meta(p, &m1) != CPLUS_SAFE_OK) { DET("SELFERR: meta failed"); return V_SELFERR; }
    free_s(p);
    q = malloc_s(newsz);
    if (!q) { DET("SELFERR: realloc setup failed"); return V_SELFERR; }
    if ((uintptr_t)q == (uintptr_t)p) {
        if (cplus_safe_alloc_get_meta(q, &m2) != CPLUS_SAFE_OK || m2.generation == m1.generation) {
            DET("CORRUPT: recycled address did not bump generation");
            return V_CORRUPT;
        }
        if (cplus_safe_ptr_check_generation(p, m1.generation) != 0) {
            DET("CORRUPT: stale generation still matched after address reuse");
            return V_CORRUPT;
        }
        DET("address reused; stale generation correctly rejected");
    } else {
        if (cplus_safe_ptr_check_generation(p, m1.generation) != 0) {
            DET("CORRUPT: freed pointer generation still matched");
            return V_CORRUPT;
        }
        DET("address not reused; freed generation rejected (sz=%zu/%zu)", sz, newsz);
    }
    free_s(q);
    return V_CLEAN;
}

static int f_inject_all(const Case *c) {
    long k = (long)c->p[0];
    void *q;
    if (k < 0 || k > 100) k = 0;
    cplus_safe_alloc_fail_reset();
    cplus_safe_alloc_fail_after(k);
    for (long i = 0; i < k; ++i) {
        q = malloc_s(8);
        if (!q) {
            DET("UNEXPECTED_OK: allocation %ld of %ld failed too early", i + 1, k);
            cplus_safe_alloc_fail_reset();
            return V_UNEXPECTED_OK;
        }
    }
    q = malloc_s(8);
    if (q != NULL) {
        DET("UNEXPECTED_OK: allocation after fail_after(%ld) unexpectedly succeeded", k);
        cplus_safe_alloc_fail_reset();
        return V_UNEXPECTED_OK;
    }
    cplus_safe_alloc_fail_reset();
    /* Other allocators must honour injection too. */
    cplus_safe_alloc_fail_after(0);
    if (calloc_s(4, 4) != NULL || aligned_alloc_s(16, 8) != NULL ||
        realloc_s(NULL, 8) != NULL) {
        DET("UNEXPECTED_OK: injection ignored by calloc/aligned/realloc(NULL)");
        cplus_safe_alloc_fail_reset();
        return V_UNEXPECTED_OK;
    }
    cplus_safe_alloc_fail_reset();
    q = malloc_s(8);
    if (!q) { DET("CORRUPT: fail_reset left allocator in failing state"); return V_CORRUPT; }
    free_s(q);
    DET("fail_after(%ld): exactly %ld successes then failure; reset works", k, k);
    return V_CLEAN;
}

static int f_interior_cap(const Case *c) {
    size_t sz = (size_t)c->p[0] ? (size_t)c->p[0] : 64;
    void *p = malloc_s(sz);
    if (!p) { DET("SELFERR: setup failed"); return V_SELFERR; }
    struct { const void *ptr; size_t exp; const char *what; } t[] = {
        { p, sz, "base" },
        { (const void *)((unsigned char *)p + sz / 2), sz - sz / 2, "interior" },
        { (const void *)((unsigned char *)p + sz), 0, "one-past-end" },
        { (const void *)((unsigned char *)p + sz + 1), (size_t)-1, "beyond" },
        { (const void *)((unsigned char *)p - 1), (size_t)-1, "before" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; ++i) {
        size_t got = cplus_safe_remaining_capacity(t[i].ptr);
        if (got != t[i].exp) {
            DET("CORRUPT: remaining_capacity(%s)=%zu expected %zu", t[i].what, got, t[i].exp);
            return V_CORRUPT;
        }
    }
    free_s(p);
    DET("remaining_capacity interior/edge queries correct (sz=%zu)", sz);
    return V_CLEAN;
}

static int f_forged_batch(const Case *c) {
    unsigned long cnt = (unsigned long)c->p[0];
    void *p = malloc_s(64);
    volatile int local = 11;
    if (!p) { DET("SELFERR: setup failed"); return V_SELFERR; }
    if (cnt > 300) cnt = 300;
    for (unsigned long i = 0; i < cnt; ++i) {
        void *f;
        switch (i % 8) {
            case 0: f = (void *)(uintptr_t)(i + 1); break;
            case 1: f = (void *)(uintptr_t)(0x1000 + i * 8); break;
            case 2: f = (void *)&local; break;
            case 3: f = (void *)&g_static_canary; break;
            case 4: f = (void *)((unsigned char *)p + 1 + (i % 63)); break;
            case 5: f = (void *)(uintptr_t)(SIZE_MAX - i); break;
            case 6: f = (void *)(uintptr_t)0xDEADBEEF0000ULL + i; break;
            default: f = (void *)((unsigned char *)p + 64 + i); break;
        }
        if (cplus_safe_ptr_is_live(f)) continue; /* avoid false positive */
        if (free_checked_s(f) == CPLUS_SAFE_OK) {
            DET("UNEXPECTED_OK: forged batch pointer <ptr> (i=%lu) was freed", i);
            return V_UNEXPECTED_OK;
        }
        if (realloc_s(f, 8) != NULL) {
            DET("UNEXPECTED_OK: forged batch pointer <ptr> (i=%lu) was realloc'd", i);
            return V_UNEXPECTED_OK;
        }
    }
    if (!cplus_safe_ptr_is_live(p) || cplus_safe_ptr_size(p) != 64) {
        DET("CORRUPT: forged batch disturbed the real live block");
        return V_CORRUPT;
    }
    free_s(p);
    DET("%lu fabricated pointers all refused", cnt);
    return V_CLEAN;
}

static int f_mixed(const Case *c) {
    unsigned long nops = (unsigned long)c->p[0];
    uint64_t s = c->seed ^ c->p[1];
    if (nops > 500) nops = 500;
    void *slots[48];
    size_t sizes[48];
    unsigned char pat[48];
    memset(slots, 0, sizeof slots);
    memset(sizes, 0, sizeof sizes);
    memset(pat, 0, sizeof pat);
    for (unsigned long op = 0; op < nops; ++op) {
        uint64_t r = mix64(s += 0x9E3779B97F4A7C15ULL);
        unsigned choice = (unsigned)(r & 3);
        int idx = (int)((r >> 8) % 48);
        if (choice <= 1) {                 /* allocate into a free slot */
            if (slots[idx] != NULL) continue;
            size_t sz = 8 + ((r >> 16) % 128);
            void *p = (choice == 0) ? malloc_s(sz)
                                    : calloc_s((sz / 4) + 1, 4);
            if (choice == 1) sz = ((sz / 4) + 1) * 4;
            if (!p) { DET("SELFERR: mixed alloc failed op=%lu", op); goto cleanup; }
            slots[idx] = p; sizes[idx] = sz; pat[idx] = (unsigned char)(r >> 40);
            fill_pattern(p, sz, pat[idx]);
        } else if (choice == 2) {          /* free an occupied slot + double-free */
            if (!slots[idx]) continue;
            if (free_checked_s(slots[idx]) != CPLUS_SAFE_OK) {
                DET("CORRUPT: mixed free of tracked slot failed op=%lu", op);
                return V_CORRUPT;
            }
            if (free_checked_s(slots[idx]) == CPLUS_SAFE_OK) {
                DET("UNEXPECTED_OK: mixed double-free accepted op=%lu", op);
                return V_UNEXPECTED_OK;
            }
            slots[idx] = NULL;
        } else {                           /* verify metadata + pattern */
            if (!slots[idx]) continue;
            if (!cplus_safe_ptr_is_live(slots[idx]) ||
                cplus_safe_ptr_size(slots[idx]) != sizes[idx] ||
                !check_pattern(slots[idx], sizes[idx], pat[idx])) {
                DET("CORRUPT: mixed invariant broken op=%lu idx=%d", op, idx);
                return V_CORRUPT;
            }
        }
    }
    /* Final reconciliation against the allocator's own counters. */
    {
        size_t expected = 0;
        for (int i = 0; i < 48; ++i) {
            if (!slots[i]) continue;
            ++expected;
            if (!cplus_safe_ptr_is_live(slots[i]) ||
                !check_pattern(slots[i], sizes[i], pat[i])) {
                DET("CORRUPT: final mixed invariant broken idx=%d", i);
                return V_CORRUPT;
            }
        }
        if (cplus_safe_alloc_live_count() != expected) {
            DET("CORRUPT: final live_count=%zu expected=%zu",
                cplus_safe_alloc_live_count(), expected);
            return V_CORRUPT;
        }
        DET("mixed %lu ops, %zu live blocks reconciled", nops, expected);
    }
cleanup:
    for (int i = 0; i < 48; ++i) if (slots[i]) free_s(slots[i]);
    return V_CLEAN;
}

static int f_realloc_elem(const Case *c) {
    size_t count = (size_t)c->p[0] ? (size_t)c->p[0] : 4;
    size_t esz = (size_t)c->p[1] ? (size_t)c->p[1] : 4;
    size_t newsz = (size_t)c->p[2] ? (size_t)c->p[2] : 32;
    cplus_safe_alloc_meta m;
    void *p = calloc_s(count, esz);
    void *q;
    if (!p) { DET("SELFERR: calloc setup failed"); return V_SELFERR; }
    if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK || m.elem_size != esz) {
        DET("SELFERR: initial elem_size wrong %zu", m.elem_size); return V_SELFERR;
    }
    q = realloc_s(p, newsz);
    if (!q) { DET("SELFERR: realloc failed"); return V_SELFERR; }
    p = q;
    if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) {
        DET("CORRUPT: meta after realloc failed"); return V_CORRUPT;
    }
    if (m.elem_size != esz || m.byte_size != newsz || m.elem_count != newsz / esz ||
        cplus_safe_ptr_elem_size(p) != esz || cplus_safe_ptr_count(p) != newsz / esz) {
        DET("CORRUPT: realloc did not preserve elem_size esz=%zu got=%zu cnt=%zu/%zu",
            esz, m.elem_size, m.elem_count, newsz / esz);
        return V_CORRUPT;
    }
    free_s(p);
    DET("realloc preserved elem_size=%zu bytes=%zu count=%zu", esz, newsz, newsz / esz);
    return V_CLEAN;
}

static int f_cache_exhaust(const Case *c) {
    unsigned long n = (unsigned long)c->p[0];
    if (n > 1800) n = 1800;
    void **v = (void **)malloc(n * sizeof(void *));
    size_t *sz = (size_t *)malloc(n * sizeof(size_t));
    unsigned long i;
    if (!v || !sz) { free(v); free(sz); DET("SELFERR: setup OOM"); return V_SELFERR; }
    /* Keep >1024 live at once: forces the 1024-entry recent cache to overflow
     * when they are all subsequently freed. */
    for (i = 0; i < n; ++i) {
        sz[i] = 16 + (i % 5) * 8;
        v[i] = malloc_s(sz[i]);
        if (!v[i]) { DET("SELFERR: bulk alloc %lu failed", i); goto done; }
        fill_pattern(v[i], sz[i], (unsigned char)(i & 0xFF));
    }
    for (i = 0; i < n; ++i) {
        if (cplus_safe_ptr_size(v[i]) != sz[i] ||
            !check_pattern(v[i], sz[i], (unsigned char)(i & 0xFF))) {
            DET("CORRUPT: bulk block %lu metadata/pattern wrong", i); goto done;
        }
        if (free_checked_s(v[i]) != CPLUS_SAFE_OK) {
            DET("CORRUPT: bulk free of live block %lu failed", i); goto done;
        }
        if (free_checked_s(v[i]) == CPLUS_SAFE_OK) {
            DET("UNEXPECTED_OK: double free of block %lu accepted (recent cache)", i);
            goto done;
        }
    }
    if (cplus_safe_alloc_live_count() != 0) {
        DET("CORRUPT: after bulk free live_count=%zu", cplus_safe_alloc_live_count());
        goto done;
    }
    DET("%lu distinct blocks freed; no double-free accepted", n);
done:
    /* Free any still-live blocks from an aborted run. */
    for (i = 0; i < n; ++i) if (v[i] && cplus_safe_ptr_is_live(v[i])) free_s(v[i]);
    free(v); free(sz);
    return V_CLEAN;
}

static int f_hardened(const Case *c) {
    unsigned sub = (unsigned)(c->p[0] % 5);
    size_t sz = (size_t)c->p[1] ? (size_t)c->p[1] : 64;
    void *p = cplus_malloc(sz);
    void *q;
    if (!p) { DET("SELFERR: cplus_malloc setup failed"); return V_SELFERR; }
    fill_pattern(p, sz, 0x66);
    if (sub == 0) {
        q = cplus_realloc(p, RSIZE_MAX_LOCAL + 1);
        /* hardened libc has no RSIZE cap; a huge realloc simply OOMs, which
         * is still a refusal. Accept only NULL, and verify atomicity. */
        if (q != NULL) { DET("UNEXPECTED_OK: cplus_realloc huge returned <ptr>"); return V_UNEXPECTED_OK; }
    } else if (sub == 1) {
        cplus_alloc_fail_after(0);
        q = cplus_realloc(p, sz + 64);
        cplus_alloc_fail_reset();
        if (q != NULL) { DET("UNEXPECTED_OK: cplus_realloc ignored injection <ptr>"); return V_UNEXPECTED_OK; }
    } else if (sub == 2) {
        cplus_free(p);
        if (cplus_alloc_is_live(p)) { DET("CORRUPT: cplus_free left block live"); return V_CORRUPT; }
        void *tmp = p;
        if (cplus_free_checked(&tmp) != EINVAL) {
            DET("UNEXPECTED_OK: cplus double free accepted"); return V_UNEXPECTED_OK;
        }
        DET("hardened double-free refused (second free EINVAL)");
        return V_CLEAN; /* pattern already freed; nothing more to check */
    } else if (sub == 3) {
        q = cplus_realloc((void *)(uintptr_t)0xDEADBEEFULL, sz);
        if (q != NULL) { DET("UNEXPECTED_OK: cplus_realloc accepted forged ptr"); return V_UNEXPECTED_OK; }
    } else {
        q = cplus_malloc(0);
        if (!q || cplus_alloc_size(q) != 1) {
            DET("CORRUPT: cplus_malloc(0) lacks 1-byte tracking"); return V_CORRUPT;
        }
        cplus_free(q);
    }
    if (!cplus_alloc_is_live(p) || cplus_alloc_size(p) != sz ||
        !check_pattern(p, sz, 0x66)) {
        DET("CORRUPT: cplus_realloc failure lost atomicity"); return V_CORRUPT;
    }
    cplus_free(p);
    DET("hardened sub=%u behaved", sub);
    return V_CLEAN;
}

static int run_case(const Case *c) {
    cplus_safe_alloc_fail_reset();
    cplus_alloc_fail_reset();
    DET("start");
    switch (c->fam) {
        case 0:  return f_ovr_malloc(c);
        case 1:  return f_ovr_calloc(c);
        case 2:  return f_ovr_realloc(c);
        case 3:  return f_zero_size(c);
        case 4:  return f_align_bad(c);
        case 5:  return f_align_ok(c);
        case 6:  return f_double_free(c);
        case 7:  return f_invalid_ptr(c);
        case 8:  return f_realloc_atomic(c);
        case 9:  return f_meta_consistency(c);
        case 10: return f_query_null(c);
        case 11: return f_cycles(c);
        case 12: return f_stale_gen(c);
        case 13: return f_inject_all(c);
        case 14: return f_interior_cap(c);
        case 15: return f_forged_batch(c);
        case 16: return f_mixed(c);
        case 17: return f_realloc_elem(c);
        case 18: return f_cache_exhaust(c);
        case 19: return f_hardened(c);
        default: DET("SELFERR: unknown family %d", c->fam); return V_SELFERR;
    }
}

/* ---------- manifest + driver --------------------------------------------- */

static int parse_manifest(const char *path, Case **out, size_t *n) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("open manifest"); return -1; }
    size_t cap = 1024, count = 0;
    Case *arr = (Case *)malloc(cap * sizeof(Case));
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (count == cap) { cap *= 2; arr = (Case *)realloc(arr, cap * sizeof(Case)); }
        Case *c = &arr[count];
        unsigned long long seed, p0, p1, p2, p3;
        int fam; unsigned long idx;
        if (sscanf(line, "%lu %d %llx %llx %llx %llx %llx",
                   &idx, &fam, &seed, &p0, &p1, &p2, &p3) != 7) {
            fprintf(stderr, "bad manifest line: %s", line); continue;
        }
        c->idx = idx; c->fam = fam; c->seed = (uint64_t)seed;
        c->p[0] = (uint64_t)p0; c->p[1] = (uint64_t)p1;
        c->p[2] = (uint64_t)p2; c->p[3] = (uint64_t)p3;
        ++count;
    }
    fclose(f);
    *out = arr; *n = count;
    return 0;
}

int main(int argc, char **argv) {
    const char *manifest = argc > 1 ? argv[1] : "results/cases.manifest";
    const char *logpath = argc > 2 ? argv[2] : "results/scenarios.log";
    Case *cases = NULL;
    size_t n = 0, refused = 0, crashes = 0, corrupt = 0, unexpected = 0, selferr = 0;
    if (parse_manifest(manifest, &cases, &n) != 0) return 2;
    FILE *log = fopen(logpath, "w");
    if (!log) { perror("open log"); return 2; }

    for (size_t i = 0; i < n; ++i) {
        Case *c = &cases[i];
        int pipefd[2];
        if (pipe(pipefd) != 0) { perror("pipe"); return 2; }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 2; }
        if (pid == 0) {
            close(pipefd[0]);
            int v = run_case(c);
            g_detail[sizeof g_detail - 1] = '\0';
            ssize_t ignored = write(pipefd[1], g_detail, strlen(g_detail));
            (void)ignored;
            close(pipefd[1]);
            _exit(v);
        }
        close(pipefd[1]);
        char detail[1024];
        size_t got = 0;
        for (;;) {
            ssize_t r = read(pipefd[0], detail + got, sizeof(detail) - 1 - got);
            if (r <= 0) break;
            got += (size_t)r;
            if (got >= sizeof(detail) - 1) break;
        }
        detail[got] = '\0';
        close(pipefd[0]);
        int status = 0;
        waitpid(pid, &status, 0);

        const char *verdict;
        int sig = 0;
        if (WIFSIGNALED(status)) {
            sig = WTERMSIG(status);
            verdict = "CRASH";
            ++crashes;
        } else {
            int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            if (code == V_CLEAN) { verdict = "REFUSED"; ++refused; }
            else if (code == V_UNEXPECTED_OK) { verdict = "UNEXPECTED_OK"; ++unexpected; }
            else if (code == V_CORRUPT) { verdict = "CORRUPT"; ++corrupt; }
            else { verdict = "SELFERR"; ++selferr; }
        }
        const char *fname = (c->fam >= 0 && c->fam < NFAM) ? g_fam_names[c->fam] : "UNKNOWN";
        fprintf(log,
                "CASE %lu %s seed=%016" PRIx64
                " p=%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64
                " verdict=%s signal=%d detail=%s\n",
                c->idx, fname, c->seed, c->p[0], c->p[1], c->p[2], c->p[3],
                verdict, sig, detail);
    }
    fclose(log);
    free(cases);

    fprintf(stdout, "cases=%zu refused=%zu crashes=%zu corrupt=%zu unexpected_ok=%zu selferr=%zu\n",
            n, refused, crashes, corrupt, unexpected, selferr);
    fprintf(stdout, "REDTEAM alloc %zu %zu %zu %zu %zu\n",
            n, refused, crashes, corrupt, unexpected);
    return 0; /* findings are reported, never hidden */
}
