/* Adversarial deterministic differential fuzz harness for the C+ hardened libc.
 *
 * Generates only VALID inputs (NUL-terminated strings, correctly sized
 * destinations, non-overlapping memcpy operands) from a fixed-seed xorshift32
 * stream -- no dependence on libc rand().  Every generated value is fed to a
 * cplus_* wrapper and to the identically named host function, and the two
 * results are compared.  Canaries surround every destination buffer so silent
 * overruns/underruns are reported as divergences too.
 *
 * Host-function caveats (see tests/adversarial/differential_attack_NOTES.md):
 *  - memrchr() is NOT declared by the macOS SDK/libSystem used here, so that
 *    one family compares against a local reference implementation with the
 *    exact ISO/BSD semantics instead of a host symbol.
 *  - strnlen() is available via _DARWIN_C_SOURCE, which is enabled below.
 */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "install-include/cplus_libc.h"

/* ------------------------------------------------------------------ RNG -- */
static uint32_t g_seed;

static void rng_seed(uint32_t s) { g_seed = s ? s : 0x6D2B79F5u; }

/* xorshift32: deterministic, no libc rand() involvement. */
static uint32_t xr32(void) {
    uint32_t x = g_seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_seed = x;
    return x;
}

/* n must be > 0. */
static unsigned rnd(unsigned n) { return (unsigned)(xr32() % (uint32_t)n); }

static unsigned char rand_byte(void) { return (unsigned char)(xr32() & 0xFFu); }

/* Non-NUL random byte: keeps generated char arrays well-formed C strings. */
static unsigned char rand_char(void) {
    unsigned v = xr32() & 0xFFu;
    return (unsigned char)(v ? v : 1u);
}

/* --------------------------------------------------------------- guards -- */
#define CANARY_U32 0xA5C3E17Bu

static void guard_init(uint32_t *g) {
    for (int i = 0; i < 4; ++i) g[i] = CANARY_U32;
}
static int guard_ok(const uint32_t *g) {
    for (int i = 0; i < 4; ++i)
        if (g[i] != CANARY_U32) return 0;
    return 1;
}

typedef struct { uint32_t pre[4]; char b[64]; uint32_t post[4]; } str_dst;
typedef struct { uint32_t pre[4]; char b[128]; uint32_t post[4]; } cat_dst;
typedef struct { uint32_t pre[4]; unsigned char b[128]; uint32_t post[4]; } mem_dst;

/* -------------------------------------------------------------- reporting */
static unsigned long g_divergences;

static void report(const char *family, const char *fn, const char *fmt, ...) {
    if (g_divergences < 16u) {
        printf("DIFF [%s/%s] ", family, fn);
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        putchar('\n');
    }
    ++g_divergences;
}

static void print_bytes(const char *label, const unsigned char *p, size_t n) {
    printf(" %s=[", label);
    for (size_t i = 0; i < n && i < 40u; ++i) printf("%02x", p[i]);
    if (n > 40u) printf("...len=%zu", n);
    putchar(']');
}

static int sgn(int v) { return (v > 0) - (v < 0); }

/* Local semantic reference for memrchr (no host symbol on this SDK). */
static void *ref_memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    while (n > 0u) {
        --n;
        if (p[n] == uc) return (void *)(p + n);
    }
    return NULL;
}

/* ------------------------------------------------------------ generators -- */
/* Generate a well-formed (NUL-terminated, no embedded NUL) C string. */
static size_t gen_str(char *b, size_t cap, size_t maxlen) {
    size_t n = (size_t)rnd((unsigned)maxlen + 1u);
    if (n > cap) n = cap;
    for (size_t i = 0; i < n; ++i) b[i] = (char)rand_char();
    b[n] = '\0';
    return n;
}

/* Needle is either an actual substring of hay (match case) or random (often
 * no-match case).  Both are NUL-terminated and in-bounds. */
