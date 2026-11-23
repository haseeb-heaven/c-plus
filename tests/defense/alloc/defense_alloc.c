/* Defense-in-depth regression harness for the SafeCRT checked allocator.
 *
 * Build from the repository root (see run.sh in this directory):
 *   $CC -std=c11 -I. -c stdlib/safe/src/safe_alloc.c  ...
 *   $CC -std=c11 -I. tests/defense/alloc/defense_alloc.c ... -pthread
 *
 * The same source compiles before and after hardening.  Two additive feature
 * macros in safe_alloc.h select the post-fix assertions:
 *   CPLUS_SAFE_HAS_TAGGED_RECENT_CACHE  (open-addressed, generation-tagged free cache)
 *   CPLUS_SAFE_HAS_GENERATION_HELPERS   (generation-aware checked free / transfer)
 * When they are absent the harness prints the pre-fix gap marker and verifies
 * the legacy behavior instead of failing, so the before/after output is an
 * honest comparison rather than a rewritten expectation.
 *
 * No freed pointer is ever dereferenced by this harness; double-free probes
 * use saved aliases passed only to the checked APIs.
 */
#define _POSIX_C_SOURCE 200809L
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_stats.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHK(cond, name)                                                        \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            fprintf(stderr, "DEFENSE_ALLOC FAIL %s\n", (name));                \
        }                                                                      \
    } while (0)

static void mark(const char *tag, const char *what) {
    printf("%s %s 1\n", tag, what);
}

static unsigned long stat_fail(const char *api) {
    cplus_safe_stat st;
    if (cplus_safe_stats_get(api, &st)) return st.fail;
    return 0;
}

/* Mirrors the recent-cache index.  Before hardening the cache was a
 * 1024-entry direct-mapped array keyed by a 2-round mix; after hardening it
 * is open-addressed and keyed by a 3-round mix (matching libc/src/stdlib/alloc.c).
 * Mirroring the active hash guarantees the collision probe always exercises a
 * real bucket collision. */
static size_t active_recent_index(const void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
#ifdef CPLUS_SAFE_HAS_TAGGED_RECENT_CACHE
    x *= (uintptr_t)0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
#endif
    return (size_t)x & 1023u;
}

/* ------------------------------------------------------------------------ */
/* 1. Invariants that were already correct: prove they are not regressed.    */
/* ------------------------------------------------------------------------ */
static void test_preserved_invariants(void) {
    size_t base_live = cplus_safe_alloc_live_count();
    size_t base_bytes = cplus_safe_alloc_live_bytes();
    void *p, *r, *z;
    cplus_safe_alloc_meta m;
    size_t n = 0;
    int stack_obj = 7;
    void *bad = &stack_obj;

    errno = 0;
    CHK(malloc_s(SIZE_MAX) == NULL && errno == EINVAL, "malloc_s SIZE_MAX refused EINVAL");
    errno = 0;
    CHK(calloc_s(SIZE_MAX, 2) == NULL && errno == EOVERFLOW, "calloc_s overflow refused EOVERFLOW");
    errno = 0;
    CHK(realloc_s(NULL, RSIZE_MAX_MEM + 1u) == NULL && errno == EINVAL, "realloc_s over-limit refused");
    errno = 0;
    CHK(aligned_alloc_s(0, 64) == NULL && errno == EINVAL, "aligned_alloc_s alignment 0 refused");
    errno = 0;
    CHK(aligned_alloc_s(3, 64) == NULL && errno == EINVAL, "aligned_alloc_s non-power-of-two refused");
    errno = 0;
    CHK(aligned_alloc_s(16, SIZE_MAX) == NULL && errno == EINVAL, "aligned_alloc_s SIZE_MAX refused");

    /* Zero-size normalization: 1-byte tracked block on all three paths. */
    z = malloc_s(0);
    CHK(z != NULL && cplus_safe_ptr_size(z) == 1, "malloc_s(0) 1-byte block");
    free_s(z);
    z = calloc_s(0, 128);
    CHK(z != NULL && cplus_safe_ptr_size(z) == 1 && *(unsigned char *)z == 0,
        "calloc_s(0,128) zeroed 1-byte block");
    free_s(z);
    z = malloc_s(16);
    r = realloc_s(z, 0);
    CHK(r != NULL && cplus_safe_ptr_size(r) == 1, "realloc_s(p,0) 1-byte block");
    free_s(r);

    /* All valid alignment sizes are accepted (posix_memalign contract, more
     * permissive than C11 size%alignment); do NOT harden by refusing them. */
    p = aligned_alloc_s(16, 24);
    CHK(p != NULL && (((uintptr_t)p & 15u) == 0u), "aligned_alloc_s(16,24) still accepted");
    free_s(p);

    /* NULL out-params and invalid pointers are refused without crashing. */
    memset(&m, 0x7f, sizeof(m));
    CHK(cplus_safe_alloc_get_meta(NULL, &m) != CPLUS_SAFE_OK &&
        m.is_live == 0 && m.byte_size == 0 && m.generation == 0,
        "get_meta NULL ptr zeroes out and refuses");
    p = malloc_s(8);
    CHK(cplus_safe_alloc_get_meta(p, NULL) != CPLUS_SAFE_OK, "get_meta NULL out refused");
    n = 123;
    CHK(cplus_safe_query_length(NULL, 1, &n) != CPLUS_SAFE_OK && n == 0,
        "query_length NULL ptr zeroes out and refuses");
    CHK(cplus_safe_query_length(p, 1, NULL) != CPLUS_SAFE_OK, "query_length NULL out refused");
    CHK(cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK && m.is_live == 1,
        "get_meta valid block OK");
    free_s(p);

    /* Invalid free of a stack address is refused, no metadata drift. */
    errno = 0;
    CHK(free_checked_s(bad) == CPLUS_SAFE_EINVAL && errno == EINVAL &&
        bad == &stack_obj, "invalid free REFUSED");
    CHK(cplus_safe_alloc_live_count() == base_live &&
        cplus_safe_alloc_live_bytes() == base_bytes, "preserved: live counters intact");
}

