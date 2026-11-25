/*
 * Dedicated AddressSanitizer test for the C+ allocation APIs.
 *
 * This test uses ONLY the C+ allocator surfaces -- the plain layer
 * (cplus_malloc/cplus_calloc/cplus_realloc/cplus_free + metadata queries) and
 * the checked layer (malloc_s/calloc_s/realloc_s/free_s/free_checked_s +
 * cplus_safe_* metadata queries).  It never calls raw malloc for the blocks
 * under test and never pokes ASan's shadow memory by hand.
 *
 * Why this works: every C+ allocation delegates to a SEPARATE host malloc
 * block, so ASan's interceptors already place redzones around each block and
 * put freed blocks in quarantine.  The invalid accesses below are therefore
 * genuine heap-buffer-overflow / heap-use-after-free events from ASan's point
 * of view.  No manual shadow-memory poisoning and no sanitizer-internal
 * headers are used anywhere in this file.
 *
 * Usage (one scenario per child process):
 *     test_alloc_asan <1..12>
 *
 * Scenarios 1-4, 6-7, 9-10 contain an invalid access: on an ASan-enabled host
 * the child aborts with a nonzero exit status after printing ASAN_LABEL.
 * Scenarios 5, 8, 11, 12 are valid-only and must exit 0.  Scenarios 6, 7 and 10
 * complete their in-bounds (clean) writes before the labelled invalid access,
 * so the ASan report is attributable to the last ASAN_LABEL printed.
 *
 * Host caveat: on macOS/arm64 Clang's ASan runtime may abort before main()
 * (sanitizer_malloc_mac.inc).  That is a host limitation, not a test bug; the
 * translation unit still compiles cleanly with -fsanitize=address.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "cplus_libc.h"
#include "safe_alloc.h"

/* Volatile sink: prevents the compiler from eliding either the bad read or the
 * bad write as dead.  Writes go through a volatile pointer as well. */
static volatile unsigned char g_sink;

static void label(const char *s) {
    printf("ASAN_LABEL %s\n", s);
    fflush(stdout);
}

static void clean_note(const char *s) {
    printf("ASAN_CLEAN %s\n", s);
    fflush(stdout);
}

static int fail_note(const char *s) {
    printf("ASAN_FAIL %s\n", s);
    fflush(stdout);
    return 1;
}

/* Returned only if an invalid access somehow did NOT trip ASan. */
#define NO_ASAN_DETECTED 90

/* ------------------------------------------------------------------------- */
/* 1. WRITE one byte past a cplus_malloc allocation.                          */
/*    Expected: ASan heap-buffer-overflow WRITE.                              */
/* ------------------------------------------------------------------------- */
static int asan_sc_01_malloc_overflow_write(void) {
    const size_t n = 32;
    void *raw = cplus_malloc(n);
    volatile unsigned char *p;
    size_t i;
    if (!raw) return fail_note("sc01 cplus_malloc(32) returned NULL");
    p = (volatile unsigned char *)raw;
    if (cplus_alloc_size(raw) != n) return fail_note("sc01 wrong metadata size");

    for (i = 0; i < n; ++i) p[i] = (unsigned char)i;
    g_sink = p[n - 1];
    clean_note("sc01 in-bounds writes to cplus_malloc(32) completed");

    label("sc01 EXPECT heap-buffer-overflow WRITE at p[32] (one byte past cplus_malloc(32))");
    p[n] = 0xA5u; /* invalid WRITE */
    return NO_ASAN_DETECTED;
}

/* ------------------------------------------------------------------------- */
/* 2. READ one byte past a cplus_calloc allocation.                           */
/*    Expected: ASan heap-buffer-overflow READ.                               */
/* ------------------------------------------------------------------------- */
static int asan_sc_02_calloc_overflow_read(void) {
    const size_t n = 32; /* 8 * 4 */
    void *raw = cplus_calloc(8, 4);
    volatile unsigned char *p;
    unsigned char acc = 0;
    size_t i;
    if (!raw) return fail_note("sc02 cplus_calloc(8,4) returned NULL");
    p = (volatile unsigned char *)raw;
    if (cplus_alloc_size(raw) != n) return fail_note("sc02 wrong metadata size");

    for (i = 0; i < n; ++i) acc ^= p[i];
    g_sink = acc;
    clean_note("sc02 in-bounds reads of cplus_calloc(8,4) completed");

    label("sc02 EXPECT heap-buffer-overflow READ at p[32] (one byte past cplus_calloc(8,4))");
    g_sink = p[n]; /* invalid READ */
    return NO_ASAN_DETECTED;
}