static size_t gen_needle(char *out, size_t cap, const char *hay, size_t haylen) {
    if (haylen > 0u && (xr32() & 1u) != 0u) {
        size_t start = (size_t)rnd((unsigned)haylen);
        size_t maxn = haylen - start;
        size_t len = (size_t)rnd((unsigned)maxn + 1u);
        if (len > cap) len = cap;
        memcpy(out, hay + start, len); /* out has room for len + NUL */
        out[len] = '\0';
        return len;
    }
    return gen_str(out, cap, 4u);
}

static void append_str(char *b, size_t *p, size_t cap, const char *s) {
    while (*s && *p + 1u < cap) b[(*p)++] = *s++;
    b[*p] = '\0';
}

static void append_ch(char *b, size_t *p, size_t cap, char ch) {
    if (*p + 1u < cap) b[(*p)++] = ch;
    b[*p] = '\0';
}

/* Randomised integer literal for the strto* family. */
static void gen_int_str(char *out, size_t cap, int *base_out) {
    size_t p = 0;
    int base = (rnd(3u) == 0u) ? 0 : ((rnd(2u) == 0u) ? 10 : 16);
    *base_out = base;
    out[0] = '\0';

    if (rnd(4u) == 0u) append_str(out, &p, cap, "   ");
    if (rnd(2u)) append_ch(out, &p, cap, '-');
    else if (rnd(2u)) append_ch(out, &p, cap, '+');

    if (rnd(12u) == 0u) {
        append_ch(out, &p, cap, 'z'); /* no conversion at all */
    } else if (base == 16 || (base == 0 && rnd(2u))) {
        static const char hd[] = "0123456789abcdefABCDEF";
        append_str(out, &p, cap, "0x");
        size_t nd = 1u + (size_t)rnd(8u);
        for (size_t i = 0; i < nd; ++i) append_ch(out, &p, cap, hd[rnd(22u)]);
    } else if (base == 0) {
        static const char od[] = "01234567";
        static const char dd[] = "0123456789";
        append_ch(out, &p, cap, '0');
        size_t nd = (size_t)rnd(10u);
        /* Sometimes use 8/9 after the leading zero: base-0 parsing must stop
         * there, which is a strong endptr/ERANGE discriminator. */
        int trap = (int)rnd(2u);
        for (size_t i = 0; i < nd; ++i)
            append_ch(out, &p, cap, trap ? dd[rnd(10u)] : od[rnd(8u)]);
    } else {
        static const char dd[] = "0123456789";
        size_t nd = 1u + (size_t)rnd(12u);
        for (size_t i = 0; i < nd; ++i) append_ch(out, &p, cap, dd[rnd(10u)]);
    }

    if (rnd(8u) == 0u) { /* force ERANGE */
        for (int i = 0; i < 40; ++i) append_ch(out, &p, cap, '9');
    }
    if (rnd(4u) == 0u) append_str(out, &p, cap, "@_junk"); /* exercise endptr */
}

/* Randomised floating literal for strtod/strtof. */
static void gen_float_str(char *out, size_t cap) {
    size_t p = 0;
    out[0] = '\0';
    if (rnd(4u) == 0u) append_str(out, &p, cap, "  ");
    if (rnd(2u)) append_ch(out, &p, cap, '-');

    switch (rnd(8u)) {
        case 0: append_str(out, &p, cap, "inf"); break;
        case 1: append_str(out, &p, cap, "nan"); break;
        case 2: append_str(out, &p, cap, "0x1.8p3"); break;
        case 3: append_str(out, &p, cap, "1e9999"); break; /* ERANGE */
        default: {
            size_t nd = 1u + (size_t)rnd(10u);
            for (size_t i = 0; i < nd; ++i) append_ch(out, &p, cap, (char)('0' + rnd(10u)));
            if (rnd(2u)) {
                append_ch(out, &p, cap, '.');
                size_t nf = (size_t)rnd(8u);
                for (size_t i = 0; i < nf; ++i) append_ch(out, &p, cap, (char)('0' + rnd(10u)));
            }
            if (rnd(2u)) {
                append_ch(out, &p, cap, (rnd(2u) ? 'e' : 'E'));
                if (rnd(2u)) append_ch(out, &p, cap, '-');
                size_t ne = 1u + (size_t)rnd(3u);
                for (size_t i = 0; i < ne; ++i) append_ch(out, &p, cap, (char)('0' + rnd(10u)));
            }
            break;
        }
    }
    if (rnd(6u) == 0u) append_str(out, &p, cap, "xyz");
}

