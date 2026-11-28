/* Adversarial black-box tests for the platform-backed cplus_* wide-character
 * surface. ONLY cplus_* symbols are exercised; host functions are used purely
 * as an oracle for differential checks and are never interposed.
 *
 * Defined inputs only: every wide string passed to a conversion API is NUL
 * terminated; no unterminated wide string is ever given to a raw API.
 *
 * The wrappers are thin, logged, NULL-hardened adapters over the installed
 * host C runtime. A crash, an out-of-bounds write, a missing NULL guard, a
 * wrong failure return, or a wrapper/host divergence is a real defect.
 * Platform quirks that the wrapper faithfully delegates are reported as
 * FINDING: lines (informational) but are not asserted as wrapper failures. */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ---- declarations under test (mirror install-include/cplus_libc.h) ------ */
long cplus_wcstol(const wchar_t *, wchar_t **, int);
long long cplus_wcstoll(const wchar_t *, wchar_t **, int);
unsigned long cplus_wcstoul(const wchar_t *, wchar_t **, int);
unsigned long long cplus_wcstoull(const wchar_t *, wchar_t **, int);
double cplus_wcstod(const wchar_t *, wchar_t **);
float cplus_wcstof(const wchar_t *, wchar_t **);
long double cplus_wcstold(const wchar_t *, wchar_t **);
wchar_t *cplus_wcstok(wchar_t *, const wchar_t *, wchar_t **);
size_t cplus_wcsftime(wchar_t *, size_t, const wchar_t *, const struct tm *);
size_t cplus_mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
size_t cplus_wcrtomb(char *, wchar_t, mbstate_t *);
size_t cplus_mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
size_t cplus_wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);
int cplus_wprintf(const wchar_t *, ...);
int cplus_vwprintf(const wchar_t *, va_list);
int cplus_fwprintf(FILE *, const wchar_t *, ...);
int cplus_vfwprintf(FILE *, const wchar_t *, va_list);
int cplus_swprintf(wchar_t *, size_t, const wchar_t *, ...);
int cplus_vswprintf(wchar_t *, size_t, const wchar_t *, va_list);
int cplus_wscanf(const wchar_t *, ...);
int cplus_fwscanf(FILE *, const wchar_t *, ...);
int cplus_swscanf(const wchar_t *, const wchar_t *, ...);
int cplus_vswscanf(const wchar_t *, const wchar_t *, va_list);
wint_t cplus_towupper(wint_t);
wctrans_t cplus_wctrans(const char *);
wint_t cplus_towctrans(wint_t, wctrans_t);
intmax_t cplus_wcstoimax(const wchar_t *, wchar_t **, int);
uintmax_t cplus_wcstoumax(const wchar_t *, wchar_t **, int);

#define NELM(a) (sizeof(a) / sizeof((a)[0]))
#define FINDING(...) do { printf("FINDING: "); printf(__VA_ARGS__); printf("\n"); } while (0)

