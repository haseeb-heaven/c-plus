/* Adversarial, table-driven integer-overflow corpus for the C+ hardened libc.
 *
 * Standalone C99.  Links ONLY build/libc/libcplus_hardened.a (host CRT is the
 * differential oracle; no SafeCRT).  Every input below is a DEFINED call: we
 * never trigger raw UB in the test itself (no signed overflow, no OOB access,
 * no NULL deref), and all overflow values are fed through the hardened entry
 * points whose contract is to reject them gracefully.
 *
 * A mismatch is never hidden: it is printed as a "FINDING:" line (with the
 * exact tuple and observed result), counted, and makes main() return 1.  On a
 * clean run the program prints "corpus_integer: <N> cases ok" and returns 0.
 */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <inttypes.h>

/* C leaves endptr unspecified for an invalid conversion base.  The C+ hardened
 * wrappers define it as nptr (with EINVAL); glibc leaves it NULL.  Do not
 * compare an unspecified host value against the hardened contract. */
#define INVALID_CONV_BASE(b_) ((b_) < 0 || (b_) == 1 || (b_) > 36)

/* ---- functions under test (namespaced wrappers in libcplus_hardened.a) --- */
extern void *cplus_malloc(size_t);
extern void *cplus_calloc(size_t, size_t);
extern void *cplus_realloc(void *, size_t);
extern void cplus_free(void *);
extern long cplus_strtol(const char *, char **, int);
extern unsigned long cplus_strtoul(const char *, char **, int);
extern long long cplus_strtoll(const char *, char **, int);
extern unsigned long long cplus_strtoull(const char *, char **, int);
extern double cplus_strtod(const char *, char **);
extern float cplus_strtof(const char *, char **);
extern intmax_t cplus_wcstoimax(const wchar_t *, wchar_t **, int);
extern uintmax_t cplus_wcstoumax(const wchar_t *, wchar_t **, int);
extern void *cplus_aligned_alloc(size_t, size_t);
extern size_t cplus_fread(void *, size_t, size_t, FILE *);
extern size_t cplus_fwrite(const void *, size_t, size_t, FILE *);
extern void cplus_libc_log_set_enabled(int);

/* ---- bookkeeping -------------------------------------------------------- */
static unsigned long g_cases = 0;
static unsigned long g_findings = 0;

#define FINDING(...)                                                          \
    do {                                                                      \
        ++g_findings;                                                         \
        if (g_findings <= 40) {                                               \
            fputs("FINDING: ", stderr);                                       \
            fprintf(stderr, __VA_ARGS__);                                     \
            fputc('\n', stderr);                                              \
        }                                                                     \
    } while (0)

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond))                                                          \
            FINDING(__VA_ARGS__);                                             \
    } while (0)

/* Deterministic xorshift64* PRNG so the whole corpus is reproducible. */
static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static void rnd_seed(uint64_t s) { rng_state = s ? s : 1ULL; }
static uint64_t rnd(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 2685821657736338717ULL;
}

#define SLU(x) ((unsigned long long)(x)) /* size_t -> printf */

/* ======================================================================== */
/* 1. malloc / calloc / realloc overflow shapes                              */
/* ======================================================================== */
typedef struct {
    size_t n;
    size_t sz;
    int zero_dim; /* 0-length dimension => valid calloc, never an overflow */
} AllocCase;