static size_t gen_wstr(wchar_t *b, size_t cap, size_t maxlen) {
    size_t n = (size_t)rnd((unsigned)maxlen + 1u);
    if (n > cap) n = cap;
    for (size_t i = 0; i < n; ++i) {
        wchar_t wc;
        do {
            wc = (wchar_t)(1u + rnd(0x1FFFu));
        } while (wc >= 0xD800 && wc <= 0xDFFF); /* exclude surrogates */
        b[i] = wc;
    }
    b[n] = L'\0';
    return n;
}

/* --------------------------------------------------------------- families */
static void string_family(unsigned long iters) {
    for (unsigned long it = 0; it < iters; ++it) {
        char s1[40], s2[40], needle[8];
        size_t n1 = gen_str(s1, sizeof s1 - 1u, 32u);
        size_t n2 = gen_str(s2, sizeof s2 - 1u, 32u);
        size_t bound = (size_t)rnd(41u); /* 0..40, strnlen bound */

        size_t h = strlen(s1), c = cplus_strlen(s1);
        if (h != c)
            report("string", "strlen", "n1=%zu host=%zu cplus=%zu", n1, h, c);

        size_t hn = strnlen(s1, bound), cn = cplus_strnlen(s1, bound);
        if (hn != cn)
            report("string", "strnlen", "bound=%zu host=%zu cplus=%zu", bound, hn, cn);

        /* Huge bounds are still valid: both APIs stop at the first NUL. */
        if (strnlen(s1, SIZE_MAX) != cplus_strnlen(s1, SIZE_MAX))
            report("string", "strnlen", "huge_bound host=%zu cplus=%zu",
                   strnlen(s1, SIZE_MAX), cplus_strnlen(s1, SIZE_MAX));
        if (sgn(strncmp(s1, s2, SIZE_MAX)) != sgn(cplus_strncmp(s1, s2, SIZE_MAX)))
            report("string", "strncmp", "huge_bound host=%d cplus=%d",
                   sgn(strncmp(s1, s2, SIZE_MAX)), sgn(cplus_strncmp(s1, s2, SIZE_MAX)));

        int hs = sgn(strcmp(s1, s2)), cs = sgn(cplus_strcmp(s1, s2));
        if (hs != cs) {
            report("string", "strcmp", "host=%d cplus=%d", hs, cs);
            print_bytes("s1", (const unsigned char *)s1, n1);
            print_bytes("s2", (const unsigned char *)s2, n2);
            printf("\n");
        }
        hs = sgn(strncmp(s1, s2, bound));
        cs = sgn(cplus_strncmp(s1, s2, bound));
        if (hs != cs) {
            report("string", "strncmp", "bound=%zu host=%d cplus=%d", bound, hs, cs);
            print_bytes("s1", (const unsigned char *)s1, n1);
            print_bytes("s2", (const unsigned char *)s2, n2);
            printf("\n");
        }

        int target;
        switch (rnd(3u)) {
            case 0: target = (int)rand_byte(); break;                 /* 0..255 */
            case 1: target = n1 ? (int)(unsigned char)s1[rnd((unsigned)n1)] : 0; break;
            default: target = '\0'; break;                            /* NUL case */
        }
        char *hc = strchr(s1, target), *cc = cplus_strchr(s1, target);
        if (hc != cc) {
            report("string", "strchr", "target=%d host_off=%ld cplus_off=%ld",
                   target, hc ? (long)(hc - s1) : -1L,
                   cc ? (long)(cc - s1) : -1L);
            print_bytes("s1", (const unsigned char *)s1, n1);
            printf("\n");
        }
        /* int -> char conversion: values > UCHAR_MAX must behave identically. */
        char *hc2 = strchr(s1, target + 256);
        char *cc2 = cplus_strchr(s1, target + 256);
        if (hc2 != cc2)
            report("string", "strchr", "target+256=%d host_off=%ld cplus_off=%ld",
                   target + 256, hc2 ? (long)(hc2 - s1) : -1L,
                   cc2 ? (long)(cc2 - s1) : -1L);
        hc = strrchr(s1, target);
        cc = cplus_strrchr(s1, target);
        if (hc != cc) {
            report("string", "strrchr", "target=%d host_off=%ld cplus_off=%ld",
                   target, hc ? (long)(hc - s1) : -1L,
                   cc ? (long)(cc - s1) : -1L);
            print_bytes("s1", (const unsigned char *)s1, n1);
            printf("\n");
        }

        size_t nn = gen_needle(needle, sizeof needle - 1u, s1, n1);
        hc = strstr(s1, needle);
        cc = cplus_strstr(s1, needle);
        if (hc != cc) {
            report("string", "strstr", "host_off=%ld cplus_off=%ld",
                   hc ? (long)(hc - s1) : -1L, cc ? (long)(cc - s1) : -1L);
            print_bytes("s1", (const unsigned char *)s1, n1);
            print_bytes("needle", (const unsigned char *)needle, nn);
            printf("\n");
        }

        /* strncpy: destination 64 bytes, n <= 32, with canaries. */
        {
            str_dst hd, cd;
            guard_init(hd.pre); guard_init(hd.post);
            guard_init(cd.pre); guard_init(cd.post);
            memset(hd.b, 0x5A, sizeof hd.b);
            memset(cd.b, 0x5A, sizeof cd.b);
            size_t ncp = (size_t)rnd(33u);
            strncpy(hd.b, s1, ncp);
            cplus_strncpy(cd.b, s1, ncp);
            if (memcmp(hd.b, cd.b, sizeof hd.b) != 0 ||
                !guard_ok(hd.pre) || !guard_ok(hd.post) ||
                !guard_ok(cd.pre) || !guard_ok(cd.post)) {
                report("string", "strncpy", "n=%zu guards=(%d%d%d%d) bytes_differ=%d",
                       ncp, guard_ok(hd.pre), guard_ok(hd.post),
                       guard_ok(cd.pre), guard_ok(cd.post),
                       memcmp(hd.b, cd.b, sizeof hd.b) != 0);
                print_bytes("s1", (const unsigned char *)s1, n1);
                printf("\n");
            }
        }

        /* strcat: dest 128 bytes, prefix <= 16, append <= 32. */
        {
            cat_dst hd, cd;
            char prefix[20];
            size_t np = gen_str(prefix, sizeof prefix - 1u, 16u);
            guard_init(hd.pre); guard_init(hd.post);
            guard_init(cd.pre); guard_init(cd.post);
            memset(hd.b, 0x66, sizeof hd.b);
            memset(cd.b, 0x66, sizeof cd.b);
            strcpy(hd.b, prefix);
            strcpy(cd.b, prefix);
            strcat(hd.b, s2);
            cplus_strcat(cd.b, s2);
            if (memcmp(hd.b, cd.b, sizeof hd.b) != 0 ||
                !guard_ok(hd.pre) || !guard_ok(hd.post) ||
                !guard_ok(cd.pre) || !guard_ok(cd.post)) {
                report("string", "strcat", "prefix_len=%zu append_len=%zu guards=(%d%d%d%d)",
                       np, n2, guard_ok(hd.pre), guard_ok(hd.post),
                       guard_ok(cd.pre), guard_ok(cd.post));
                print_bytes("prefix", (const unsigned char *)prefix, np);
                print_bytes("s2", (const unsigned char *)s2, n2);
                printf("\n");
            }
        }
    }
}