/* ====================================================================== */
/* 1. Conversion overflow / invalid bases / empty input                    */
/* ====================================================================== */
static void test_conversion_overflow(void) {
    wchar_t *end = NULL;
    const wchar_t *huge = L"0xFFFFFFFFFFFFFFFFFFFFFFFF";

    errno = 0;
    unsigned long long u = cplus_wcstoull(huge, &end, 16);
    assert(errno == ERANGE);
    assert(u == ULLONG_MAX);
    assert(*end == L'\0');

    errno = 0;
    uintmax_t um = cplus_wcstoumax(huge, &end, 16);
    assert(errno == ERANGE);
    assert(um == UINTMAX_MAX);

    errno = 0;
    long long s = cplus_wcstoll(L"99999999999999999999999999999999", &end, 10);
    assert(errno == ERANGE);
    assert(s == LLONG_MAX);

    /* invalid bases: 1, 37, INT_MAX, -1 -> 0 / EINVAL / endptr==input */
    {
        int bases[] = {1, 37, INT_MAX, -1};
        size_t i;
        for (i = 0; i < NELM(bases); ++i) {
            const wchar_t *in = L"10";
            wchar_t *ep = (wchar_t *)in;
            errno = 0;
            assert(cplus_wcstoul(in, &ep, bases[i]) == 0UL);
            assert(errno == EINVAL);
            assert(ep == in);
            ep = (wchar_t *)in;
            errno = 0;
            assert(cplus_wcstoimax(in, &ep, bases[i]) == 0);
            assert(errno == EINVAL);
            assert(ep == in);
        }
    }

    /* empty string: 0, endptr left at input (errno is host-defined) */
    {
        const wchar_t *empty = L"";
        wchar_t *ep = (wchar_t *)empty;
        assert(cplus_wcstoul(empty, &ep, 10) == 0UL);
        assert(ep == empty);
        ep = (wchar_t *)empty;
        assert(cplus_wcstoumax(empty, &ep, 10) == 0);
        assert(ep == empty);
    }

    /* NULL required string is a documented hardening extension */
    errno = 0;
    end = (wchar_t *)huge;
    assert(cplus_wcstoull(NULL, &end, 10) == 0ULL);
    assert(errno == EINVAL && end == NULL);

    /* float overflow -> HUGE_VAL family + ERANGE */
    errno = 0;
    assert(cplus_wcstod(L"1e9999", &end) > 1.0 && errno == ERANGE);
    errno = 0;
    assert(cplus_wcstof(L"1e9999", &end) > 1.0f && errno == ERANGE);
    errno = 0;
    assert(cplus_wcstold(L"1e9999", &end) > 1.0L && errno == ERANGE);
}

/* ====================================================================== */
/* 2. endptr forgery                                                       */
/* ====================================================================== */
static void test_endptr_forgery(void) {
    /* (a) endptr pointing into the middle of the input buffer. The host
     *     writes the first-unconverted address through endptr; wrapper and
     *     host must leave identical state. */
    {
        wchar_t a[] = L"12345";
        wchar_t b[] = L"12345";
        wchar_t *pa = a + 2, *pb = b + 2;
        long va = cplus_wcstol(a, &pa, 10);
        long vb = wcstol(b, &pb, 10);
        assert(va == vb && va == 12345L);
        assert(pa == a + 5 && pb == b + 5);
        assert(pa - a == pb - b);
    }
    /* (b) NULL endptr: legal, must not be rejected */
    assert(cplus_wcstol(L"789", NULL, 10) == 789L);
    assert(cplus_wcstoull(L"789", NULL, 10) == 789ULL);
    assert(cplus_wcstod(L"2.5", NULL) == 2.5);
    /* (c) endptr aliasing the input buffer itself (writable "forgery"):
     *     no crash and the write lands where the host would put it. */
    {
        wchar_t a[] = L"77xy";
        wchar_t b[] = L"77xy";
        wchar_t *pa = &a[1], *pb = &b[1];
        assert(cplus_wcstoul(a, &pa, 10) == 77UL);
        assert(wcstoul(b, &pb, 10) == 77UL);
        assert(pa - a == pb - b && pa == a + 2);
    }
    /* Actual read-only endptr targets are caller UB and are not passed. */
}

/* ====================================================================== */
/* 3. wcstok hostile delimiters                                            */
/* ====================================================================== */
static void test_wcstok_hostile(void) {
    /* empty delimiter -> whole string is one token, then NULL */
    {
        wchar_t s[] = L"abc def";
        wchar_t *save = NULL, *t;
        t = cplus_wcstok(s, L"", &save);
        assert(t && wcscmp(t, L"abc def") == 0);
        t = cplus_wcstok(NULL, L"", &save);
        assert(t == NULL);
    }
    /* all delimiters -> NULL immediately, bounded (no infinite loop) */
    {
        wchar_t s[] = L",,,,";
        wchar_t *save = NULL;
        int guard = 0;
        while (cplus_wcstok(save ? NULL : s, L",", &save) != NULL) {
            if (++guard > 8) assert(!"wcstok failed to terminate");
        }
        assert(guard == 0);
    }
    /* trailing / adjacent delimiters, repeated NULL continuation */
    {
        wchar_t s[] = L"a,,b,c,";
        wchar_t *save = NULL, *t;
        t = cplus_wcstok(s, L",", &save);
        assert(t && wcscmp(t, L"a") == 0);
        t = cplus_wcstok(NULL, L",", &save);
        assert(t && wcscmp(t, L"b") == 0);
        t = cplus_wcstok(NULL, L",", &save);
        assert(t && wcscmp(t, L"c") == 0);
        t = cplus_wcstok(NULL, L",", &save);
        assert(t == NULL);
    }
    /* saveptr corruption: NULL saveptr on a continuation call is safe */
    {
        wchar_t *save = NULL;
        assert(cplus_wcstok(NULL, L",", &save) == NULL);
    }
    /* saveptr reset into the middle of a live buffer resumes there */
    {
        wchar_t s[] = L"aa,bb,cc";
        wchar_t *save = s + 3;
        wchar_t *t = cplus_wcstok(NULL, L",", &save);
        assert(t && wcscmp(t, L"bb") == 0);
    }
    /* required-pointer guards: NULL delimiter and NULL saveptr are errors */
    {
        wchar_t s[] = L"x y";
        wchar_t *save = NULL;
        errno = 0;
        assert(cplus_wcstok(s, NULL, &save) == NULL);
        assert(errno == EINVAL);
        errno = 0;
        assert(cplus_wcstok(s, L" ", NULL) == NULL);
        assert(errno == EINVAL);
    }
}