static void test_alloc_overflow(void) {
    static const AllocCase fixed[] = {
        { SIZE_MAX, 2, 0 },          /* wraps to 0 */
        { SIZE_MAX / 2u + 1u, 2, 0 },/* wraps to 0 */
        { 1, SIZE_MAX, 0 },          /* no wrap, host alloc must fail */
        { SIZE_MAX, 0, 0 + 1 },      /* zero dimension: defined, not overflow */
        { 0, SIZE_MAX, 1 },          /* zero dimension: defined, not overflow */
        { SIZE_MAX / 2u, 2, 0 }      /* product == SIZE_MAX-1, host fails */
    };
    size_t i;
    void *p;
    int e;

    for (i = 0; i < sizeof fixed / sizeof fixed[0]; ++i) {
        errno = 0;
        p = cplus_calloc(fixed[i].n, fixed[i].sz);
        e = errno;
        if (fixed[i].zero_dim) {
            /* calloc(n,0)/calloc(0,sz) are DEFINED: either a unique pointer
             * or NULL.  We only require no crash; free a real allocation. */
            if (p)
                cplus_free(p);
        } else {
            CHECK(p == NULL,
                  "calloc(%llu,%llu) returned non-NULL %p (expected NULL)",
                  SLU(fixed[i].n), SLU(fixed[i].sz), p);
            CHECK(e == ENOMEM,
                  "calloc(%llu,%llu) errno=%d (expected ENOMEM)",
                  SLU(fixed[i].n), SLU(fixed[i].sz), e);
        }
        ++g_cases;
    }

    /* 100 pseudorandom (n,size) pairs with n*size definitely wrapping. */
    rnd_seed(0xC0FFEE123456789ULL);
    for (i = 0; i < 100; ++i) {
        size_t n = (size_t)(rnd() | (1ULL << 63));
        size_t sz = (size_t)(rnd() | (1ULL << 63));
        errno = 0;
        p = cplus_calloc(n, sz);
        e = errno;
        CHECK(p == NULL && e == ENOMEM,
              "calloc(%llu,%llu) [case %llu] -> p=%p errno=%d (expected NULL/ENOMEM)",
              SLU(n), SLU(sz), SLU((unsigned long long)i), p, e);
        ++g_cases;
    }

    /* Direct malloc / realloc overflow entry points. */
    errno = 0;
    p = cplus_malloc(SIZE_MAX);
    e = errno;
    CHECK(p == NULL && e == ENOMEM,
          "malloc(SIZE_MAX) -> p=%p errno=%d (expected NULL/ENOMEM)", p, e);
    ++g_cases;

    errno = 0;
    p = cplus_malloc(0);
    e = errno;
    CHECK(p != NULL, "malloc(0) -> p=%p errno=%d (expected non-NULL)", p, e);
    if (p)
        cplus_free(p);
    ++g_cases;

    /* A failed realloc leaves the original block valid and untouched. */
    {
        void *q = cplus_malloc(8);
        void *r;
        CHECK(q != NULL, "malloc(8) failed before realloc overflow probe");
        errno = 0;
        r = cplus_realloc(q, SIZE_MAX);
        e = errno;
        CHECK(r == NULL && e == ENOMEM,
              "realloc(p,SIZE_MAX) -> p=%p errno=%d (expected NULL/ENOMEM)", r, e);
        cplus_free(q); /* q is still live after the failed realloc */
        ++g_cases;

        errno = 0;
        r = cplus_realloc(NULL, SIZE_MAX);
        e = errno;
        CHECK(r == NULL && e == ENOMEM,
              "realloc(NULL,SIZE_MAX) -> p=%p errno=%d (expected NULL/ENOMEM)", r, e);
        ++g_cases;
    }
}

/* ======================================================================== */
/* 2. strtol / strtoul / strtoll / strtoull / strtod / strtof                */
/* ======================================================================== */

/* Build one of 12 deterministic string shapes into buf. */
static void gen_string(int i, char *buf, size_t cap) {
    int kind = i % 12;
    uint64_t v = rnd();
    switch (kind) {
    case 0:  snprintf(buf, cap, "%ld", (long)(v % 2000000000UL)); break;
    case 1:  snprintf(buf, cap, "-%ld", (long)(v % 2000000000UL)); break;
    case 2:  snprintf(buf, cap, "0x%llx", (unsigned long long)v); break;
    case 3:  snprintf(buf, cap, "0%llo", (unsigned long long)(v % 1000000000ULL)); break;
    case 4:  snprintf(buf, cap, "  +%lu tail", (unsigned long)(v % 1000000UL)); break;
    case 5:  snprintf(buf, cap, "%lu", (unsigned long)(ULONG_MAX - (v % 1000UL))); break;
    case 6:  snprintf(buf, cap, "-%lu", (unsigned long)(ULONG_MAX - (v % 1000UL))); break;
    case 7:  snprintf(buf, cap, "%ld.%03lde%+d", (long)(v % 1000UL),
                      (long)(v % 1000UL), (int)(v % 600UL) - 300); break;
    case 8:  snprintf(buf, cap, "%s", (v & 1) ? "nan" : "-inf"); break;
    case 9:  snprintf(buf, cap, "%s", (v & 1) ? "" : "-"); break;
    case 10: { char *p = buf; int d; for (d = 0; d < 30; ++d) *p++ = '9'; *p = '\0'; break; }
    default: snprintf(buf, cap, "0x%016llx%016llx", (unsigned long long)v,
                      (unsigned long long)rnd()); break;
    }
}