/* ------------------------------------------------------------------------ */
/* 2. Failure injection and realloc atomicity.                               */
/* ------------------------------------------------------------------------ */
static void test_injection_atomicity(void) {
    size_t base_live = cplus_safe_alloc_live_count();
    size_t base_bytes = cplus_safe_alloc_live_bytes();
    void *p = malloc_s(64);
    void *r;
    unsigned char *b;
    size_t i;
    cplus_safe_alloc_meta before, after;
    CHK(p != NULL, "atomicity setup");
    if (!p) return;
    b = (unsigned char *)p;
    memset(b, 0x5A, 64);
    CHK(cplus_safe_alloc_get_meta(p, &before) == CPLUS_SAFE_OK, "atomicity meta before");

    cplus_safe_alloc_fail_after(0);
    errno = 0;
    r = realloc_s(p, 4096);
    CHK(r == NULL && errno == ENOMEM, "injected realloc failure returns NULL/ENOMEM");
    CHK(cplus_safe_ptr_is_live(p) == 1 && cplus_safe_ptr_size(p) == 64,
        "injected realloc failure preserves owner/size");
    for (i = 0; i < 64; ++i) { if (b[i] != 0x5A) break; }
    CHK(i == 64, "injected realloc failure preserves payload");
    cplus_safe_alloc_fail_reset();

    CHK(cplus_safe_alloc_get_meta(p, &after) == CPLUS_SAFE_OK &&
        after.generation == before.generation && after.byte_size == before.byte_size &&
        after.is_live == 1, "injected realloc failure is metadata-atomic");

    /* calloc under injection must not move live counters. */
    {
        cplus_safe_alloc_fail_after(0);
        errno = 0;
        CHK(calloc_s(4, 16) == NULL && errno == ENOMEM, "injected calloc refused");
        cplus_safe_alloc_fail_reset();
        CHK(cplus_safe_alloc_live_count() == base_live + 1 &&
            cplus_safe_alloc_live_bytes() == base_bytes + 64,
            "injected calloc left no accounting drift");
    }
    free_s(p);
    CHK(cplus_safe_alloc_live_count() == base_live &&
        cplus_safe_alloc_live_bytes() == base_bytes, "atomicity counters restored");
}