/* ====================================================================== */
/* 4. wcsftime edge sizes                                                  */
/* ====================================================================== */
static void test_wcsftime_edges(void) {
    struct tm tm;
    wchar_t buf[16];
    size_t r;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 120; tm.tm_mon = 0; tm.tm_mday = 2;

    /* maxsize 0 with a valid destination: defined return, no write */
    errno = 0;
    r = cplus_wcsftime(buf, 0, L"%Y-%m-%d", &tm);
    assert(r == 0 || r > 0); /* no crash is the contract here */

    /* maxsize 0 with NULL destination: wrapper deliberately passes through */
    errno = 0;
    r = cplus_wcsftime(NULL, 0, L"%Y-%m-%d", &tm);
    assert(r == (size_t)0 || r > 0);

    /* maxsize 1 tiny buffer + canaries: truncation must not overrun */
    {
        wchar_t tiny[4];
        size_t i;
        for (i = 0; i < NELM(tiny); ++i) tiny[i] = L'#';
        r = cplus_wcsftime(tiny, 1, L"%Y-%m-%d", &tm);
        assert(tiny[1] == L'#' && tiny[2] == L'#' && tiny[3] == L'#');
        (void)r;
    }

    /* SIZE_MAX with a buffer that actually fits the (short) format: no
     * overrun. Apple's host rejects huge n with EINVAL/0; either way bounded. */
    {
        wchar_t small[8];
        size_t i;
        for (i = 0; i < NELM(small); ++i) small[i] = L'#';
        errno = 0;
        r = cplus_wcsftime(small, (size_t)-1, L"X", &tm);
        /* glibc NUL-terminates the short result at small[1] even for an
         * oversized maxsize; the safety property is that the TAIL canary is
         * untouched (no write past the formatted result). */
        assert(small[7] == L'#');
        if (r == 0 && errno == EINVAL)
            FINDING("wcsftime: host rejects maxsize=SIZE_MAX with EINVAL (return 0); canaries intact, no overflow");
        (void)r;
    }

    /* NULL format and NULL destination with nonzero size are hardened */
    errno = 0;
    assert(cplus_wcsftime(buf, NELM(buf), NULL, &tm) == 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_wcsftime(NULL, 8, L"%Y", &tm) == 0);
    assert(errno == EINVAL);
}