static void memory_family(unsigned long iters) {
    for (unsigned long it = 0; it < iters; ++it) {
        unsigned char src[128], a[128], b[128];
        for (size_t i = 0; i < 128u; ++i) {
            src[i] = rand_byte();
            a[i] = rand_byte();
            b[i] = rand_byte();
        }

        /* memcpy: source and destination are distinct objects (no overlap). */
        {
            mem_dst hd, cd;
            size_t len = (size_t)rnd(129u);
            guard_init(hd.pre); guard_init(hd.post);
            guard_init(cd.pre); guard_init(cd.post);
            memset(hd.b, 0x3C, sizeof hd.b);
            memset(cd.b, 0x3C, sizeof cd.b);
            memcpy(hd.b, src, len);
            cplus_memcpy(cd.b, src, len);
            if (memcmp(hd.b, cd.b, sizeof hd.b) != 0 ||
                !guard_ok(hd.pre) || !guard_ok(hd.post) ||
                !guard_ok(cd.pre) || !guard_ok(cd.post)) {
                report("memory", "memcpy", "len=%zu guards=(%d%d%d%d)", len,
                       guard_ok(hd.pre), guard_ok(hd.post),
                       guard_ok(cd.pre), guard_ok(cd.post));
                print_bytes("src", src, len);
                printf("\n");
            }
        }

        /* memmove: overlapping source/destination is legal and intentional. */
        {
            mem_dst hd, cd;
            unsigned char seed[128];
            for (size_t i = 0; i < 128u; ++i) seed[i] = rand_byte();
            size_t off = (size_t)rnd(16u);
            size_t mlen = (size_t)rnd((unsigned)(128 - off + 1u));
            int upward = (int)rnd(2u); /* dest > src vs dest < src */
            guard_init(hd.pre); guard_init(hd.post);
            guard_init(cd.pre); guard_init(cd.post);
            memcpy(hd.b, seed, 128u);
            memcpy(cd.b, seed, 128u);
            if (upward) {
                memmove(hd.b + off, hd.b, mlen);
                cplus_memmove(cd.b + off, cd.b, mlen);
            } else {
                memmove(hd.b, hd.b + off, mlen);
                cplus_memmove(cd.b, cd.b + off, mlen);
            }
            if (memcmp(hd.b, cd.b, sizeof hd.b) != 0 ||
                !guard_ok(hd.pre) || !guard_ok(hd.post) ||
                !guard_ok(cd.pre) || !guard_ok(cd.post)) {
                report("memory", "memmove", "off=%zu len=%zu upward=%d guards=(%d%d%d%d)",
                       off, mlen, upward, guard_ok(hd.pre), guard_ok(hd.post),
                       guard_ok(cd.pre), guard_ok(cd.post));
                printf("\n");
            }
        }

        /* memcmp: sometimes equal, sometimes different. */
        {
            size_t clen = (size_t)rnd(129u);
            if (rnd(2u)) memcpy(b, a, 128u);
            int hs = sgn(memcmp(a, b, clen));
            int cs = sgn(cplus_memcmp(a, b, clen));
            if (hs != cs) {
                report("memory", "memcmp", "len=%zu host=%d cplus=%d", clen, hs, cs);
                print_bytes("a", a, clen > 0u ? clen : 1u);
                print_bytes("b", b, clen > 0u ? clen : 1u);
                printf("\n");
            }
        }

        /* memchr / memrchr over the same 128-byte object. */
        {
            int c = rnd(2u) ? (int)a[rnd(128u)] : (int)rand_byte();
            void *hh = memchr(a, c, 128u);
            void *ch = cplus_memchr(a, c, 128u);
            if (hh != ch) {
                report("memory", "memchr", "c=%d host_off=%ld cplus_off=%ld", c,
                       hh ? (long)((unsigned char *)hh - a) : -1L,
                       ch ? (long)((unsigned char *)ch - a) : -1L);
                printf("\n");
            }
            void *hr = ref_memrchr(a, c, 128u);
            void *cr = cplus_memrchr(a, c, 128u);
            if (hr != cr) {
                report("memory", "memrchr", "c=%d ref_off=%ld cplus_off=%ld", c,
                       hr ? (long)((unsigned char *)hr - a) : -1L,
                       cr ? (long)((unsigned char *)cr - a) : -1L);
                printf("\n");
            }
            /* Explicit NUL target (rare with uniform byte sampling). */
            void *hz = memchr(a, 0, 128u);
            void *cz = cplus_memchr(a, 0, 128u);
            if (hz != cz)
                report("memory", "memchr", "c=0 host_off=%ld cplus_off=%ld",
                       hz ? (long)((unsigned char *)hz - a) : -1L,
                       cz ? (long)((unsigned char *)cz - a) : -1L);
        }
    }
}