/* ------------------------------------------------------------------------ */
/* 3. F-06: recent-cache collision must not misclassify a real double free.  */
/* ------------------------------------------------------------------------ */
static void test_recent_collision(void) {
    enum { N = 8192 };
    void **v = (void **)malloc((size_t)N * sizeof(void *));
    int seen[1024];
    int a = -1, b = -1, i;
    unsigned long df0, iv0, dfd, ivd;
    void *ca, *cb, *da;
    int rc;

    CHK(v != NULL, "collision setup");
    if (!v) return;
    for (i = 0; i < 1024; ++i) seen[i] = -1;
    for (i = 0; i < N; ++i) {
        v[i] = malloc_s(24);
        if (!v[i]) { CHK(0, "collision allocation"); goto out; }
        {
            int idx = (int)active_recent_index(v[i]);
            if (seen[idx] >= 0 && a < 0) { a = seen[idx]; b = i; }
            else if (seen[idx] < 0) seen[idx] = i;
        }
    }
    CHK(a >= 0, "found deterministic recent-cache collision");
    if (a < 0) goto out;

    df0 = stat_fail("double_free");
    iv0 = stat_fail("invalid_free");
    ca = v[a];
    cb = v[b];
    da = v[a];
    CHK(free_checked_s(ca) == CPLUS_SAFE_OK, "collision free a");
    CHK(free_checked_s(cb) == CPLUS_SAFE_OK, "collision free b (overwrites legacy bucket)");
    errno = 0;
    rc = free_checked_s(da);
    dfd = stat_fail("double_free") - df0;
    ivd = stat_fail("invalid_free") - iv0;
    CHK(rc == CPLUS_SAFE_EINVAL && errno == EINVAL, "collision double free REFUSED (no crash)");
#ifdef CPLUS_SAFE_HAS_TAGGED_RECENT_CACHE
    CHK(dfd == 1 && ivd == 0, "collision double free classified as double_free");
    mark("RECENT_COLLISION", "double_free_classified");
#else
    CHK(ivd == 1 && dfd == 0, "legacy: collision double free misclassified as invalid_free");
    mark("RECENT_COLLISION", "legacy_misclassification");
#endif
    /* v[a] and v[b] are already released; release the rest. */
    for (i = 0; i < N; ++i) {
        if (i == a || i == b) continue;
        (void)free_checked_s(v[i]);
    }
out:
    free(v);
}

/* ------------------------------------------------------------------------ */
/* 4. F-01: stale alias at a reissued address must be refusable explicitly.  */
/* ------------------------------------------------------------------------ */
static void test_stale_alias(void) {
    void *p = malloc_s(64);
    void *q = NULL;
    cplus_safe_alloc_meta m0, m1;
    unsigned long long g0;
    size_t l0 = cplus_safe_alloc_live_count();
    size_t b0 = cplus_safe_alloc_live_bytes();
    int attempt;
    CHK(p != NULL, "stale setup");
    if (!p) return;
    CHK(cplus_safe_alloc_get_meta(p, &m0) == CPLUS_SAFE_OK && m0.generation != 0,
        "stale original generation captured");
    g0 = m0.generation;
    CHK(free_checked_s(p) == CPLUS_SAFE_OK, "stale original released");
    for (attempt = 0; attempt < 8 && q == NULL; ++attempt) q = malloc_s(64);
    CHK(q != NULL, "reissued allocation");
    if (!q) return;
    if (q != p) {
        mark("STALE_ALIAS", "address_reuse_not_deterministic_SKIP");
        free_s(q);
        return;
    }
    CHK(cplus_safe_alloc_get_meta(q, &m1) == CPLUS_SAFE_OK && m1.generation != g0,
        "reissued block has a different generation");
    CHK(cplus_safe_ptr_check_generation(p, g0) == 0,
        "stale generation rejected by check_generation");

#ifdef CPLUS_SAFE_HAS_GENERATION_HELPERS
    {
        int rc;
        errno = 0;
        rc = free_checked_generation_s(p, g0);
        CHK(rc == CPLUS_SAFE_EINVAL && errno == EINVAL,
            "stale-generation free REFUSED with EINVAL");
        CHK(cplus_safe_ptr_is_live(q) == 1 && cplus_safe_ptr_size(q) == 64,
            "reissued block survives refused stale free");
        CHK(cplus_safe_alloc_live_count() == l0 + 1 &&
            cplus_safe_alloc_live_bytes() == b0 + 64 &&
            cplus_safe_ptr_check_generation(q, m1.generation) == 1,
            "refused stale free is metadata-atomic");
        rc = free_checked_generation_s(q, m1.generation);
        CHK(rc == CPLUS_SAFE_OK && !cplus_safe_ptr_is_live(q),
            "current-generation free succeeds");
        mark("STALE_ALIAS", "generation_free_REFUSED");
    }
#else
    {
        /* Pre-fix behavior: the legacy checked free has no generation to
         * compare and releases the NEW owner through a stale alias. */
        int rc = free_checked_s(p);
        CHK(rc == CPLUS_SAFE_OK && cplus_safe_ptr_is_live(q) == 0,
            "legacy: stale alias freed the reissued block");
        mark("STALE_ALIAS", "legacy_stale_alias_frees_new_owner");
        q = NULL;
    }
#endif
    if (q) free_s(q);
    if (cplus_safe_alloc_live_count() != l0 || cplus_safe_alloc_live_bytes() != b0)
        fprintf(stderr, "DEBUG stale counters: live=%zu/%zu bytes=%zu/%zu q=%p p=%p\n",
                cplus_safe_alloc_live_count(), l0, cplus_safe_alloc_live_bytes(), b0,
                (void *)q, (void *)p);
    CHK(cplus_safe_alloc_live_count() == l0 && cplus_safe_alloc_live_bytes() == b0,
        "stale alias counters restored");
}

