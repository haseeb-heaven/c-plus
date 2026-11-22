/*
 * test_corpus_buffer.c -- deterministic adversarial corpus for buffer overflow
 * and off-by-one handling in the C+ hardened libc string/memory interfaces.
 *
 * Standalone C99.  Build/run (from repo root):
 *   sh tests/libc/build_libc.sh >/dev/null
 *   clang -std=c99 -Wall -Wextra -pedantic -O2 -Ilibc -Ilibc/include \
 *         tests/libc/test_corpus_buffer.c build/libc/libcplus_hardened.a \
 *         -lm -pthread -o /tmp/corpus_buffer && /tmp/corpus_buffer
 *
 * SAFETY / COVERAGE NOTES (what is and is not safely testable here)
 * ---------------------------------------------------------------
 *  - This static library exports NO checked "_s" entry points (verified with
 *    `nm -g build/libc/libcplus_hardened.a`: there are no memcpy_s / memset_s /
 *    strncpy_s / strnlen_s / ... symbols).  Therefore `length == size + 1` for
 *    the raw cplus_memcpy / cplus_memset / cplus_memmove families CANNOT be
 *    invoked from this test without the test itself performing an out-of-bounds
 *    write (undefined behaviour in the harness, not a library finding).  Those
 *    cases are reported as NOT TESTED; the exact-size boundary and the bounded
 *    cplus_strnlen / cplus_strncpy / cplus_strlcpy paths are exercised instead.
 *  - `cplus_strncpy(dst, src, SIZE_MAX)` is NOT safely callable: its zero-pad
 *    loop runs until i == SIZE_MAX once src's NUL is reached, i.e. an unbounded
 *    write.  It is reported as NOT TESTED.  The bounded huge-n equivalents
 *    ARE tested: cplus_strncat with SIZE_MAX (stops at src NUL, writes one NUL),
 *    cplus_strnlen with SIZE_MAX (stops at NUL), cplus_memchr with SIZE_MAX
 *    (stops at first match).
 *  - `cplus_memrchr` with n == SIZE_MAX starts scanning at p[n-1] and would
 *    immediately read a wild address; it is reported as NOT TESTED.  The
 *    guarded "one past" memrchr variant uses a valid n == size + 1 instead.
 *
 * Every operation is wrapped in canary checks.  A corrupted pre/post canary
 * prints "FINDING: <category> (a=.. b=.. c=.. cap=..)" with the exact tuple and
 * is counted; the final assert(g_findings == 0) gates the result.
 *
 * VERIFIED RESULT (clang -O2 and -O1 -fsanitize=undefined, test TU and the
 * whole library rebuilt with UBSan): 78581 cases ok, 0 FINDING(s), ~0.4s.
 * No canary corruption, no sanitizer diagnostics.  The 4204 transient
 * "grid-copy" FINDINGs seen during development were a defect in this test's
 * first pattern generator (0x41 + 191 == 0x00 wrapped to an early NUL); the
 * generator now uses a guaranteed-nonzero positional pattern.
 */
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stddef.h>

/* Interfaces under test (declared locally to keep the TU standalone). */
extern void  *cplus_memcpy(void *restrict, const void *restrict, size_t);
extern void  *cplus_memmove(void *, const void *, size_t);
extern void  *cplus_memset(void *, int, size_t);
extern int    cplus_memcmp(const void *, const void *, size_t);
extern void  *cplus_memchr(const void *, int, size_t);
extern void  *cplus_memrchr(const void *, int, size_t);
extern size_t cplus_strnlen(const char *, size_t);
extern char  *cplus_strncpy(char *restrict, const char *restrict, size_t);
extern char  *cplus_strncat(char *restrict, const char *restrict, size_t);

#define LEN 256u
#define GC  16u

/* pre[16] | buf[active cap][near post-canary 16][slack] | post[16].
 * The near post-canary sits immediately after the declared active region so a
 * single-byte off-by-one is detected, including for cap < LEN. */