static void ctype_family(void) {
    for (int c = 0; c < 256; ++c) {
        if ((isalpha(c) != 0) != (cplus_isalpha(c) != 0))
            report("ctype", "isalpha", "c=%d host=%d cplus=%d", c, isalpha(c) != 0, cplus_isalpha(c) != 0);
        if ((isdigit(c) != 0) != (cplus_isdigit(c) != 0))
            report("ctype", "isdigit", "c=%d host=%d cplus=%d", c, isdigit(c) != 0, cplus_isdigit(c) != 0);
        if ((isspace(c) != 0) != (cplus_isspace(c) != 0))
            report("ctype", "isspace", "c=%d host=%d cplus=%d", c, isspace(c) != 0, cplus_isspace(c) != 0);
        if (toupper(c) != cplus_toupper(c))
            report("ctype", "toupper", "c=%d host=%d cplus=%d", c, toupper(c), cplus_toupper(c));
        if (tolower(c) != cplus_tolower(c))
            report("ctype", "tolower", "c=%d host=%d cplus=%d", c, tolower(c), cplus_tolower(c));
    }
}

/* Finite pseudo-random double in [-scale, scale]. */
static double rand_double(double scale) {
    uint32_t u = xr32();
    return (((double)u / 4294967295.0) * 2.0 - 1.0) * scale;
}