/* ------------------------------------------------------------------------ */
/* 5. Generation-aware ownership transfer (additive hardening).              */
/* ------------------------------------------------------------------------ */
static void test_generation_transfer(void) {
    void *src = malloc_s(32);
    void *dst = NULL;
    cplus_safe_alloc_meta m;
    unsigned long long g0;
    CHK(src != NULL, "transfer setup");
    if (!src) return;
    CHK(cplus_safe_alloc_get_meta(src, &m) == CPLUS_SAFE_OK, "transfer meta before");
    g0 = m.generation;
    CHK(cplus_safe_transfer_ownership(&dst, &src) == CPLUS_SAFE_OK &&
        src == NULL && dst != NULL, "legacy transfer still works");
    CHK(cplus_safe_alloc_get_meta(dst, &m) == CPLUS_SAFE_OK && m.generation != g0,
        "legacy transfer bumps generation");
#ifdef CPLUS_SAFE_HAS_GENERATION_HELPERS
    {
        void *dst2 = NULL;
        int rc;
        errno = 0;
        rc = cplus_safe_transfer_ownership_generation(&dst2, &dst, g0);
        CHK(rc == CPLUS_SAFE_EINVAL && errno == EINVAL && dst2 == NULL &&
            dst != NULL, "stale-generation transfer REFUSED, source intact");
        CHK(cplus_safe_alloc_get_meta(dst, &m) == CPLUS_SAFE_OK &&
            m.generation != g0, "refused transfer is metadata-atomic");
        rc = cplus_safe_transfer_ownership_generation(&dst2, &dst, m.generation);
        CHK(rc == CPLUS_SAFE_OK && dst == NULL && dst2 != NULL,
            "current-generation transfer succeeds");
        free_s(dst2);
        mark("GEN_TRANSFER", "stale_transfer_REFUSED");
    }
#else
    {
        void *dst2 = NULL;
        int rc = cplus_safe_transfer_ownership(&dst2, &dst);
        CHK(rc == CPLUS_SAFE_OK && dst == NULL && dst2 != NULL,
            "legacy: transfer has no generation gate");
        free_s(dst2);
        mark("GEN_TRANSFER", "legacy_transfer_generation_blind");
    }
#endif
}

/* ------------------------------------------------------------------------ */
/* 6. Metadata table growth (cache exhaustion path) stays consistent.        */
/* ------------------------------------------------------------------------ */
static void test_table_growth(void) {
    enum { N = 4096 };
    void **v = (void **)malloc((size_t)N * sizeof(void *));
    size_t base_live = cplus_safe_alloc_live_count();
    size_t base_bytes = cplus_safe_alloc_live_bytes();
    size_t expect = base_bytes;
    int i;
    CHK(v != NULL, "growth setup");
    if (!v) return;
    for (i = 0; i < N; ++i) {
        v[i] = malloc_s(8);
        if (!v[i]) { CHK(0, "growth allocation"); goto out; }
        memset(v[i], 0xC3, 8);
        expect += 8;
    }
    CHK(cplus_safe_alloc_live_count() == base_live + N &&
        cplus_safe_alloc_live_bytes() == expect, "growth live accounting exact");
    for (i = 0; i < N; ++i) {
        CHK(cplus_safe_ptr_size(v[i]) == 8 && cplus_safe_ptr_is_live(v[i]) == 1,
            "growth metadata lookup after rehash");
        if (g_failures) break;
    }
    for (i = 0; i < N; ++i) (void)free_checked_s(v[i]);
    CHK(cplus_safe_alloc_live_count() == base_live &&
        cplus_safe_alloc_live_bytes() == base_bytes, "growth counters restored");
out:
    free(v);
}

/* ------------------------------------------------------------------------ */
int main(void) {
    cplus_safe_alloc_fail_reset();
    printf("FEATURES tagged_recent=%d generation_helpers=%d\n",
#ifdef CPLUS_SAFE_HAS_TAGGED_RECENT_CACHE
           1,
#else
           0,
#endif
#ifdef CPLUS_SAFE_HAS_GENERATION_HELPERS
           1
#else
           0
#endif
    );

    test_preserved_invariants();
    test_injection_atomicity();
    test_recent_collision();
    test_stale_alias();
    test_generation_transfer();
    test_table_growth();

    printf("DEFENSE_ALLOC %d %d\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