typedef struct {
    unsigned char pre[GC];
    unsigned char buf[LEN + 2u * GC];
    unsigned char post[GC];
} gbuf;

static unsigned long g_findings = 0;
static unsigned long g_cases = 0;

/* Nonzero positional pattern: never emits 0x00 so a generated source string
 * has exactly the intended length (a wrapping additive pattern would
 * accidentally terminate it early and invalidate the expected copy count). */
static unsigned char pattern_at(unsigned char seed, size_t i) {
    return (unsigned char)(1u + ((unsigned int)seed + i) % 255u);
}

static void fill_pattern(unsigned char *p, size_t n, unsigned char seed) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = pattern_at(seed, i);
}

static void gbuf_init(gbuf *g, size_t cap, unsigned char fill) {
    assert(cap <= LEN + 1u);
    memset(g, 0xCC, sizeof *g);
    memset(g->pre, 0xA5, GC);
    memset(g->post, 0x5A, GC);
    memset(g->buf, fill, cap);
    memset(g->buf + cap, 0x5A, GC); /* near post-canary */
}

static int gbuf_ok(const gbuf *g, size_t cap) {
    size_t i;
    for (i = 0; i < GC; ++i) if (g->pre[i] != 0xA5) return 0;
    for (i = 0; i < GC; ++i) if (g->buf[cap + i] != 0x5A) return 0;
    for (i = 0; i < GC; ++i) if (g->post[i] != 0x5A) return 0;
    return 1;
}

static void gbuf_repair(gbuf *g, size_t cap) {
    memset(g->pre, 0xA5, GC);
    memset(g->buf + cap, 0x5A, GC);
    memset(g->post, 0x5A, GC);
}

static int report_finding(const char *cat, size_t a, size_t b, size_t c,
                          size_t cap) {
    printf("FINDING: %s canary corruption (a=%lu b=%lu c=%lu cap=%lu)\n",
           cat, (unsigned long)a, (unsigned long)b, (unsigned long)c,
           (unsigned long)cap);
    ++g_findings;
    return 1; /* keeps the assert() expression true so the corpus continues */
}

/* assert() is used after EVERY operation; on success gbuf_ok() is true, on
 * failure report_finding() records the exact tuple and we keep scanning. */
#define CHECK_C(g, cap, cat, a, b, c)                                          \
    do {                                                                       \
        assert(gbuf_ok((g), (cap)) ||                                          \
               report_finding((cat), (size_t)(a), (size_t)(b), (size_t)(c),    \
                              (size_t)(cap)));                                  \
        if (!gbuf_ok((g), (cap))) gbuf_repair((g), (cap));                     \
    } while (0)

#define EXPECT(cond, cat, a, b, c)                                             \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FINDING: %s check failed (a=%lu b=%lu c=%lu) [%s]\n",      \
                   (cat), (unsigned long)(a), (unsigned long)(b),              \
                   (unsigned long)(c), #cond);                                  \
            ++g_findings;                                                      \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------ table 1 */
/* >= 700 distinct (length, offset, fill) tuples, looped. */
typedef struct { size_t len, off; unsigned char fill; } tup;
#define NTUP 768u
static tup g_table[NTUP];

static void table_init(void) {
    size_t i;
    for (i = 0; i < NTUP; ++i) {
        unsigned char f;
        g_table[i].len = (i % LEN) + 1u;     /* 1..256 */
        g_table[i].off = i / LEN;            /* 0,1,2 -- distinct with len */
        f = (unsigned char)(0x11u + ((i * 61u + 7u) % 0x60u)); /* 0x11..0x70 */
        if (f == 0x5A) f = 0x5B;             /* keep post-canary value distinct */
        g_table[i].fill = f;                 /* nonzero, never 0x5A */
    }
}