static void cmp_d(const char *fn, double h, double c, double x, double y) {
    int hn = isnan(h), cn = isnan(c);
    int hi = isinf(h), ci = isinf(c);
    if (hn || cn) {
        if (hn != cn)
            report("math", fn, "x=%a y=%a host=%a cplus=%a (NaN classification)",
                   x, y, h, c);
        return;
    }
    if (hi || ci) {
        if (hi != ci || h != c)
            report("math", fn, "x=%a y=%a host=%a cplus=%a (Inf classification)",
                   x, y, h, c);
        return;
    }
    if (h == c && signbit(h) != signbit(c))
        report("math", fn, "x=%a y=%a host=%a cplus=%a (signed-zero mismatch)", x, y, h, c);
    double tol = 1e-12 * (1.0 + fabs(h));
    if (fabs(h - c) > tol)
        report("math", fn, "x=%a y=%a host=%a cplus=%a diff=%a", x, y, h, c, h - c);
}

static void math_family(unsigned long iters) {
    /* Domain/classification edge inputs, still all generated deterministically. */
    static const double specials[] = {
        NAN, INFINITY, -INFINITY, 0.0, -0.0,
        DBL_MAX, -DBL_MAX, DBL_MIN, -DBL_MIN
    };
    const size_t nspecial = sizeof specials / sizeof specials[0];
    for (size_t i = 0; i < nspecial; ++i) {
        double v = specials[i];
        cmp_d("fabs_special",  fabs(v),          cplus_fabs(v),          v, 0.0);
        cmp_d("sqrt_special",  sqrt(v),          cplus_sqrt(v),          v, 0.0);
        cmp_d("sin_special",   sin(v),           cplus_sin(v),           v, 0.0);
        cmp_d("cos_special",   cos(v),           cplus_cos(v),           v, 0.0);
        cmp_d("exp_special",   exp(v),           cplus_exp(v),           v, 0.0);
        cmp_d("log_special",   log(v),           cplus_log(v),           v, 0.0);
        cmp_d("floor_special", floor(v),         cplus_floor(v),         v, 0.0);
        cmp_d("ceil_special",  ceil(v),          cplus_ceil(v),          v, 0.0);
        cmp_d("fmod_special",  fmod(v, 3.0),     cplus_fmod(v, 3.0),     v, 3.0);
        cmp_d("pow_special",   pow(v, 2.0),      cplus_pow(v, 2.0),      v, 2.0);
        cmp_d("pow_special",   pow(2.0, v),      cplus_pow(2.0, v),      2.0, v);
    }

    for (unsigned long it = 0; it < iters; ++it) {
        double x = rand_double(50.0);
        double y = rand_double(4.0);
        cmp_d("fabs",  fabs(x),          cplus_fabs(x),          x, y);
        cmp_d("sqrt",  sqrt(x),          cplus_sqrt(x),          x, y);
        cmp_d("sin",   sin(x),           cplus_sin(x),           x, y);
        cmp_d("cos",   cos(x),           cplus_cos(x),           x, y);
        cmp_d("exp",   exp(x),           cplus_exp(x),           x, y);
        cmp_d("log",   log(x),           cplus_log(x),           x, y);
        cmp_d("floor", floor(x),         cplus_floor(x),         x, y);
        cmp_d("ceil",  ceil(x),          cplus_ceil(x),          x, y);
        cmp_d("fmod",  fmod(x, y),       cplus_fmod(x, y),       x, y);
        cmp_d("pow",   pow(x, y),        cplus_pow(x, y),        x, y);
    }
}