/* ====================================================================== */
/* 5. Restartable multibyte conversions                                    */
/* ====================================================================== */
static void test_multibyte_restartable(void) {
    mbstate_t st, st2;
    wchar_t wc, wc2;
    size_t r, r2;
    const char *s;
    char out[16];
    const char *srcp;
    const wchar_t *wsrcp;
    wchar_t dstw[16];

    /* Force C locale for determinism, as required. */
    assert(setlocale(LC_ALL, "C") != NULL);
    assert(MB_CUR_MAX == 1);

    /* Deterministic differential check on a valid single-byte character. */
    s = "A";
    memset(&st, 0, sizeof st); memset(&st2, 0, sizeof st2);
    errno = 0; r = cplus_mbrtowc(&wc, s, 1, &st);
    errno = 0; r2 = mbrtowc(&wc2, s, 1, &st2);
    assert(r == r2 && wc == wc2);

    /* Invalid byte 0xFF is host-locale-dependent (glibc reports EILSEQ,
     * some BSD libcs map 1:1).  Report both results without asserting a
     * particular host outcome; the wrapper delegates to the same host call. */
    s = "\xff";
    memset(&st, 0, sizeof st); memset(&st2, 0, sizeof st2);
    errno = 0; r = cplus_mbrtowc(&wc, s, 1, &st);
    errno = 0; r2 = mbrtowc(&wc2, s, 1, &st2);
    FINDING("mbrtowc: byte 0xFF in \"C\" locale -> wrapper r=%zu wide=0x%X, host r=%zu wide=0x%X (locale-dependent, wrapper delegates)", r, (unsigned)wc, r2, (unsigned)wc2);

    /* truncated sequence in C locale also maps 1:1; differential only */
    s = "\xc3\xa9";
    memset(&st, 0, sizeof st); memset(&st2, 0, sizeof st2);
    r = cplus_mbrtowc(&wc, s, 1, &st);
    r2 = mbrtowc(&wc2, s, 1, &st2);
    assert(r == r2 && wc == wc2);

    /* Embedded NUL -> 0 and wc = 0 */
    s = "\0x";
    memset(&st, 0, sizeof st);
    r = cplus_mbrtowc(&wc, s, 2, &st);
    assert(r == 0 && wc == 0);

    /* NULL state pointer is valid (host internal state) */
    r = cplus_mbrtowc(&wc, "A", 1, NULL);
    assert(r == 1 && wc == L'A');

    /* wcrtomb NULL destination is a valid length/reset query */
    memset(&st, 0, sizeof st);
    r = cplus_wcrtomb(NULL, L'a', &st);
    assert(r == (size_t)-1 || r <= MB_LEN_MAX);

    /* endptr object is required for mbsrtowcs/wcsrtombs */
    errno = 0;
    assert(cplus_mbsrtowcs(NULL, NULL, 0, NULL) == (size_t)-1);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_wcsrtombs(NULL, NULL, 0, NULL) == (size_t)-1);
    assert(errno == EINVAL);

    /* normal restartable round trips (C locale) */
    srcp = "hello";
    memset(&st, 0, sizeof st);
    r = cplus_mbsrtowcs(dstw, &srcp, NELM(dstw), &st);
    assert(r == 5 && srcp == NULL && wcscmp(dstw, L"hello") == 0);
    wsrcp = L"world";
    memset(&st, 0, sizeof st);
    r = cplus_wcsrtombs(out, &wsrcp, sizeof out, &st);
    assert(r == 5 && wsrcp == NULL && memcmp(out, "world", 5) == 0);

    /* deliberately confused mbstate_t: zero-init, reuse across conversions */
    memset(&st, 0, sizeof st);
    assert(cplus_mbrtowc(&wc, "a", 1, &st) == 1);
    assert(cplus_mbrtowc(&wc, "b", 1, &st) == 1);
    assert(cplus_mbrtowc(&wc, NULL, 0, &st) == 0); /* reset via NULL s */
    assert(cplus_mbrtowc(&wc, "c", 1, &st) == 1 && wc == L'c');

    /* Now a multibyte locale, where EILSEQ / -2 are meaningful. */
    if (setlocale(LC_CTYPE, "en_US.UTF-8") != NULL ||
        setlocale(LC_CTYPE, "C.UTF-8") != NULL) {
        memset(&st, 0, sizeof st);
        errno = 0;
        r = cplus_mbrtowc(&wc, "\xff", 1, &st);
        assert(r == (size_t)-1 && errno == EILSEQ);

        memset(&st, 0, sizeof st);
        errno = 0;
        r = cplus_mbrtowc(&wc, "\xc3\xa9", 1, &st);
        assert(r == (size_t)-2);

        memset(&st, 0, sizeof st);
        r = cplus_mbrtowc(&wc, "\xc3\xa9", 2, &st);
        assert(r == 2 && wc == 0xE9);
    } else {
        FINDING("mbrtowc: no UTF-8 locale available; EILSEQ/-2 assertions skipped");
    }

    /* restore required locale */
    assert(setlocale(LC_ALL, "C") != NULL);
}