/* Differential check for one integer conversion. */
#define CHECK_IC(FN, HOST, TYPE, PRIFMT, NAME) do {                            \
    char *ce_ = NULL, *he_ = NULL;                                             \
    int cerr_ = 0, herr_ = 0;                                                  \
    TYPE cv_ = 0, hv_ = 0;                                                     \
    errno = 0; cv_ = FN(s, &ce_, b); cerr_ = errno;                            \
    errno = 0; hv_ = HOST(s, &he_, b); herr_ = errno;                          \
    if (cv_ != hv_)                                                            \
        FINDING("%s('%s',base=%d): value %" PRIFMT " != host %" PRIFMT,         \
                NAME, s, b, cv_, hv_);                                         \
    if (!INVALID_CONV_BASE(b) &&                                              \
        (ce_ ? (ce_ - s) : (ptrdiff_t)-1) != (he_ ? (he_ - s) : (ptrdiff_t)-1)) \
        FINDING("%s('%s',base=%d): endptr off %td != host %td",                \
                NAME, s, b, ce_ ? (ce_ - s) : (ptrdiff_t)-1,                    \
                he_ ? (he_ - s) : (ptrdiff_t)-1);                              \
    if (cerr_ != herr_)                                                        \
        FINDING("%s('%s',base=%d): errno %d != host %d", NAME, s, b,           \
                cerr_, herr_);                                                 \
} while (0)

static int same_double(double a, double b) {
    return memcmp(&a, &b, sizeof a) == 0 || a == b || (isnan(a) && isnan(b));
}
static int same_float(float a, float b) {
    return memcmp(&a, &b, sizeof a) == 0 || a == b || (isnan(a) && isnan(b));
}

static void test_strto(void) {
    static const int bases[] = { 0, 2, 8, 10, 16, 36, 1, 37, -1, INT_MIN, INT_MAX };
    const int nb = (int)(sizeof bases / sizeof bases[0]);
    char s[128];
    int i, bi;

    rnd_seed(0x123456789ABCDEFULL);
    for (i = 0; i < 200; ++i) {
        gen_string(i, s, sizeof s);

        for (bi = 0; bi < nb; ++bi) {
            int b = bases[bi];
            CHECK_IC(cplus_strtol,   strtol,   long,             "ld",  "strtol");
            CHECK_IC(cplus_strtoul,  strtoul,  unsigned long,    "lu",  "strtoul");
            CHECK_IC(cplus_strtoll,  strtoll,  long long,        "lld", "strtoll");
            CHECK_IC(cplus_strtoull, strtoull, unsigned long long,"llu", "strtoull");
            g_cases += 4;
        }

        /* Floating conversions ignore 'base'; one oracle call per string. */
        {
            char *ce = NULL, *he = NULL;
            int cerr, herr;
            double cv, hv;
            float cf, hf;

            errno = 0; cv = cplus_strtod(s, &ce); cerr = errno;
            errno = 0; hv = strtod(s, &he);      herr = errno;
            if (!same_double(cv, hv))
                FINDING("strtod('%s'): value %g != host %g", s, cv, hv);
            if ((ce ? (ce - s) : (ptrdiff_t)-1) != (he ? (he - s) : (ptrdiff_t)-1))
                FINDING("strtod('%s'): endptr off %td != host %td", s,
                        ce ? (ce - s) : (ptrdiff_t)-1,
                        he ? (he - s) : (ptrdiff_t)-1);
            if (cerr != herr)
                FINDING("strtod('%s'): errno %d != host %d", s, cerr, herr);
            ++g_cases;

            ce = he = NULL;
            errno = 0; cf = cplus_strtof(s, &ce); cerr = errno;
            errno = 0; hf = strtof(s, &he);      herr = errno;
            if (!same_float(cf, hf))
                FINDING("strtof('%s'): value %g != host %g", s, (double)cf, (double)hf);
            if ((ce ? (ce - s) : (ptrdiff_t)-1) != (he ? (he - s) : (ptrdiff_t)-1))
                FINDING("strtof('%s'): endptr off %td != host %td", s,
                        ce ? (ce - s) : (ptrdiff_t)-1,
                        he ? (he - s) : (ptrdiff_t)-1);
            if (cerr != herr)
                FINDING("strtof('%s'): errno %d != host %d", s, cerr, herr);
            ++g_cases;
        }
    }
}