static void check_int(const char *fn, const char *s, int base,
                      long long hv, long long cv, ptrdiff_t ho, ptrdiff_t co,
                      int herr, int cerr, int uns) {
    if (hv == cv && ho == co && herr == cerr) return;
    if (uns)
        report("conv", fn, "s=\"%s\" base=%d host=(%llu,off=%td,errno=%d) cplus=(%llu,off=%td,errno=%d)",
               s, base, (unsigned long long)hv, ho, herr,
               (unsigned long long)cv, co, cerr);
    else
        report("conv", fn, "s=\"%s\" base=%d host=(%lld,off=%td,errno=%d) cplus=(%lld,off=%td,errno=%d)",
               s, base, hv, ho, herr, cv, co, cerr);
}

static void check_fp(const char *fn, const char *s, double h, double c,
                     ptrdiff_t ho, ptrdiff_t co, int herr, int cerr) {
    if (ho != co || herr != cerr)
        report("conv", fn, "s=\"%s\" host=(off=%td,errno=%d) cplus=(off=%td,errno=%d)",
               s, ho, herr, co, cerr);
    cmp_d(fn, h, c, 0.0, 0.0);
}

static void conv_family(unsigned long iters) {
    for (unsigned long it = 0; it < iters; ++it) {
        char s[96];
        int base;
        char *he, *ce;
        ptrdiff_t ho, co;
        int herr, cerr;

        gen_int_str(s, sizeof s, &base);

        errno = 0; {
            long v = strtol(s, &he, base); herr = errno; ho = he - s;
            errno = 0;
            long w = cplus_strtol(s, &ce, base); cerr = errno; co = ce - s;
            check_int("strtol", s, base, (long long)v, (long long)w, ho, co, herr, cerr, 0);
        }
        errno = 0; {
            unsigned long v = strtoul(s, &he, base); herr = errno; ho = he - s;
            errno = 0;
            unsigned long w = cplus_strtoul(s, &ce, base); cerr = errno; co = ce - s;
            check_int("strtoul", s, base, (long long)v, (long long)w, ho, co, herr, cerr, 1);
        }
        errno = 0; {
            long long v = strtoll(s, &he, base); herr = errno; ho = he - s;
            errno = 0;
            long long w = cplus_strtoll(s, &ce, base); cerr = errno; co = ce - s;
            check_int("strtoll", s, base, v, w, ho, co, herr, cerr, 0);
        }
        errno = 0; {
            unsigned long long v = strtoull(s, &he, base); herr = errno; ho = he - s;
            errno = 0;
            unsigned long long w = cplus_strtoull(s, &ce, base); cerr = errno; co = ce - s;
            check_int("strtoull", s, base, (long long)v, (long long)w, ho, co, herr, cerr, 1);
        }

        gen_float_str(s, sizeof s);
        errno = 0; {
            double v = strtod(s, &he); herr = errno; ho = he - s;
            errno = 0;
            double w = cplus_strtod(s, &ce); cerr = errno; co = ce - s;
            check_fp("strtod", s, v, w, ho, co, herr, cerr);
        }
        errno = 0; {
            float v = strtof(s, &he); herr = errno; ho = he - s;
            errno = 0;
            float w = cplus_strtof(s, &ce); cerr = errno; co = ce - s;
            check_fp("strtof", s, (double)v, (double)w, ho, co, herr, cerr);
        }
    }
}