/* Category 1: memset/memcpy/memmove at exactly the declared size. */
static void cat_memory(void) {
    size_t i;
    for (i = 0; i < NTUP; ++i) {
        tup t = g_table[i];
        size_t cap = t.len, d, k;
        gbuf gd, gs;

        /* memset(buf, fill, cap) -- exactly cap bytes. */
        gbuf_init(&gd, cap, (unsigned char)(t.fill ^ 0xFFu));
        cplus_memset(gd.buf, t.fill, cap);
        CHECK_C(&gd, cap, "memset", t.len, t.off, t.fill);
        for (k = 0; k < cap; ++k)
            if (gd.buf[k] != t.fill) {
                EXPECT(0, "memset-content", t.len, t.off, t.fill);
                break;
            }
        ++g_cases;

        /* memcpy(buf, src, cap) -- exactly cap bytes. */
        gbuf_init(&gs, cap, 0xCC);
        gbuf_init(&gd, cap, 0x22);
        fill_pattern(gs.buf, cap, t.fill);
        cplus_memcpy(gd.buf, gs.buf, cap);
        CHECK_C(&gd, cap, "memcpy", t.len, t.off, t.fill);
        CHECK_C(&gs, cap, "memcpy-src", t.len, t.off, t.fill);
        for (k = 0; k < cap; ++k)
            if (gd.buf[k] != gs.buf[k]) {
                EXPECT(0, "memcpy-content", t.len, t.off, t.fill);
                break;
            }
        ++g_cases;

        /* memmove with overlap, length exactly cap-d. */
        gbuf_init(&gd, cap, 0x33);
        fill_pattern(gd.buf, cap, t.fill);
        d = t.off % cap;
        if (cap > d) {
            cplus_memmove(gd.buf + d, gd.buf, cap - d);
            CHECK_C(&gd, cap, "memmove", t.len, t.off, t.fill);
            for (k = 0; k < cap - d; ++k)
                if (gd.buf[d + k] != pattern_at(t.fill, k)) {
                    EXPECT(0, "memmove-content", t.len, t.off, t.fill);
                    break;
                }
        }
        ++g_cases;
    }
}

/* Category 3: strnlen -- NUL at variable positions, and no NUL in region. */
static void cat_strnlen(void) {
    size_t i;
    gbuf g;
    size_t r, pos;

    /* Exhaustive: NUL at every position 0..LEN-1 in a full-size buffer. */
    for (pos = 0; pos < LEN; ++pos) {
        gbuf_init(&g, LEN, 0x63);
        g.buf[pos] = 0;
        r = cplus_strnlen((const char *)g.buf, LEN);
        CHECK_C(&g, LEN, "strnlen-allpos", pos, LEN, 0);
        EXPECT(r == pos, "strnlen-allpos-ret", pos, LEN, 0);
        ++g_cases;
    }

    for (i = 0; i < NTUP; ++i) {
        tup t = g_table[i];
        size_t cap = t.len;

        pos = (t.off == 0u) ? 0u : (t.off * 37u) % cap;
        pos = pos % cap;
        gbuf_init(&g, cap, t.fill);
        g.buf[pos] = 0;
        r = cplus_strnlen((const char *)g.buf, cap);
        CHECK_C(&g, cap, "strnlen", t.len, t.off, t.fill);
        EXPECT(r == pos, "strnlen-return", t.len, t.off, t.fill);
        ++g_cases;

        /* No NUL inside [0,cap); a sentinel NUL sits beyond the canary. */
        gbuf_init(&g, cap, t.fill);
        g.buf[cap + GC] = 0; /* still inside g.buf[] (cap+GC <= 272 < 288) */
        r = cplus_strnlen((const char *)g.buf, cap);
        CHECK_C(&g, cap, "strnlen-nonul", t.len, t.off, t.fill);
        EXPECT(r == cap, "strnlen-nonul-return", t.len, t.off, t.fill);
        ++g_cases;
    }
}