/* ------------------------------------------------------------------------- */
/* 3. WRITE to a cplus_free'd allocation.                                     */
/*    Expected: ASan heap-use-after-free WRITE.                               */
/* ------------------------------------------------------------------------- */
static int asan_sc_03_use_after_free_write(void) {
    const size_t n = 48;
    void *raw = cplus_malloc(n);
    volatile unsigned char *p;
    size_t i;
    if (!raw) return fail_note("sc03 cplus_malloc(48) returned NULL");
    p = (volatile unsigned char *)raw;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)(i ^ 0x3Cu);

    cplus_free(raw);
    if (cplus_alloc_is_live(raw)) return fail_note("sc03 freed block still marked live");

    label("sc03 EXPECT heap-use-after-free WRITE to cplus_free'd block (p[0])");
    p[0] = 0x77u; /* invalid WRITE after free */
    return NO_ASAN_DETECTED;
}

/* ------------------------------------------------------------------------- */
/* 4. READ from a cplus_free'd allocation.                                    */
/*    Expected: ASan heap-use-after-free READ.                                */
/* ------------------------------------------------------------------------- */
static int asan_sc_04_use_after_free_read(void) {
    const size_t n = 48;
    void *raw = cplus_malloc(n);
    volatile unsigned char *p;
    size_t i;
    if (!raw) return fail_note("sc04 cplus_malloc(48) returned NULL");
    p = (volatile unsigned char *)raw;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)i;

    cplus_free(raw);
    if (cplus_alloc_is_live(raw)) return fail_note("sc04 freed block still marked live");

    label("sc04 EXPECT heap-use-after-free READ from cplus_free'd block (p[7])");
    g_sink = p[7]; /* invalid READ after free */
    return NO_ASAN_DETECTED;
}