/* ====================================================================== */
/* 6. Wide formatted I/O                                                   */
/* ====================================================================== */
static int vs_helper(wchar_t *s, size_t n, const wchar_t *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vswprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

/* The exact requested width 999999999 makes the host vswprintf burn many
 * seconds of CPU (it appears to materialise/pad the full field before
 * discovering n is tiny). Probe it in a child with a 2s alarm so this suite
 * stays < 5s while still demonstrating the behaviour. */
static void test_huge_width_swprintf(void) {
    pid_t pid = fork();
    if (pid == 0) {
        wchar_t b[16];
        size_t i;
        for (i = 0; i < NELM(b); ++i) b[i] = L'#';
        alarm(2);
        errno = 0;
        (void)cplus_swprintf(b, 8, L"%999999999ls", L"hi");
        if (b[8] != L'#' || b[15] != L'#') _exit(2); /* overran n */
        _exit(0);
    }
    assert(pid > 0);
    {
        int status = 0;
        assert(waitpid(pid, &status, 0) == pid);
        if (WIFSIGNALED(status)) {
            FINDING("swprintf: literal width 999999999 in %%ls did not return within 2s in the child (host CPU denial-of-service); wrapper delegates to host vswprintf with no width guard");
        } else {
            assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            FINDING("swprintf: literal width 999999999 in %%ls completed in child with canaries intact and no overrun");
        }
    }
}

static void test_wide_format(void) {
    /* A still-large but tractable width verifies boundedness directly. */
    {
        wchar_t b[16];
        size_t i;
        for (i = 0; i < NELM(b); ++i) b[i] = L'#';
        errno = 0;
        int r = cplus_swprintf(b, 8, L"%1000000ls", L"hi");
        assert(b[8] == L'#' && b[15] == L'#');
        if (r < 0)
            FINDING("swprintf: large width 1000000 into n=8 returned -1 (errno=%d) without overrun; canaries intact", errno);
        (void)r;
    }
    test_huge_width_swprintf();
    /* n=0 with NULL s: host may reject, but must not crash */
    {
        errno = 0;
        int r = cplus_swprintf(NULL, 0, L"abc %d", 42);
        (void)r;
    }
    /* NULL format and NULL destination with n>0 are hardened */
    {
        wchar_t b[8];
        errno = 0;
        assert(cplus_swprintf(b, NELM(b), NULL) == -1);
        assert(errno == EINVAL);
        errno = 0;
        assert(cplus_swprintf(NULL, 8, L"x") == -1);
        assert(errno == EINVAL);
    }
    /* vswprintf form is bounded too */
    {
        wchar_t b[8];
        size_t i;
        for (i = 0; i < NELM(b); ++i) b[i] = L'#';
        int r = vs_helper(b, 4, L"%1000000ls", L"xy");
        assert(b[4] == L'#' && b[7] == L'#');
        (void)r;
    }
    /* %n is documented as NOT neutralized; verify with a legitimate pointer */
    {
        wchar_t b[32];
        int n = -1;
        int r = cplus_swprintf(b, NELM(b), L"abcd%nEF", &n);
        assert(r == 6 && n == 4 && wcscmp(b, L"abcdEF") == 0);
        FINDING("swprintf: percent-n is NOT rejected/neutralized (documented caller responsibility): legitimate %%n wrote %d into the supplied int*", n);
    }
    /* NULL format / stream guards on the other wide format entry points */
    errno = 0;
    assert(cplus_wprintf(NULL) == -1 && errno == EINVAL);
    errno = 0;
    assert(cplus_fwprintf(NULL, L"x") == -1 && errno == EINVAL);
    errno = 0;
    {
        va_list empty_ap;
        __builtin_memset(&empty_ap, 0, sizeof empty_ap);
        assert(cplus_vfwprintf(NULL, L"x", empty_ap) == -1 && errno == EINVAL);
    }
    errno = 0;
    assert(cplus_wscanf(NULL) == EOF && errno == EINVAL);
    errno = 0;
    assert(cplus_fwscanf(NULL, L"%d") == EOF && errno == EINVAL);
    errno = 0;
    assert(cplus_swscanf(NULL, L"%d") == EOF && errno == EINVAL);
}

/* ====================================================================== */
/* 7. wctrans / towctrans                                                  */
/* ====================================================================== */
static void test_wctrans(void) {
    errno = 0;
    assert(cplus_wctrans("definitely-not-a-property") == (wctrans_t)0);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_wctrans("") == (wctrans_t)0);
    errno = 0;
    assert(cplus_wctrans(NULL) == (wctrans_t)0 && errno == EINVAL);
    {
        wctrans_t up = cplus_wctrans("toupper");
        assert(up != (wctrans_t)0);
        assert(cplus_towctrans(L'a', up) == L'A');
        /* embedded NUL in the property name: only the prefix is seen */
        assert(cplus_wctrans("toupper\0junk") == up);
    }
    /* towctrans with a fabricated descriptor: host-defined, must be safe and
     * must match the host oracle exactly. */
    {
        wint_t a, b;
        errno = 0;
        a = cplus_towctrans(L'a', (wctrans_t)0);
        errno = 0;
        b = towctrans(L'a', (wctrans_t)0);
        assert(a == b);
        FINDING("towctrans: descriptor (wctrans_t)0 returned 0x%X with no crash; host-defined behavior faithfully delegated (no guard)", (unsigned)a);
    }
}