/* Category 4: memchr / memrchr / memcmp at 0, last, one-past, huge n. */
static void cat_search(void) {
    size_t i;
    for (i = 0; i < NTUP; ++i) {
        tup t = g_table[i];
        size_t cap = t.len;
        unsigned char target = (unsigned char)(t.fill ^ 0xFFu);
        void *r;
        gbuf g, a, b;

        /* memchr at position 0. */
        gbuf_init(&g, cap, t.fill);
        g.buf[0] = target;
        r = cplus_memchr(g.buf, target, cap);
        CHECK_C(&g, cap, "memchr-first", t.len, t.off, t.fill);
        EXPECT(r == (void *)(g.buf + 0), "memchr-first-ret", t.len, t.off, t.fill);
        ++g_cases;

        /* memchr at the last valid byte. */
        gbuf_init(&g, cap, t.fill);
        g.buf[cap - 1u] = target;
        r = cplus_memchr(g.buf, target, cap);
        CHECK_C(&g, cap, "memchr-last", t.len, t.off, t.fill);
        EXPECT(r == (void *)(g.buf + cap - 1u), "memchr-last-ret", t.len, t.off, t.fill);
        ++g_cases;

        /* memchr "one past": target 0x5A lives exactly at buf[cap]. */
        gbuf_init(&g, cap, t.fill);
        r = cplus_memchr(g.buf, 0x5A, cap + 1u);
        CHECK_C(&g, cap, "memchr-onepast", t.len, t.off, t.fill);
        EXPECT(r == (void *)(g.buf + cap), "memchr-onepast-ret", t.len, t.off, t.fill);
        ++g_cases;

        /* memchr with SIZE_MAX n: stops at first match, no wild read. */
        gbuf_init(&g, cap, t.fill);
        g.buf[0] = target;
        r = cplus_memchr(g.buf, target, SIZE_MAX);
        CHECK_C(&g, cap, "memchr-huge", t.len, t.off, t.fill);
        EXPECT(r == (void *)(g.buf + 0), "memchr-huge-ret", t.len, t.off, t.fill);
        ++g_cases;

        /* memrchr at the last valid byte and at position 0. */
        gbuf_init(&g, cap, t.fill);
        g.buf[cap - 1u] = target;
        r = cplus_memrchr(g.buf, target, cap);
        CHECK_C(&g, cap, "memrchr-last", t.len, t.off, t.fill);
        EXPECT(r == (void *)(g.buf + cap - 1u), "memrchr-last-ret", t.len, t.off, t.fill);
        ++g_cases;

        gbuf_init(&g, cap, t.fill);
        g.buf[0] = target;
        r = cplus_memrchr(g.buf, target, cap);
        CHECK_C(&g, cap, "memrchr-first", t.len, t.off, t.fill);
        EXPECT(r == (void *)(g.buf + 0), "memrchr-first-ret", t.len, t.off, t.fill);
        ++g_cases;

        /* memcmp: equal, differing last byte, one-past, huge n w/ diff at 0. */
        gbuf_init(&a, cap, t.fill);
        gbuf_init(&b, cap, t.fill);
        EXPECT(cplus_memcmp(a.buf, b.buf, cap) == 0, "memcmp-eq", t.len, t.off, t.fill);
        CHECK_C(&a, cap, "memcmp-eq-a", t.len, t.off, t.fill);
        CHECK_C(&b, cap, "memcmp-eq-b", t.len, t.off, t.fill);
        ++g_cases;

        gbuf_init(&a, cap, t.fill);
        gbuf_init(&b, cap, t.fill);
        b.buf[cap - 1u] ^= 1u;
        EXPECT(cplus_memcmp(a.buf, b.buf, cap) != 0, "memcmp-lastdiff", t.len, t.off, t.fill);
        b.buf[cap - 1u] ^= 1u;
        CHECK_C(&b, cap, "memcmp-lastdiff-b", t.len, t.off, t.fill);
        ++g_cases;

        /* One-past via valid n == cap+1: deliberately alter the canary byte,
         * observe memcmp sees it, then restore before the canary check. */
        gbuf_init(&a, cap, t.fill);
        gbuf_init(&b, cap, t.fill);
        b.buf[cap] = 0x00;
        EXPECT(cplus_memcmp(a.buf, b.buf, cap + 1u) != 0, "memcmp-onepast", t.len, t.off, t.fill);
        gbuf_repair(&b, cap);
        assert(gbuf_ok(&b, cap));
        ++g_cases;

        /* SIZE_MAX n with a difference at byte 0: returns immediately. */
        gbuf_init(&a, cap, t.fill);
        gbuf_init(&b, cap, t.fill);
        b.buf[0] ^= 1u;
        EXPECT(cplus_memcmp(a.buf, b.buf, SIZE_MAX) != 0, "memcmp-huge", t.len, t.off, t.fill);
        b.buf[0] ^= 1u;
        CHECK_C(&b, cap, "memcmp-huge-b", t.len, t.off, t.fill);
        ++g_cases;
    }
}

