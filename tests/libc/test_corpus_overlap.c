/* tests/libc/test_corpus_overlap.c
 *
 * Deterministic, high-volume adversarial corpus probing overlap / aliasing
 * behavior of the C+ hardened libc memory and string routines.
 *
 * Design
 * ------
 *  1) MEMMOVE CORPUS (table-driven).
 *     A case table is built deterministically at startup from an explicit
 *     lengths table {0,1,2,3,7,15,31,63,64} and the two 64-entry offset
 *     ranges src_offset in [0,63], dst_offset in [0,63].  That is
 *     64 * 64 * 9 = 36864 distinct (src, dst, len, direction) tuples, all
 *     executed inside one backing buffer `buf[4096]` filled with a fixed
 *     pseudorandom pattern.  For each tuple:
 *        - an independent reference is produced with the HOST memmove applied
 *          to a separate pristine copy,
 *        - cplus_memmove is applied to the live buffer,
 *        - the whole buffer is compared byte-for-byte against the reference,
 *        - bytes outside [dst, dst+len) are additionally checked to be
 *          unchanged relative to the pristine copy.
 *
 *  2) MEMCMP / MEMCHR / MEMRCHR CORPUS (200 cases).
 *     cplus_memcmp is exercised on overlapping regions (both pointers alias
 *     the same backing buffer).  cplus_memchr / cplus_memrchr are given an n
 *     that extends past a logical sub-buffer end (a NUL marker) but stays
 *     inside the backing buffer, which is fully defined behavior.
 *
 *  3) STRNCPY / STRNCAT CORPUS (100 cases).
 *     Source and destination live in the same backing buffer but occupy
 *     disjoint ranges (defined; no overlap), and results are compared
 *     against the HOST strncpy / strncat applied to a pristine copy.
 *
 * NOTE ON memcpy_s: a checked CRT surface "memcpy_s" is declared as an
 * optional "safe" counterpart in libc/contracts/libc_contracts.json and
 * libc/contracts/symbol_map.json; such a checked API may *reject* overlapping
 * ranges.  This test never calls cplus_memcpy with overlap (that is UB in the
 * test itself) and does not require any particular checked-memcpy behavior;
 * all overlapping raw copies go through cplus_memmove, which is legal.
 *
 * Any mismatch emits a "FINDING:" line carrying exact offsets and lengths.
 */
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

/* Public prototypes (mirror libc/install-include/cplus_libc.h). */
void *cplus_memmove(void *, const void *, size_t);
int   cplus_memcmp(const void *, const void *, size_t);
void *cplus_memchr(const void *, int, size_t);
void *cplus_memrchr(const void *, int, size_t);
char *cplus_strncpy(char *restrict, const char *restrict, size_t);
char *cplus_strncat(char *restrict, const char *restrict, size_t);

#define BUF_SIZE   ((size_t)4096)
#define SBUF_SIZE  ((size_t)1024)

static unsigned long g_failures = 0;
static unsigned long g_total    = 0;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static int sgn(int v) { return (v > 0) - (v < 0); }

/* Fill a buffer with a fixed, deterministic pseudorandom byte pattern. */
static void fill_pattern(unsigned char *p, size_t n) {
    uint32_t x = 0x12345678u;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;       /* Numerical Recipes LCG */
        p[i] = (unsigned char)((x >> 24) & 0xffu);
    }
}

/* Independent reference memrchr (avoids any host feature-macro dependency). */
static void *ref_memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = n; i > 0; --i)
        if (p[i - 1] == uc) return (void *)(uintptr_t)(p + i - 1);
    return NULL;
}

static unsigned long first_diff(const unsigned char *a, const unsigned char *b,
                                size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return (unsigned long)i;
    return (unsigned long)n;
}

/* ------------------------------------------------------------------ */
/* 1) memmove overlap corpus                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned src;
    unsigned dst;
    unsigned len;
    int      dir;   /* -1: dst < src, 0: dst == src, +1: dst > src */
} move_case;

static const unsigned kLens[] = { 0u, 1u, 2u, 3u, 7u, 15u, 31u, 63u, 64u };
#define NLENS ((size_t)(sizeof kLens / sizeof kLens[0]))
#define NOFF  ((size_t)64)
#define N_MOVE_CASES (NOFF * NOFF * NLENS)

static move_case  g_cases[N_MOVE_CASES];
static unsigned char buf[BUF_SIZE];
static unsigned char pristine[BUF_SIZE];
static unsigned char refbuf[BUF_SIZE];