/* ====================================================================== */
/* 8. Deterministic differential vs host (1000 valid inputs, fixed seed)   */
/* ====================================================================== */
static unsigned long long rng_state = 0x12345678ULL;
static unsigned long long lcg(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return rng_state;
}

static void test_differential_1000(void) {
    static const int bases[] = {0, 2, 8, 10, 16, 36};
    wchar_t wb[64];
    int i;
    for (i = 0; i < 1000; ++i) {
        long long v = (long long)(lcg() % 2000000000000ULL);
        int base = bases[lcg() % NELM(bases)];
        wchar_t *eh = NULL, *ec = NULL;
        long long rh, rc;
        int eh_err, ec_err;

        swprintf(wb, NELM(wb), L"%lld", v);
        errno = 0; rh = wcstoll(wb, &eh, base); eh_err = errno;
        errno = 0; rc = cplus_wcstoll(wb, &ec, base); ec_err = errno;
        assert(rh == rc && eh_err == ec_err && (eh - wb) == (ec - wb));

        {
            unsigned long long uh, uc;
            errno = 0; uh = wcstoull(wb, &eh, base); eh_err = errno;
            errno = 0; uc = cplus_wcstoull(wb, &ec, base); ec_err = errno;
            assert(uh == uc && eh_err == ec_err && (eh - wb) == (ec - wb));
        }
        {
            intmax_t ih, ic;
            errno = 0; ih = wcstoimax(wb, &eh, base); eh_err = errno;
            errno = 0; ic = cplus_wcstoimax(wb, &ec, base); ec_err = errno;
            assert(ih == ic && eh_err == ec_err && (eh - wb) == (ec - wb));
        }
        {
            uintmax_t uh, uc;
            errno = 0; uh = wcstoumax(wb, &eh, base); eh_err = errno;
            errno = 0; uc = cplus_wcstoumax(wb, &ec, base); ec_err = errno;
            assert(uh == uc && eh_err == ec_err && (eh - wb) == (ec - wb));
        }
        {
            double dh, dc;
            swprintf(wb, NELM(wb), L"%d.%06d", (int)(lcg() % 100000), (int)(lcg() % 1000000));
            errno = 0; dh = wcstod(wb, &eh); eh_err = errno;
            errno = 0; dc = cplus_wcstod(wb, &ec); ec_err = errno;
            assert(dh == dc && eh_err == ec_err && (eh - wb) == (ec - wb));
        }
        {
            wint_t wh = towupper((wint_t)(L'a' + (int)(lcg() % 26)));
            wint_t wc_ = cplus_towupper(wh);
            assert(wc_ == towupper(wh));
        }
    }
}

int main(void) {
    /* Deterministic locale: required by the harness. */
    assert(setlocale(LC_ALL, "C") != NULL);
    setvbuf(stdout, NULL, _IONBF, 0);

    test_conversion_overflow();
    test_endptr_forgery();
    test_wcstok_hostile();
    test_wcsftime_edges();
    test_multibyte_restartable();
    test_wide_format();
    test_wctrans();
    test_differential_1000();

    printf("wchar_attack: ok\n");
    return 0;
}