/* ======================================================================== */
/* 3. wcstoimax / wcstoumax (incl. 40-digit saturation)                      */
/* ======================================================================== */
static void gen_wide(int i, wchar_t *out, size_t cap) {
    assert(cap >= 42); /* 40 digits + optional sign + terminator */
    if (i < 40) {
        int k = 0, d;
        if (i & 1)
            out[k++] = L'-';
        for (d = 0; d < 40; ++d)
            out[k++] = L'9';
        out[k] = L'\0';
    } else {
        uint64_t v = rnd();
        switch (i % 4) {
        case 0: swprintf(out, cap, L"%ld", (long)(v % 1000000000UL)); break;
        case 1: swprintf(out, cap, L"0x%llx", (unsigned long long)v); break;
        case 2: swprintf(out, cap, L"-%ld", (long)(v % 1000000000UL)); break;
        default: swprintf(out, cap, L"%ldtail", (long)(v % 1000000000UL)); break;
        }
    }
}

static void test_wcstoimax(void) {
    static const int bases[] = { 0, 10, 16 };
    wchar_t wb[64];
    int i;

    rnd_seed(0xDEADBEEFCAFEULL);
    for (i = 0; i < 100; ++i) {
        int base = (i < 40) ? 10 : bases[i % 3];
        int sat40 = (i < 40);
        int positive = !(i & 1);
        wchar_t *ce = NULL, *he = NULL;
        int cerr, herr;
        intmax_t cv, hv;
        uintmax_t uv, vv;

        gen_wide(i, wb, sizeof wb / sizeof wb[0]);

        errno = 0; cv = cplus_wcstoimax(wb, &ce, base); cerr = errno;
        errno = 0; hv = wcstoimax(wb, &he, base);      herr = errno;
        if (cv != hv)
            FINDING("wcstoimax('%ls',base=%d): value %lld != host %lld",
                    wb, base, (long long)cv, (long long)hv);
        if (!INVALID_CONV_BASE(base) &&
            (ce ? (ce - wb) : (ptrdiff_t)-1) != (he ? (he - wb) : (ptrdiff_t)-1))
            FINDING("wcstoimax('%ls',base=%d): endptr off %td != host %td",
                    wb, base, ce ? (ce - wb) : (ptrdiff_t)-1,
                    he ? (he - wb) : (ptrdiff_t)-1);
        if (cerr != herr)
            FINDING("wcstoimax('%ls',base=%d): errno %d != host %d",
                    wb, base, cerr, herr);
        if (sat40) {
            CHECK(cerr == ERANGE,
                  "wcstoimax('%ls'): errno=%d expected ERANGE", wb, cerr);
            CHECK(cv == INTMAX_MAX || cv == INTMAX_MIN,
                  "wcstoimax('%ls'): value %lld not saturated", wb, (long long)cv);
        }
        ++g_cases;

        ce = he = NULL;
        errno = 0; uv = cplus_wcstoumax(wb, &ce, base); cerr = errno;
        errno = 0; vv = wcstoumax(wb, &he, base);      herr = errno;
        if (uv != vv)
            FINDING("wcstoumax('%ls',base=%d): value %llu != host %llu",
                    wb, base, (unsigned long long)uv, (unsigned long long)vv);
        if (!INVALID_CONV_BASE(base) &&
            (ce ? (ce - wb) : (ptrdiff_t)-1) != (he ? (he - wb) : (ptrdiff_t)-1))
            FINDING("wcstoumax('%ls',base=%d): endptr off %td != host %td",
                    wb, base, ce ? (ce - wb) : (ptrdiff_t)-1,
                    he ? (he - wb) : (ptrdiff_t)-1);
        if (cerr != herr)
            FINDING("wcstoumax('%ls',base=%d): errno %d != host %d",
                    wb, base, cerr, herr);
        if (sat40 && positive) {
            CHECK(cerr == ERANGE,
                  "wcstoumax('%ls'): errno=%d expected ERANGE", wb, cerr);
            CHECK(uv == UINTMAX_MAX,
                  "wcstoumax('%ls'): value %llu not saturated",
                  wb, (unsigned long long)uv);
        }
        ++g_cases;
    }
}

/* ======================================================================== */
/* 4. checked arithmetic in cplus_aligned_alloc                              */
/* ======================================================================== */
static void check_aligned(size_t a, size_t sz) {
    int reject = (a == 0u) || ((a & (a - 1u)) != 0u) ||
                 ((a % sizeof(void *)) != 0u) || ((sz % a) != 0u);
    void *p;

    errno = 0;
    p = cplus_aligned_alloc(a, sz);
    if (reject) {
        if (!(p == NULL && errno == EINVAL))
            FINDING("aligned_alloc(%llu,%llu): expected NULL/EINVAL, got p=%p errno=%d",
                    SLU(a), SLU(sz), p, errno);
    } else if (p != NULL) {
        if (((uintptr_t)p % a) != 0u)
            FINDING("aligned_alloc(%llu,%llu): p=%p not %llu-byte aligned",
                    SLU(a), SLU(sz), p, SLU(a));
        free(p); /* host aligned_alloc() => host free() */
    }
    ++g_cases;
}