static void wchar_family(unsigned long iters) {
    for (unsigned long it = 0; it < iters; ++it) {
        wchar_t a[24], b[24];
        size_t na = gen_wstr(a, sizeof a / sizeof a[0] - 1u, 16u);
        size_t nb = gen_wstr(b, sizeof b / sizeof b[0] - 1u, 16u);
        if (rnd(4u) == 0u) { /* equal-string case */
            wcsncpy(b, a, 24u);
            nb = na;
        }
        size_t h = wcslen(a), c = cplus_wcslen(a);
        if (h != c)
            report("wchar", "wcslen", "na=%zu host=%zu cplus=%zu", na, h, c);
        int hs = sgn(wcscmp(a, b)), cs = sgn(cplus_wcscmp(a, b));
        if (hs != cs) {
            report("wchar", "wcscmp", "na=%zu nb=%zu host=%d cplus=%d first_a=%lu first_b=%lu",
                   na, nb, hs, cs, (unsigned long)a[0], (unsigned long)b[0]);
        }
    }
}

/* ------------------------------------------------------------------ main -- */
int main(int argc, char **argv) {
    uint32_t seed = 0x13579BDFu;
    if (argc > 1) {
        char *end = NULL;
        unsigned long v = strtoul(argv[1], &end, 0);
        if (end != argv[1] && *end == '\0') seed = (uint32_t)v;
    }
    rng_seed(seed);

    const unsigned long NSTR = 3000u, NMEM = 3000u, NMATH = 3000u;
    const unsigned long NCONV = 3000u, NWCHAR = 1000u;
    const unsigned long total = NSTR + NMEM + 256u + NMATH + NCONV + NWCHAR;

    string_family(NSTR);
    memory_family(NMEM);
    ctype_family();
    math_family(NMATH);
    conv_family(NCONV);
    wchar_family(NWCHAR);

    if (g_divergences) {
        printf("differential_attack: FAIL (iterations=%lu divergences=%lu seed=0x%08x)\n",
               total, g_divergences, seed);
        return 1;
    }
    printf("differential_attack: ok (iterations=%lu; string=%lu memory=%lu ctype=256 math=%lu conversions=%lu wchar=%lu; seed=0x%08x)\n",
           total, NSTR, NMEM, NMATH, NCONV, NWCHAR, seed);
    return 0;
}