/* ------------------------------------------------------------------ table 2 */
/* 300 generated (source length, destination capacity) tuples for the
 * strncpy / strncat family, including exact, one-less, zero and huge-n cases. */
typedef struct { size_t src_len, cap; unsigned char seed; } stup;
#define NST 300u
static stup g_st[NST];

static void st_init(void) {
    size_t i;
    for (i = 0; i < NST; ++i) {
        g_st[i].src_len = (i * 17u + 3u) % (LEN + 1u);
        g_st[i].cap     = (i * 29u + 11u) % (LEN + 1u);
        g_st[i].seed    = (unsigned char)(0x21u + (i % 70u));
    }
    /* Hand-crafted boundary tuples. */
    g_st[0]  = (stup){ 0u,   0u, 0x41 }; /* empty source, zero capacity */
    g_st[1]  = (stup){ 8u,   0u, 0x41 }; /* zero capacity */
    g_st[2]  = (stup){ 8u,   8u, 0x41 }; /* cap == src_len (no terminator) */
    g_st[3]  = (stup){ 8u,   7u, 0x41 }; /* cap == src_len - 1 */
    g_st[4]  = (stup){ 8u,   9u, 0x41 }; /* cap == src_len + 1 */
    g_st[5]  = (stup){ 1u,   0u, 0x41 };
    g_st[6]  = (stup){ LEN, LEN, 0x41 }; /* full buffer */
    g_st[7]  = (stup){ LEN, LEN - 1u, 0x41 };
    g_st[8]  = (stup){ LEN - 1u, LEN, 0x41 };
    g_st[9]  = (stup){ 1u, LEN, 0x41 };  /* heavy zero padding */
    /* Tuples 10..39: cap == src_len + 1 so SIZE_MAX strncat lands its NUL
     * exactly on the last active byte. */
    for (i = 10; i < 40; ++i) {
        if (g_st[i].src_len < LEN) g_st[i].cap = g_st[i].src_len + 1u;
        else g_st[i].cap = LEN;
    }
}