static void test_aligned(void) {
    int sh, made = 0;

    /* Every power of two from 2^0 .. 2^32, with an in-contract and an
     * out-of-contract size.  size=0 avoids enormous real allocations while
     * still exercising the full constraint check. */
    for (sh = 0; sh <= 32; ++sh) {
        size_t a = (size_t)1 << sh;
        check_aligned(a, 0);
        check_aligned(a, a + 1u);
        made += 2;
    }

    /* Non-powers, odd alignments, and mixed sizes to reach 200 pairs. */
    rnd_seed(0xA5A5F00DULL);
    while (made < 200) {
        uint64_t r = rnd();
        size_t a;
        size_t sz;
        if (r & 1u)
            a = (size_t)1 << (int)((r >> 1) % 33u); /* power of two <= 2^32 */
        else
            a = (size_t)(r | 1ULL);                 /* odd => non-power */
        sz = (r & 2u) ? 0u : (size_t)((r >> 8) % 1024u);
        check_aligned(a, sz);
        ++made;
    }
}

/* ======================================================================== */
/* 5. overflowing (size,nmemb) in cplus_fread / cplus_fwrite                 */
/* ======================================================================== */
static void test_fread_fwrite(void) {
    FILE *f = tmpfile();
    struct {
        unsigned char pre[8];
        unsigned char buf[32];
        unsigned char post[8];
    } holder;
    size_t i, k;

    if (!f) {
        FINDING("tmpfile() failed; cannot run fread/fwrite overflow corpus");
        return;
    }
    memset(&holder, 0xA5, sizeof holder);

    rnd_seed(0xFEEDFACE1234ULL);
    for (i = 0; i < 100; ++i) {
        uint64_t r = rnd();
        /* Both operands >= 2^32 => product necessarily wraps mod 2^64. */
        size_t sz = (size_t)((r & 0xFFFFFFFFULL) | (1ULL << 32));
        size_t nm = (size_t)((r >> 32) | (1ULL << 32));
        size_t got;
        int e;

        errno = 0;
        got = cplus_fread(holder.buf, sz, nm, f);
        e = errno;
        CHECK(got == 0 && e == EOVERFLOW,
              "fread(size=%llu,nmemb=%llu) [case %llu] -> got=%llu errno=%d "
              "(expected 0/EOVERFLOW)", SLU(sz), SLU(nm), SLU((unsigned long long)i),
              SLU(got), e);
        ++g_cases;

        errno = 0;
        got = cplus_fwrite(holder.buf, sz, nm, f);
        e = errno;
        CHECK(got == 0 && e == EOVERFLOW,
              "fwrite(size=%llu,nmemb=%llu) [case %llu] -> got=%llu errno=%d "
              "(expected 0/EOVERFLOW)", SLU(sz), SLU(nm), SLU((unsigned long long)i),
              SLU(got), e);
        ++g_cases;

        for (k = 0; k < sizeof holder.pre; ++k)
            CHECK(holder.pre[k] == 0xA5,
                  "fread/fwrite case %llu clobbered pre-canary[%llu]=%u",
                  SLU((unsigned long long)i), SLU((unsigned long long)k),
                  (unsigned)holder.pre[k]);
        for (k = 0; k < sizeof holder.post; ++k)
            CHECK(holder.post[k] == 0xA5,
                  "fread/fwrite case %llu clobbered post-canary[%llu]=%u",
                  SLU((unsigned long long)i), SLU((unsigned long long)k),
                  (unsigned)holder.post[k]);
    }
    fclose(f);
}

/* ======================================================================== */
int main(void) {
    cplus_libc_log_set_enabled(0); /* keep the corpus fast + quiet */

    test_alloc_overflow();
    test_strto();
    test_wcstoimax();
    test_aligned();
    test_fread_fwrite();

    assert(g_cases > 600);
    if (g_findings == 0) {
        printf("corpus_integer: %lu cases ok\n", g_cases);
        return 0;
    }
    printf("corpus_integer: %lu cases, %lu FINDING(s)\n", g_cases, g_findings);
    return 1;
}