/* ------------------------------------------------------------------------- */
/* 5. Reuse a freed allocation: free, cplus_malloc again, valid write.         */
/*    Expected: CLEAN (exit 0).                                               */
/* ------------------------------------------------------------------------- */
static int asan_sc_05_freed_reuse_clean(void) {
    const size_t n = 64;
    void *a = cplus_malloc(n);
    void *b;
    volatile unsigned char *va, *vb;
    size_t i;
    if (!a) return fail_note("sc05 first cplus_malloc returned NULL");
    va = (volatile unsigned char *)a;
    for (i = 0; i < n; ++i) va[i] = 0x11u;

    cplus_free(a);
    b = cplus_malloc(n);
    if (!b) return fail_note("sc05 second cplus_malloc returned NULL");
    if (!cplus_alloc_is_live(b) || cplus_alloc_size(b) != n)
        return fail_note("sc05 reissued block metadata wrong");

    vb = (volatile unsigned char *)b;
    for (i = 0; i < n; ++i) vb[i] = (unsigned char)(i * 3u);
    g_sink = vb[n - 1];
    clean_note("sc05 freed-then-reissued pointer is live, sized correctly, and writable");
    cplus_free(b);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* 6. realloc growth: full new range clean, one byte past new size reported.   */
/*    Expected: clean writes, then ASan heap-buffer-overflow WRITE at q[256].  */
/* ------------------------------------------------------------------------- */
static int asan_sc_06_realloc_growth(void) {
    const size_t old_n = 64, new_n = 256;
    void *p = cplus_malloc(old_n);
    void *q;
    volatile unsigned char *vq;
    size_t i;
    if (!p) return fail_note("sc06 cplus_malloc(64) returned NULL");
    {
        volatile unsigned char *vp = (volatile unsigned char *)p;
        for (i = 0; i < old_n; ++i) vp[i] = 0x22u;
    }

    q = cplus_realloc(p, new_n);
    if (!q) return fail_note("sc06 cplus_realloc(64->256) returned NULL");
    if (cplus_alloc_size(q) != new_n) return fail_note("sc06 grown metadata size wrong");
    vq = (volatile unsigned char *)q;

    for (i = 0; i < new_n; ++i) vq[i] = (unsigned char)(i & 0xFFu);
    g_sink = vq[new_n - 1];
    clean_note("sc06 full 256-byte write after realloc growth is in-bounds");

    label("sc06 EXPECT heap-buffer-overflow WRITE at q[256] (one byte past grown block)");
    vq[new_n] = 0x5Au; /* invalid WRITE */
    return NO_ASAN_DETECTED;
}

/* ------------------------------------------------------------------------- */
/* 7. realloc shrinkage: new range clean, touching old tail is reported.       */
/*    Expected: clean writes, then ASan heap-buffer-overflow WRITE at q[64].   */
/* ------------------------------------------------------------------------- */
static int asan_sc_07_realloc_shrink(void) {
    const size_t old_n = 256, new_n = 64;
    void *p = cplus_malloc(old_n);
    void *q;
    volatile unsigned char *vq;
    size_t i;
    if (!p) return fail_note("sc07 cplus_malloc(256) returned NULL");
    {
        volatile unsigned char *vp = (volatile unsigned char *)p;
        for (i = 0; i < old_n; ++i) vp[i] = 0x33u;
    }

    q = cplus_realloc(p, new_n);
    if (!q) return fail_note("sc07 cplus_realloc(256->64) returned NULL");
    if (cplus_alloc_size(q) != new_n) return fail_note("sc07 shrunk metadata size wrong");
    vq = (volatile unsigned char *)q;

    for (i = 0; i < new_n; ++i) vq[i] = (unsigned char)(i + 1u);
    g_sink = vq[new_n - 1];
    clean_note("sc07 64-byte range after realloc shrink is in-bounds");

    label("sc07 EXPECT heap-buffer-overflow WRITE at q[64] (shrunk-off tail)");
    vq[new_n] = 0x6Bu; /* invalid WRITE into poisoned tail */
    return NO_ASAN_DETECTED;
}

/* ------------------------------------------------------------------------- */
/* 8. realloc failure via failure injection: original block stays valid.       */
/*    Expected: CLEAN (exit 0).                                               */
/* ------------------------------------------------------------------------- */
static int asan_sc_08_realloc_fail_injection_clean(void) {
    const size_t n = 64;
    void *p = cplus_malloc(n);
    void *q;
    volatile unsigned char *vp;
    size_t i;
    if (!p) return fail_note("sc08 cplus_malloc(64) returned NULL");
    vp = (volatile unsigned char *)p;
    for (i = 0; i < n; ++i) vp[i] = 0x44u;

    cplus_alloc_fail_reset();
    cplus_alloc_fail_after(0); /* fail the very next (realloc) attempt */
    q = cplus_realloc(p, 4096u);
    cplus_alloc_fail_reset();

    if (q != NULL) {
        cplus_free(q);
        return fail_note("sc08 injected realloc unexpectedly succeeded");
    }
    if (!cplus_alloc_is_live(p) || cplus_alloc_size(p) != n)
        return fail_note("sc08 original block lost after failed realloc");

    for (i = 0; i < n; ++i) vp[i] = (unsigned char)(i ^ 0x5Au);
    g_sink = vp[n - 1];
    clean_note("sc08 failed realloc preserved the original 64-byte block; write is clean");
    cplus_free(p);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* 9. Two adjacent allocations: overflow A into B must be reported.            */
/*    Expected: ASan heap-buffer-overflow WRITE at a[64] (redzone before B).   */
/* ------------------------------------------------------------------------- */
static int asan_sc_09_adjacent_overflow(void) {
    const size_t n = 64;
    void *a = cplus_malloc(n);
    void *b = cplus_malloc(n);
    volatile unsigned char *va, *vb;
    size_t i;
    if (!a || !b) return fail_note("sc09 adjacent allocations returned NULL");
    va = (volatile unsigned char *)a;
    vb = (volatile unsigned char *)b;
    for (i = 0; i < n; ++i) {
        va[i] = 0x66u;
        vb[i] = 0x99u;
    }
    g_sink = vb[n - 1]; /* keep B demonstrably live and in-bounds */
    clean_note("sc09 both distinct 64-byte host blocks are individually writable");

    label("sc09 EXPECT heap-buffer-overflow WRITE at a[64] into redzone before b (distinct blocks)");
    va[n] = 0x88u; /* must NOT silently land in B */
    return NO_ASAN_DETECTED;
}

/* ------------------------------------------------------------------------- */
/* 10. Zero-size allocation: declared byte clean, byte beyond reported.        */
/*     Expected: clean write to p[0], then ASan heap-buffer-overflow WRITE.    */
/* ------------------------------------------------------------------------- */
static int asan_sc_10_zero_size(void) {
    void *raw = cplus_malloc(0);
    volatile unsigned char *p;
    if (!raw) return fail_note("sc10 cplus_malloc(0) returned NULL");
    p = (volatile unsigned char *)raw;
    if (cplus_alloc_size(raw) != 1u) return fail_note("sc10 zero-size metadata size != 1");

    p[0] = 0x44u;
    g_sink = p[0];
    clean_note("sc10 cplus_malloc(0) yields a usable tracked 1-byte block");

    label("sc10 EXPECT heap-buffer-overflow WRITE at p[1] (past zero-size 1-byte block)");
    p[1] = 0x55u; /* invalid WRITE */
    return NO_ASAN_DETECTED;
}

/* ------------------------------------------------------------------------- */
/* 11. Alignment: cplus_aligned_alloc(64,128) and aligned_alloc_s(64,128).      */
/*     Expected: CLEAN (exit 0).                                              */
/* ------------------------------------------------------------------------- */
static int asan_sc_11_alignment_clean(void) {
    const size_t n = 128;
    void *a = cplus_aligned_alloc(64u, n);
    void *b = aligned_alloc_s(64u, n);
    volatile unsigned char *va, *vb;
    size_t i;
    if (!a || !b) return fail_note("sc11 aligned allocations returned NULL");
    if (((uintptr_t)a & (uintptr_t)63u) != 0u)
        return fail_note("sc11 cplus_aligned_alloc pointer not 64-byte aligned");
    if (((uintptr_t)b & (uintptr_t)63u) != 0u)
        return fail_note("sc11 aligned_alloc_s pointer not 64-byte aligned");
    if (cplus_safe_ptr_size(b) != n) return fail_note("sc11 aligned_alloc_s metadata size wrong");

    va = (volatile unsigned char *)a;
    vb = (volatile unsigned char *)b;
    for (i = 0; i < n; ++i) {
        va[i] = (unsigned char)(i & 0xFFu);
        vb[i] = (unsigned char)(i ^ 0x81u);
    }
    g_sink = va[n - 1];
    g_sink = vb[n - 1];
    clean_note("sc11 both 64-byte-aligned 128-byte blocks are fully writable");

    /* cplus_aligned_alloc delegates to host aligned_alloc: free with host free.
     * aligned_alloc_s is owned by the checked allocator: release with free_s. */
    free(a);
    free_s(b);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* 12. Metadata integrity across alloc/realloc/free cycles.                    */
/*     Expected: CLEAN (exit 0), all metadata and live counters consistent.    */
/* ------------------------------------------------------------------------- */
static int asan_sc_12_metadata_integrity_clean(void) {
    const size_t base_plain = cplus_alloc_live_count();
    const size_t base_safe = cplus_safe_alloc_live_count();
    int cycle;

    for (cycle = 0; cycle < 4; ++cycle) {
        const size_t na = 32u + (size_t)cycle * 16u;
        const size_t nb = 48u + (size_t)cycle * 8u;
        const size_t ga = na + 64u;
        const size_t gb = nb + 32u;
        void *a = cplus_malloc(na);
        void *b = malloc_s(nb);
        cplus_alloc_meta ma;
        cplus_safe_alloc_meta mb;
        unsigned long long old_gen;
        void *a2, *b2;
        volatile unsigned char *va, *vb;
        size_t i;

        if (!a || !b) return fail_note("sc12 allocation failed");
        if (cplus_alloc_get_meta(a, &ma) != 0 || !ma.is_live || ma.byte_size != na ||
            ma.elem_size != 1u || ma.elem_count != na)
            return fail_note("sc12 plain alloc metadata wrong");
        if (cplus_safe_alloc_get_meta(b, &mb) != CPLUS_SAFE_OK || !mb.is_live ||
            mb.byte_size != nb || mb.elem_size != 1u || mb.elem_count != nb ||
            cplus_safe_ptr_size(b) != nb)
            return fail_note("sc12 checked alloc metadata wrong");
        if (cplus_alloc_live_count() != base_plain + 1u ||
            cplus_safe_alloc_live_count() != base_safe + 1u)
            return fail_note("sc12 live count after alloc wrong");

        old_gen = ma.generation;
        a2 = cplus_realloc(a, ga);
        b2 = realloc_s(b, gb);
        if (!a2 || !b2) return fail_note("sc12 realloc failed");
        if (cplus_alloc_get_meta(a2, &ma) != 0 || !ma.is_live || ma.byte_size != ga)
            return fail_note("sc12 plain realloc metadata wrong");
        if (cplus_safe_alloc_get_meta(b2, &mb) != CPLUS_SAFE_OK || !mb.is_live ||
            mb.byte_size != gb || cplus_safe_ptr_size(b2) != gb)
            return fail_note("sc12 checked realloc metadata wrong");
        if (ma.generation == old_gen || !cplus_alloc_check_generation(a2, ma.generation))
            return fail_note("sc12 generation not bumped by realloc");
        if (cplus_alloc_check_generation(a2, old_gen))
            return fail_note("sc12 stale generation still matches");
        if (cplus_alloc_live_count() != base_plain + 1u ||
            cplus_safe_alloc_live_count() != base_safe + 1u)
            return fail_note("sc12 live count after realloc wrong");

        va = (volatile unsigned char *)a2;
        vb = (volatile unsigned char *)b2;
        for (i = 0; i < ga; ++i) va[i] = (unsigned char)(i & 0xFFu);
        for (i = 0; i < gb; ++i) vb[i] = (unsigned char)(i ^ 0x33u);
        g_sink = va[ga - 1];
        g_sink = vb[gb - 1];

        cplus_free(a2);
        free_s(b2);
        if (cplus_alloc_is_live(a2) || cplus_safe_ptr_is_live(b2))
            return fail_note("sc12 freed pointer still live");
        if (cplus_alloc_get_meta(a2, &ma) == 0 || cplus_alloc_size(a2) != 0u)
            return fail_note("sc12 freed plain pointer has false metadata");
        if (cplus_safe_alloc_get_meta(b2, &mb) == CPLUS_SAFE_OK ||
            cplus_safe_ptr_size(b2) != 0u)
            return fail_note("sc12 freed checked pointer has false metadata");
        if (cplus_alloc_live_count() != base_plain ||
            cplus_safe_alloc_live_count() != base_safe)
            return fail_note("sc12 live count did not return to baseline");
    }

    clean_note("sc12 metadata, generations, and live counters consistent across all cycles");
    return 0;
}

/* ------------------------------------------------------------------------- */

static void usage(const char *prog) {
    printf("Usage: %s <scenario 1..12>\n", prog);
    printf("  1 malloc OOB WRITE   2 calloc OOB READ   3 UAF WRITE         4 UAF READ\n");
    printf("  5 freed reuse (clean) 6 realloc growth   7 realloc shrink    8 realloc-fail (clean)\n");
    printf("  9 adjacent overflow  10 malloc(0) OOB    11 alignment (clean) 12 metadata (clean)\n");
}

int main(int argc, char **argv) {
    char *end;
    long sc;

    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }
    errno = 0;
    sc = strtol(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' || sc < 1 || sc > 12) {
        usage(argv[0]);
        return 2;
    }

    switch ((int)sc) {
    case 1: return asan_sc_01_malloc_overflow_write();
    case 2: return asan_sc_02_calloc_overflow_read();
    case 3: return asan_sc_03_use_after_free_write();
    case 4: return asan_sc_04_use_after_free_read();
    case 5: return asan_sc_05_freed_reuse_clean();
    case 6: return asan_sc_06_realloc_growth();
    case 7: return asan_sc_07_realloc_shrink();
    case 8: return asan_sc_08_realloc_fail_injection_clean();
    case 9: return asan_sc_09_adjacent_overflow();
    case 10: return asan_sc_10_zero_size();
    case 11: return asan_sc_11_alignment_clean();
    case 12: return asan_sc_12_metadata_integrity_clean();
    default: break;
    }
    return 2;
}