static void cat_strncpy_family(void) {
    size_t i;
    for (i = 0; i < NST; ++i) {
        stup t = g_st[i];
        gbuf src, dst;
        size_t m, k, n;

        gbuf_init(&src, t.src_len + 1u, 0xCC);
        fill_pattern(src.buf, t.src_len, t.seed); /* all nonzero */
        src.buf[t.src_len] = 0;
        gbuf_init(&dst, t.cap, 0xEE);

        /* cplus_strncpy with n exactly == destination capacity. */
        cplus_strncpy((char *)(void *)dst.buf, (const char *)(const void *)src.buf,
                      t.cap);
        CHECK_C(&dst, t.cap, "strncpy", t.src_len, t.cap, i);
        CHECK_C(&src, t.src_len + 1u, "strncpy-src", t.src_len, t.cap, i);
        m = (t.cap < t.src_len) ? t.cap : t.src_len;
        for (k = 0; k < m; ++k)
            if (dst.buf[k] != src.buf[k]) {
                EXPECT(0, "strncpy-copy", t.src_len, t.cap, i);
                break;
            }
        for (k = m; k < t.cap; ++k)
            if (dst.buf[k] != 0) {
                EXPECT(0, "strncpy-pad", t.src_len, t.cap, i);
                break;
            }
        ++g_cases;

        /* cplus_strncat with a bounded n (cap-1) into an empty destination. */
        if (t.cap >= 1u) {
            n = t.cap - 1u;
            gbuf_init(&dst, t.cap, 0xEE);
            dst.buf[0] = 0; /* empty string */
            cplus_strncat((char *)(void *)dst.buf,
                          (const char *)(const void *)src.buf, n);
            CHECK_C(&dst, t.cap, "strncat", t.src_len, t.cap, i);
            m = (n < t.src_len) ? n : t.src_len;
            for (k = 0; k < m; ++k)
                if ((unsigned char)dst.buf[k] != src.buf[k]) {
                    EXPECT(0, "strncat-copy", t.src_len, t.cap, i);
                    break;
                }
            EXPECT(dst.buf[m] == 0, "strncat-term", t.src_len, t.cap, i);
            ++g_cases;
        }

        /* cplus_strncat with n == SIZE_MAX: bounded by the source NUL. */
        if (t.cap >= t.src_len + 1u) {
            gbuf_init(&dst, t.cap, 0xEE);
            dst.buf[0] = 0;
            cplus_strncat((char *)(void *)dst.buf,
                          (const char *)(const void *)src.buf, SIZE_MAX);
            CHECK_C(&dst, t.cap, "strncat-huge", t.src_len, t.cap, i);
            for (k = 0; k < t.src_len; ++k)
                if ((unsigned char)dst.buf[k] != src.buf[k]) {
                    EXPECT(0, "strncat-huge-copy", t.src_len, t.cap, i);
                    break;
                }
            EXPECT(dst.buf[t.src_len] == 0, "strncat-huge-term", t.src_len, t.cap, i);
            ++g_cases;
        }
    }
}

/* ------------------------------------------------------- category 5: grid */
/* Off-by-one grid: every source length 0..256 against every destination
 * capacity 0..256 using cplus_strncpy with an exact n and canaries. */
static void cat_offbyone_grid(void) {
    size_t len, cap, k, m;
    gbuf src, dst;

    for (len = 0; len <= LEN; ++len) {
        gbuf_init(&src, len + 1u, 0x41);
        fill_pattern(src.buf, len, 0x41); /* nonzero */
        src.buf[len] = 0;
        for (cap = 0; cap <= LEN; ++cap) {
            gbuf_init(&dst, cap, 0x7E);
            cplus_strncpy((char *)(void *)dst.buf,
                          (const char *)(const void *)src.buf, cap);
            CHECK_C(&dst, cap, "grid", len, cap, 0);
            CHECK_C(&src, len + 1u, "grid-src", len, cap, 0);
            m = (cap < len) ? cap : len;
            for (k = 0; k < m; ++k)
                if (dst.buf[k] != src.buf[k]) {
                    EXPECT(0, "grid-copy", len, cap, 0);
                    break;
                }
            for (k = m; k < cap; ++k)
                if (dst.buf[k] != 0) {
                    EXPECT(0, "grid-pad", len, cap, 0);
                    break;
                }
            ++g_cases;
        }
    }
}

int main(void) {
    table_init();
    st_init();
    cat_memory();
    cat_strnlen();
    cat_search();
    cat_strncpy_family();
    cat_offbyone_grid();

    if (g_findings == 0)
        printf("corpus_buffer: %lu cases ok\n", g_cases);
    else
        printf("corpus_buffer: %lu cases run, %lu FINDING(s)\n",
               g_cases, g_findings);
    fflush(stdout);
    assert(g_findings == 0);
    return 0;
}