static void build_case_table(void) {
    size_t n = 0;
    for (size_t s = 0; s < NOFF; ++s) {
        for (size_t d = 0; d < NOFF; ++d) {
            for (size_t li = 0; li < NLENS; ++li) {
                g_cases[n].src = (unsigned)s;
                g_cases[n].dst = (unsigned)d;
                g_cases[n].len = kLens[li];
                g_cases[n].dir = (d < s) ? -1 : ((d > s) ? 1 : 0);
                ++n;
            }
        }
    }
    assert(n == N_MOVE_CASES);
    assert(N_MOVE_CASES >= 900u);
}

static void run_memmove_corpus(void) {
    for (size_t k = 0; k < N_MOVE_CASES; ++k) {
        const move_case *c = &g_cases[k];
        ++g_total;

        /* Reset live buffer and build the independent reference. */
        memcpy(buf, pristine, BUF_SIZE);
        memcpy(refbuf, pristine, BUF_SIZE);
        memmove(refbuf + c->dst, refbuf + c->src, (size_t)c->len); /* HOST ref */

        /* Implementation under test: overlapping raw copy is legal here. */
        cplus_memmove(buf + c->dst, buf + c->src, (size_t)c->len);

        /* (a) bytes OUTSIDE [dst, dst+len) must be unchanged. */
        for (size_t i = 0; i < BUF_SIZE; ++i) {
            if (i >= (size_t)c->dst && i < (size_t)c->dst + c->len) continue;
            if (buf[i] != pristine[i]) {
                printf("FINDING: cplus_memmove outside src=%u dst=%u len=%u "
                       "dir=%d byte[%lu] got=%02x exp=%02x\n",
                       c->src, c->dst, c->len, c->dir, (unsigned long)i,
                       (unsigned)buf[i], (unsigned)pristine[i]);
                ++g_failures;
                break;
            }
        }

        /* (b) whole buffer must equal the independent reference. */
        if (memcmp(buf, refbuf, BUF_SIZE) != 0) {
            unsigned long idx = first_diff(buf, refbuf, BUF_SIZE);
            printf("FINDING: cplus_memmove src=%u dst=%u len=%u dir=%d "
                   "byte[%lu] got=%02x exp=%02x\n",
                   c->src, c->dst, c->len, c->dir, idx,
                   (unsigned)buf[idx], (unsigned)refbuf[idx]);
            ++g_failures;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 2) memcmp on overlap + memchr/memrchr past logical end              */
/* ------------------------------------------------------------------ */

#define N_CMP_CASES 200u

static void run_cmp_chr_corpus(void) {
    for (unsigned k = 0; k < N_CMP_CASES; ++k) {
        ++g_total;
        memcpy(buf, pristine, BUF_SIZE);

        /* --- cplus_memcmp on overlapping regions --- */
        unsigned s1 = (k * 11u) % 256u;
        unsigned delta = k % 8u;             /* <= 7 */
        unsigned s2 = s1 + delta;
        unsigned n = 16u + (k % 48u);        /* 16..63, always > delta */
        assert((size_t)s2 + n <= BUF_SIZE);
        assert(delta < n);                   /* ranges really do overlap */

        int h = memcmp(buf + s1, buf + s2, (size_t)n);       /* HOST ref */
        int g = cplus_memcmp(buf + s1, buf + s2, (size_t)n);
        if (sgn(h) != sgn(g)) {
            printf("FINDING: cplus_memcmp case=%u s1=%u s2=%u len=%u "
                   "host=%d cplus=%d\n", k, s1, s2, n, h, g);
            ++g_failures;
        }

        /* --- cplus_memchr / cplus_memrchr with n past logical end --- */
        unsigned sub = (k * 3u) % 256u;
        unsigned logical = 8u + (k % 24u);   /* logical sub-buffer length 8..31 */
        unsigned nn = logical + 1u + (k % 16u); /* reaches past logical end */
        assert((size_t)sub + nn <= BUF_SIZE);
        buf[sub + logical] = 0;              /* logical NUL end marker */

        int needle = (k & 1u)
                         ? (int)buf[sub + (k % logical)]  /* present in prefix */
                         : 0xFE;                          /* maybe absent */
        void *hp = memchr(buf + sub, needle, (size_t)nn);  /* HOST ref */
        void *gp = cplus_memchr(buf + sub, needle, (size_t)nn);
        if (hp != gp) {
            printf("FINDING: cplus_memchr case=%u sub=%u logical=%u n=%u "
                   "needle=%02x host_off=%ld cplus_off=%ld\n",
                   k, sub, logical, nn, (unsigned)(unsigned char)needle,
                   hp ? (long)((unsigned char *)hp - buf) : -1L,
                   gp ? (long)((unsigned char *)gp - buf) : -1L);
            ++g_failures;
        }

        void *hr = ref_memrchr(buf + sub, needle, (size_t)nn); /* independent */
        void *gr = cplus_memrchr(buf + sub, needle, (size_t)nn);
        if (hr != gr) {
            printf("FINDING: cplus_memrchr case=%u sub=%u logical=%u n=%u "
                   "needle=%02x ref_off=%ld cplus_off=%ld\n",
                   k, sub, logical, nn, (unsigned)(unsigned char)needle,
                   hr ? (long)((unsigned char *)hr - buf) : -1L,
                   gr ? (long)((unsigned char *)gr - buf) : -1L);
            ++g_failures;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 3) strncpy / strncat, same backing buffer but non-overlapping       */
/* ------------------------------------------------------------------ */

#define N_STR_CASES 100u
#define SRC_OFF  ((size_t)0)
#define DST_OFF  ((size_t)256)

static char sbuf[SBUF_SIZE];
static char sref[SBUF_SIZE];

static void setup_strings(unsigned k, unsigned *p_lsrc) {
    memset(sbuf, 0xCC, SBUF_SIZE);
    memset(sref, 0xCC, SBUF_SIZE);

    unsigned lsrc = 1u + (k % 32u);
    for (unsigned i = 0; i < lsrc; ++i) {
        char ch = (char)('a' + ((k + i) % 26u));
        sbuf[SRC_OFF + i] = ch;
        sref[SRC_OFF + i] = ch;
    }
    sbuf[SRC_OFF + lsrc] = '\0';
    sref[SRC_OFF + lsrc] = '\0';

    unsigned ldst = 1u + (k % 8u);
    for (unsigned i = 0; i < ldst; ++i) {
        char ch = (char)('A' + ((k + i) % 26u));
        sbuf[DST_OFF + i] = ch;
        sref[DST_OFF + i] = ch;
    }
    sbuf[DST_OFF + ldst] = '\0';
    sref[DST_OFF + ldst] = '\0';

    *p_lsrc = lsrc;
}

static void run_str_corpus(void) {
    for (unsigned k = 0; k < N_STR_CASES; ++k) {
        ++g_total;
        unsigned lsrc;
        setup_strings(k, &lsrc);

        /* src occupies [0, lsrc]; dst occupies [256, 256+n): disjoint. */
        size_t n = (size_t)(k % (lsrc + 16u));   /* includes 0 and > lsrc */
        assert(DST_OFF + n < SBUF_SIZE);

        cplus_strncpy(sbuf + DST_OFF, sbuf + SRC_OFF, n);
        strncpy(sref + DST_OFF, sref + SRC_OFF, n);        /* HOST ref */
        if (memcmp(sbuf, sref, SBUF_SIZE) != 0) {
            unsigned long idx = first_diff((const unsigned char *)sbuf,
                                           (const unsigned char *)sref,
                                           SBUF_SIZE);
            printf("FINDING: cplus_strncpy case=%u src=%lu dst=%lu n=%lu "
                   "byte[%lu] got=%02x exp=%02x\n",
                   k, (unsigned long)SRC_OFF, (unsigned long)DST_OFF,
                   (unsigned long)n, idx, (unsigned)(unsigned char)sbuf[idx],
                   (unsigned)(unsigned char)sref[idx]);
            ++g_failures;
        }

        /* --- strncat (re-setup so both start from identical seed state) --- */
        setup_strings(k, &lsrc);
        size_t nn = (size_t)(k % 40u);                     /* 0..39 */
        assert(DST_OFF + (size_t)(1u + k % 8u) + nn + 1u < SBUF_SIZE);

        cplus_strncat(sbuf + DST_OFF, sbuf + SRC_OFF, nn);
        strncat(sref + DST_OFF, sref + SRC_OFF, nn);       /* HOST ref */
        if (memcmp(sbuf, sref, SBUF_SIZE) != 0) {
            unsigned long idx = first_diff((const unsigned char *)sbuf,
                                           (const unsigned char *)sref,
                                           SBUF_SIZE);
            printf("FINDING: cplus_strncat case=%u src=%lu dst=%lu n=%lu "
                   "byte[%lu] got=%02x exp=%02x\n",
                   k, (unsigned long)SRC_OFF, (unsigned long)DST_OFF,
                   (unsigned long)nn, idx,
                   (unsigned)(unsigned char)sbuf[idx],
                   (unsigned)(unsigned char)sref[idx]);
            ++g_failures;
        }
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    fill_pattern(pristine, BUF_SIZE);
    memcpy(buf, pristine, BUF_SIZE);
    build_case_table();

    run_memmove_corpus();
    run_cmp_chr_corpus();
    run_str_corpus();

    assert(g_total == N_MOVE_CASES + N_CMP_CASES + N_STR_CASES);

    if (g_failures != 0) {
        printf("corpus_overlap: %lu FINDING(s) across %lu cases\n",
               g_failures, g_total);
        return 1;
    }

    printf("corpus_overlap: %lu cases ok\n", g_total);
    return 0;
}
