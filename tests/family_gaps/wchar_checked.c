/* tests/family_gaps/wchar_checked.c -- coverage evidence for the exported
 * ISO C wide-character / wctype surface of the C+ libc.
 *
 * Scope (every API named in the family-gap task is exercised):
 *   wcslen wcscpy wcsncpy wcscat wcsncat wcscmp wcsncmp wcsstr wcschr
 *   wcsrchr wcspbrk wcsspn wcscspn wcstok wmemcpy wmemmove wmemset
 *   wmemcmp wmemchr
 *   wcstol wcstoll wcstoul wcstoull  (wcstol family)
 *   wcstod wcstof wcstold            (wcstod family)
 *   wcstoimax wcstoumax wcsftime
 *   mbrtowc wcrtomb mbsrtowcs wcsrtombs mbsinit
 *   fgetws fputws swprintf
 *   iswalpha iswdigit iswspace iswalnum iswupper iswlower iswxdigit
 *   iswprint iswgraph iswpunct iswcntrl iswblank iswctype
 *   towupper towlower wctype wctrans towctrans
 *
 * NOTE on wcsncat: the C+ libc exports no cplus_wcsncat symbol (verified with
 * nm over build/libc/libcplus_hardened.a). It is therefore covered through
 * the host libc function directly and reported as HOST-ONLY, rather than
 * inventing a wrapper that does not exist.
 *
 * Contract:
 *   - setlocale(LC_ALL, "C") is forced for deterministic behaviour.
 *   - For every cplus_* entry point the observable result (return value,
 *     produced buffer, errno, endptr, final mbstate_t) is compared with the
 *     identically named host libc function (host differential). Inputs that
 *     are UB for the host (NULL where a pointer to an object is required) are
 *     NOT passed to the host; only the documented C+ hardening guards are
 *     probed for those, with errno == EINVAL and the conventional failure
 *     return.
 *   - Normal, boundary (length 0 / 1 / overflow / EOF / reset) and invalid
 *     (no digits, bad multibyte byte, bad format, bad base) inputs are used.
 *   - Logging parity: the entire check body is executed once with logging
 *     OFF (verbose, official counts) and once with logging ON (quiet); the
 *     digest of all observations and the failure count must be identical,
 *     and entry markers must appear only in the ON pass.
 *   - No genuine UB: no NULL passed to a function that requires an object,
 *     no byte-poisoned mbstate_t, no read past a terminator, every
 *     destination large enough for the bound actually handed to the callee.
 *   - Each check emits "FINDING: <file>:<line>: ..." on failure. main emits
 *     "wchar_checked: <N>/<N> checks ok" and returns nonzero on any failure.
 *
 * Build / verify (both libraries, each also under UBSan):
 *   CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh
 *   LIB=build/libc/libcplus_hardened.a
 *   # 1. C+ libc layer
 *   $CC -O1 -g -std=c99 -Wall -Wextra -pedantic -D_XOPEN_SOURCE=700 \
 *       tests/family_gaps/wchar_checked.c $LIB -lm -pthread -o wchar_cplus
 *   # 2. plain host libc layer (cplus_* names aliased to host, no C+ lib)
 *   $CC -O1 -g -std=c99 -Wall -Wextra -pedantic -D_XOPEN_SOURCE=700 \
 *       -DWCHAR_CHECKED_HOST_ONLY tests/family_gaps/wchar_checked.c \
 *       -lm -pthread -o wchar_host
 *   # 3/4. repeat either command adding
 *       -fsanitize=undefined -fno-sanitize-recover=all \
 *       -fno-sanitize=float-divide-by-zero
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#define LOG_PREFIX "[CPLUS-LIBC] "

/* ------------------------------------------------------------------ */
/* Function under test: C+ wrapper in the default (libc) layer, or the
 * host function itself in the plain-host layer.                        */
/* ------------------------------------------------------------------ */
#ifdef WCHAR_CHECKED_HOST_ONLY

#define cplus_wcslen wcslen
#define cplus_wcscpy wcscpy
#define cplus_wcsncpy wcsncpy
#define cplus_wcscat wcscat
#define cplus_wcscmp wcscmp
#define cplus_wcsncmp wcsncmp
#define cplus_wcsstr wcsstr
#define cplus_wcschr wcschr
#define cplus_wcsrchr wcsrchr
#define cplus_wcspbrk wcspbrk
#define cplus_wcsspn wcsspn
#define cplus_wcscspn wcscspn
#define cplus_wcstok wcstok
#define cplus_wmemcpy wmemcpy
#define cplus_wmemmove wmemmove
#define cplus_wmemset wmemset
#define cplus_wmemcmp wmemcmp
#define cplus_wmemchr wmemchr
#define cplus_wcstol wcstol
#define cplus_wcstoll wcstoll
#define cplus_wcstoul wcstoul
#define cplus_wcstoull wcstoull
#define cplus_wcstod wcstod
#define cplus_wcstof wcstof
#define cplus_wcstold wcstold
#define cplus_wcstoimax wcstoimax
#define cplus_wcstoumax wcstoumax
#define cplus_wcsftime wcsftime
#define cplus_mbrtowc mbrtowc
#define cplus_wcrtomb wcrtomb
#define cplus_mbsrtowcs mbsrtowcs
#define cplus_wcsrtombs wcsrtombs
#define cplus_mbsinit mbsinit
#define cplus_fgetws fgetws
#define cplus_fputws fputws
#define cplus_swprintf swprintf
#define cplus_iswalpha iswalpha
#define cplus_iswdigit iswdigit
#define cplus_iswspace iswspace
#define cplus_iswalnum iswalnum
#define cplus_iswupper iswupper
#define cplus_iswlower iswlower
#define cplus_iswxdigit iswxdigit
#define cplus_iswprint iswprint
#define cplus_iswgraph iswgraph
#define cplus_iswpunct iswpunct
#define cplus_iswcntrl iswcntrl
#define cplus_iswblank iswblank
#define cplus_iswctype iswctype
#define cplus_towupper towupper
#define cplus_towlower towlower
#define cplus_wctype wctype
#define cplus_wctrans wctrans
#define cplus_towctrans towctrans

static void cplus_libc_log_set_enabled(int enabled) { (void)enabled; }

#else /* C+ libc layer: local declarations of the exported wrappers */

extern void cplus_libc_log_set_enabled(int enabled);

extern size_t cplus_wcslen(const wchar_t *);
extern wchar_t *cplus_wcscpy(wchar_t *, const wchar_t *);
extern wchar_t *cplus_wcsncpy(wchar_t *, const wchar_t *, size_t);
extern wchar_t *cplus_wcscat(wchar_t *, const wchar_t *);
extern int cplus_wcscmp(const wchar_t *, const wchar_t *);
extern int cplus_wcsncmp(const wchar_t *, const wchar_t *, size_t);
extern wchar_t *cplus_wcsstr(const wchar_t *, const wchar_t *);
extern wchar_t *cplus_wcschr(const wchar_t *, wchar_t);
extern wchar_t *cplus_wcsrchr(const wchar_t *, wchar_t);
extern wchar_t *cplus_wcspbrk(const wchar_t *, const wchar_t *);
extern size_t cplus_wcsspn(const wchar_t *, const wchar_t *);
extern size_t cplus_wcscspn(const wchar_t *, const wchar_t *);
extern wchar_t *cplus_wcstok(wchar_t *, const wchar_t *, wchar_t **);
extern wchar_t *cplus_wmemcpy(wchar_t *, const wchar_t *, size_t);
extern wchar_t *cplus_wmemmove(wchar_t *, const wchar_t *, size_t);
extern wchar_t *cplus_wmemset(wchar_t *, wchar_t, size_t);
extern int cplus_wmemcmp(const wchar_t *, const wchar_t *, size_t);
extern wchar_t *cplus_wmemchr(const wchar_t *, wchar_t, size_t);

extern long cplus_wcstol(const wchar_t *, wchar_t **, int);
extern long long cplus_wcstoll(const wchar_t *, wchar_t **, int);
extern unsigned long cplus_wcstoul(const wchar_t *, wchar_t **, int);
extern unsigned long long cplus_wcstoull(const wchar_t *, wchar_t **, int);
extern double cplus_wcstod(const wchar_t *, wchar_t **);
extern float cplus_wcstof(const wchar_t *, wchar_t **);
extern long double cplus_wcstold(const wchar_t *, wchar_t **);
extern intmax_t cplus_wcstoimax(const wchar_t *, wchar_t **, int);
extern uintmax_t cplus_wcstoumax(const wchar_t *, wchar_t **, int);
extern size_t cplus_wcsftime(wchar_t *, size_t, const wchar_t *,
                             const struct tm *);

extern size_t cplus_mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
extern size_t cplus_wcrtomb(char *, wchar_t, mbstate_t *);
extern size_t cplus_mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
extern size_t cplus_wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);
extern int cplus_mbsinit(const mbstate_t *);

extern wchar_t *cplus_fgetws(wchar_t *, int, FILE *);
extern int cplus_fputws(const wchar_t *, FILE *);
extern int cplus_swprintf(wchar_t *, size_t, const wchar_t *, ...);

extern int cplus_iswalpha(wint_t);
extern int cplus_iswdigit(wint_t);
extern int cplus_iswspace(wint_t);
extern int cplus_iswalnum(wint_t);
extern int cplus_iswupper(wint_t);
extern int cplus_iswlower(wint_t);
extern int cplus_iswxdigit(wint_t);
extern int cplus_iswprint(wint_t);
extern int cplus_iswgraph(wint_t);
extern int cplus_iswpunct(wint_t);
extern int cplus_iswcntrl(wint_t);
extern int cplus_iswblank(wint_t);
extern wint_t cplus_towupper(wint_t);
extern wint_t cplus_towlower(wint_t);
extern wctype_t cplus_wctype(const char *);
extern int cplus_iswctype(wint_t, wctype_t);
extern wctrans_t cplus_wctrans(const char *);
extern wint_t cplus_towctrans(wint_t, wctrans_t);

#endif /* WCHAR_CHECKED_HOST_ONLY */

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */
static int g_checks;
static int g_failures;
static int g_quiet;
static unsigned long g_digest = 2166136261UL;

#define CHECK(cond, ...) do {                                            \
    int _c = (cond) ? 1 : 0;                                             \
    g_digest = g_digest * 16777619UL + (unsigned long)_c;                 \
    ++g_checks;                                                          \
    if (!_c) {                                                           \
        ++g_failures;                                                    \
        if (!g_quiet) {                                                  \
            printf("FINDING: %s:%d: ", __FILE__, __LINE__);              \
            printf(__VA_ARGS__);                                         \
            printf("\n");                                                \
        }                                                                \
    }                                                                    \
} while (0)

/* C+ hardening guards for inputs that are UB in raw C (NULL where an object
 * pointer is required) have no host counterpart, so they are exercised only
 * in the C+ libc layer. The plain-host layer cannot call them without
 * invoking genuine UB, which would defeat the UBSan verification. */
#ifndef WCHAR_CHECKED_HOST_ONLY
#define NULL_GUARD_CHECK(...) CHECK(__VA_ARGS__)
#else
#define NULL_GUARD_CHECK(...) ((void)0)
#endif

/* Fold an arbitrary observation into the parity digest without counting it
 * as a separate check. */
static void observe(unsigned long v) {
    g_digest = g_digest * 16777619UL + v;
}

static int sgn(int x) { return (x > 0) - (x < 0); }

static ptrdiff_t poff(const wchar_t *base, const wchar_t *p) {
    return p ? (ptrdiff_t)(p - base) : (ptrdiff_t)-1;
}

static int wsig(int x) { return (x > 0) - (x < 0); }

/* Fill wchar_t elements (a byte-oriented memset leaves each 4-byte wchar_t
 * as 0xvvvvvvvv on this host, which is not the same as (wchar_t)0xvv). */
static void wfill(wchar_t *p, size_t n, wchar_t v) {
    while (n-- != 0u) *p++ = v;
}

/* ================================================================== */
/* 1. String and memory functions                                      */
/* ================================================================== */
static void test_string_memory(void) {
    wchar_t a[32], b[32], c[32];

    /* wcslen -- normal, empty boundary, host differential */
    {
        const wchar_t s[] = L"hello";
        size_t x = cplus_wcslen(s), y = wcslen(s);
        CHECK(x == 5u && x == y, "wcslen normal: %lu vs host %lu",
              (unsigned long)x, (unsigned long)y);
    }
    {
        const wchar_t s[] = L"";
        CHECK(cplus_wcslen(s) == 0u && cplus_wcslen(s) == wcslen(s),
              "wcslen empty boundary");
    }

    /* wcscpy -- normal + differential + content */
    memset(a, 0x7f, sizeof a);
    memset(b, 0x7f, sizeof b);
    CHECK(cplus_wcscpy(a, L"copy-me") == a && wcscpy(b, L"copy-me") == b &&
              wcscmp(a, b) == 0,
          "wcscpy normal/differential");

    /* wcsncpy -- bounded copy, padding, n == 0 */
    wfill(a, 32u, (wchar_t)0x55);
    wfill(b, 32u, (wchar_t)0x55);
    (void)cplus_wcsncpy(a, L"abcdef", 3u);
    (void)wcsncpy(b, L"abcdef", 3u);
    CHECK(memcmp(a, b, sizeof a) == 0 && a[3] == (wchar_t)0x55,
          "wcsncpy bounded/differential");
    wfill(a, 32u, (wchar_t)0x55);
    wfill(b, 32u, (wchar_t)0x55);
    (void)cplus_wcsncpy(a, L"ab", 5u);
    (void)wcsncpy(b, L"ab", 5u);
    CHECK(memcmp(a, b, sizeof a) == 0 && a[2] == L'\0' && a[4] == L'\0',
          "wcsncpy padding boundary");
    wfill(a, 32u, (wchar_t)0x55);
    wfill(b, 32u, (wchar_t)0x55);
    (void)cplus_wcsncpy(a, L"ab", 0u);
    (void)wcsncpy(b, L"ab", 0u);
    CHECK(memcmp(a, b, sizeof a) == 0 && a[0] == (wchar_t)0x55,
          "wcsncpy n==0 boundary");

    /* wcscat -- normal + differential */
    wcscpy(a, L"abc");
    wcscpy(b, L"abc");
    CHECK(cplus_wcscat(a, L"def") == a && wcscat(b, L"def") == b &&
              wcscmp(a, b) == 0 && wcscmp(a, L"abcdef") == 0,
          "wcscat normal/differential");

    /* wcsncat -- HOST-ONLY (no cplus_wcsncat is exported by the C+ libc) */
    wcscpy(a, L"abc");
    wcscpy(b, L"abc");
    CHECK(wcsncat(a, L"defgh", 3u) == a && wcsncat(b, L"defgh", 3u) == b &&
              wcscmp(a, b) == 0 && wcscmp(a, L"abcdef") == 0,
          "wcsncat HOST-ONLY normal/boundary");

    /* wcscmp -- equal / less / greater, differential */
    CHECK(cplus_wcscmp(L"abc", L"abc") == 0 &&
              cplus_wcscmp(L"abc", L"abc") == wcscmp(L"abc", L"abc"),
          "wcscmp equal");
    CHECK(wsig(cplus_wcscmp(L"abc", L"abd")) ==
              wsig(wcscmp(L"abc", L"abd")) &&
              wsig(cplus_wcscmp(L"abd", L"abc")) ==
              wsig(wcscmp(L"abd", L"abc")),
          "wcscmp ordering/differential");

    /* wcsncmp -- prefix and n == 0 */
    CHECK(cplus_wcsncmp(L"abcdef", L"abcXYZ", 3u) == 0 &&
              cplus_wcsncmp(L"abcdef", L"abcXYZ", 3u) ==
              wcsncmp(L"abcdef", L"abcXYZ", 3u),
          "wcsncmp prefix/differential");
    CHECK(cplus_wcsncmp(L"abc", L"xyz", 0u) == 0 &&
              cplus_wcsncmp(L"abc", L"xyz", 0u) ==
              wcsncmp(L"abc", L"xyz", 0u),
          "wcsncmp n==0 boundary");

    /* wcsstr -- found, absent, empty needle */
    CHECK(poff(L"abcaXbca", cplus_wcsstr(L"abcaXbca", L"Xbc")) ==
              poff(L"abcaXbca", wcsstr(L"abcaXbca", L"Xbc")) &&
              poff(L"abcaXbca", cplus_wcsstr(L"abcaXbca", L"Xbc")) == 4,
          "wcsstr found/differential");
    CHECK(cplus_wcsstr(L"abcaXbca", L"Q!") == wcsstr(L"abcaXbca", L"Q!") &&
          cplus_wcsstr(L"abcaXbca", L"Q!") == NULL,
          "wcsstr absent");
    {
        const wchar_t h[] = L"abc";
        wchar_t *r = cplus_wcsstr(h, L"");
        CHECK(r == wcsstr(h, L"") && r == h,
              "wcsstr empty needle boundary");
    }

    /* wcschr -- present, absent, terminator */
    CHECK(poff(L"abcaXbca", cplus_wcschr(L"abcaXbca", L'c')) ==
              poff(L"abcaXbca", wcschr(L"abcaXbca", L'c')),
          "wcschr present/differential");
    CHECK(cplus_wcschr(L"abc", L'z') == wcschr(L"abc", L'z') &&
          cplus_wcschr(L"abc", L'z') == NULL,
          "wcschr absent");
    CHECK(poff(L"abc", cplus_wcschr(L"abc", L'\0')) ==
              poff(L"abc", wcschr(L"abc", L'\0')),
          "wcschr terminator boundary");

    /* wcsrchr -- last occurrence, absent, terminator */
    CHECK(poff(L"abcaXbca", cplus_wcsrchr(L"abcaXbca", L'c')) ==
              poff(L"abcaXbca", wcsrchr(L"abcaXbca", L'c')) &&
              poff(L"abcaXbca", cplus_wcsrchr(L"abcaXbca", L'c')) == 6,
          "wcsrchr last/differential");
    CHECK(cplus_wcsrchr(L"abc", L'z') == wcsrchr(L"abc", L'z') &&
          cplus_wcsrchr(L"abc", L'z') == NULL,
          "wcsrchr absent");
    CHECK(poff(L"abc", cplus_wcsrchr(L"abc", L'\0')) ==
              poff(L"abc", wcsrchr(L"abc", L'\0')),
          "wcsrchr terminator boundary");

    /* wcspbrk -- set member, none, empty set */
    CHECK(poff(L"abcaXbca", cplus_wcspbrk(L"abcaXbca", L"cX")) ==
              poff(L"abcaXbca", wcspbrk(L"abcaXbca", L"cX")),
          "wcspbrk present/differential");
    CHECK(cplus_wcspbrk(L"abc", L"Q!") == wcspbrk(L"abc", L"Q!") &&
          cplus_wcspbrk(L"abc", L"Q!") == NULL,
          "wcspbrk absent");
    CHECK(cplus_wcspbrk(L"abc", L"") == wcspbrk(L"abc", L""),
          "wcspbrk empty set boundary");

    /* wcsspn / wcscspn -- prefix, complement, boundaries */
    CHECK(cplus_wcsspn(L"abcXYZ", L"abc") == wcsspn(L"abcXYZ", L"abc") &&
          cplus_wcsspn(L"abcXYZ", L"abc") == 3u,
          "wcsspn prefix/differential");
    CHECK(cplus_wcsspn(L"abc", L"") == wcsspn(L"abc", L"") &&
          cplus_wcsspn(L"abc", L"") == 0u,
          "wcsspn empty set boundary");
    CHECK(cplus_wcscspn(L"abcXYZ", L"abc") == wcscspn(L"abcXYZ", L"abc") &&
          cplus_wcscspn(L"abcXYZ", L"abc") == 0u,
          "wcscspn prefix/differential");
    CHECK(cplus_wcscspn(L"xyz", L"abc") == wcscspn(L"xyz", L"abc") &&
          cplus_wcscspn(L"xyz", L"abc") == 3u,
          "wcscspn no-match boundary");

    /* wcstok -- ISO C 3-argument form with continuation */
    {
        wchar_t s1[] = L"a,b,,c";
        wchar_t s2[] = L"a,b,,c";
        wchar_t *p1 = NULL, *p2 = NULL;
        wchar_t *t1 = cplus_wcstok(s1, L",", &p1);
        wchar_t *t2 = wcstok(s2, L",", &p2);
        CHECK(t1 != NULL && t2 != NULL && wcscmp(t1, t2) == 0 &&
                  wcscmp(t1, L"a") == 0,
              "wcstok first token/differential");
        t1 = cplus_wcstok(NULL, L",", &p1);
        t2 = wcstok(NULL, L",", &p2);
        CHECK(t1 != NULL && t2 != NULL && wcscmp(t1, t2) == 0 &&
                  wcscmp(t1, L"b") == 0,
              "wcstok continuation token/differential");
        t1 = cplus_wcstok(NULL, L",", &p1);
        t2 = wcstok(NULL, L",", &p2);
        CHECK(t1 != NULL && t2 != NULL && wcscmp(t1, t2) == 0 &&
                  wcscmp(t1, L"c") == 0,
              "wcstok empty-field skip/differential");
        t1 = cplus_wcstok(NULL, L",", &p1);
        t2 = wcstok(NULL, L",", &p2);
        CHECK(t1 == NULL && t2 == NULL,
              "wcstok end boundary/differential");
    }

    /* wmemcpy -- normal and n == 0 */
    memset(a, 0x11, sizeof a);
    memset(b, 0x11, sizeof b);
    memset(c, 0x12, sizeof c);
    CHECK(cplus_wmemcpy(a, c, 5u) == a && wmemcpy(b, c, 5u) == b &&
              memcmp(a, b, sizeof a) == 0,
          "wmemcpy normal/differential");
    wfill(a, 32u, (wchar_t)0x11);
    wfill(b, 32u, (wchar_t)0x11);
    CHECK(cplus_wmemcpy(a, c, 0u) == a && wmemcpy(b, c, 0u) == b &&
              memcmp(a, b, sizeof a) == 0 && a[0] == (wchar_t)0x11,
          "wmemcpy n==0 boundary");

    /* wmemmove -- overlapping regions */
    wcscpy(a, L"abcdefghij");
    wcscpy(b, L"abcdefghij");
    (void)cplus_wmemmove(a + 2, a, 5u);
    (void)wmemmove(b + 2, b, 5u);
    CHECK(memcmp(a, b, sizeof a) == 0,
          "wmemmove overlap/differential");

    /* wmemset -- normal and n == 0 */
    memset(a, 0x33, sizeof a);
    memset(b, 0x33, sizeof b);
    CHECK(cplus_wmemset(a, L'Z', 4u) == a && wmemset(b, L'Z', 4u) == b &&
              memcmp(a, b, sizeof a) == 0 && a[3] == L'Z',
          "wmemset normal/differential");
    wfill(a, 32u, (wchar_t)0x33);
    wfill(b, 32u, (wchar_t)0x33);
    CHECK(cplus_wmemset(a, L'Z', 0u) == a && wmemset(b, L'Z', 0u) == b &&
              memcmp(a, b, sizeof a) == 0 && a[0] == (wchar_t)0x33,
          "wmemset n==0 boundary");

    /* wmemcmp -- equal / differing prefix */
    {
        const wchar_t x[] = L"abcXYZ";
        const wchar_t y[] = L"abc123";
        CHECK(cplus_wmemcmp(x, y, 3u) == 0 &&
                  cplus_wmemcmp(x, y, 3u) == wmemcmp(x, y, 3u),
              "wmemcmp equal/differential");
        CHECK(sgn(cplus_wmemcmp(x, y, 6u)) == sgn(wmemcmp(x, y, 6u)) &&
                  cplus_wmemcmp(x, y, 6u) != 0,
              "wmemcmp differ/differential");
        CHECK(cplus_wmemcmp(x, y, 0u) == 0 &&
                  cplus_wmemcmp(x, y, 0u) == wmemcmp(x, y, 0u),
              "wmemcmp n==0 boundary");
    }

    /* wmemchr -- present, absent, n == 0 */
    {
        const wchar_t s[] = L"abcaXbca";
        CHECK(poff(s, cplus_wmemchr(s, L'X', 8u)) ==
                  poff(s, wmemchr(s, L'X', 8u)),
              "wmemchr present/differential");
        CHECK(cplus_wmemchr(s, L'z', 8u) == wmemchr(s, L'z', 8u) &&
                  cplus_wmemchr(s, L'z', 8u) == NULL,
              "wmemchr absent");
        CHECK(cplus_wmemchr(s, L'a', 0u) == wmemchr(s, L'a', 0u) &&
                  cplus_wmemchr(s, L'a', 0u) == NULL,
              "wmemchr n==0 boundary");
    }
}

/* ================================================================== */
/* 2. wcstol family + wcstod family + wcstoimax/wcstoumax              */
/* ================================================================== */
static void test_numeric(void) {
    wchar_t *end1, *end2;

    /* wcstol -- base 10 with endptr, base 16, overflow, invalid, NULL */
    errno = 0;
    CHECK(cplus_wcstol(L"123xyz", &end1, 10) == 123L && end1 != NULL &&
              *end1 == L'x' && errno == 0,
          "wcstol base10/endptr");
    errno = 0;
    CHECK(cplus_wcstol(L"123xyz", &end1, 10) == wcstol(L"123xyz", &end2, 10) &&
              poff(L"123xyz", end1) == poff(L"123xyz", end2),
          "wcstol base10/differential");
    CHECK(cplus_wcstol(L"-FF", &end1, 16) == wcstol(L"-FF", &end2, 16) &&
              cplus_wcstol(L"-FF", &end1, 16) == -255L,
          "wcstol base16/differential");
    errno = 0;
    (void)cplus_wcstol(L"999999999999999999999999999999", &end1, 10);
    CHECK(errno == ERANGE, "wcstol overflow ERANGE");
    errno = 0;
    CHECK(cplus_wcstol(L"xyz", &end1, 10) == wcstol(L"xyz", &end2, 10) &&
              end1 != NULL && end2 != NULL && wcscmp(end1, end2) == 0 &&
              wcscmp(end1, L"xyz") == 0,
          "wcstol invalid input/differential");
    errno = 0;
    end1 = (wchar_t *)L"sentinel";
    NULL_GUARD_CHECK(cplus_wcstol(NULL, &end1, 10) == 0L && end1 == NULL &&
              errno == EINVAL,
          "wcstol NULL hardening (defined)");

    /* wcstoll */
    CHECK(cplus_wcstoll(L"9000000007", &end1, 10) ==
              wcstoll(L"9000000007", &end2, 10),
          "wcstoll normal/differential");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcstoll(NULL, &end1, 10) == 0LL && errno == EINVAL,
          "wcstoll NULL hardening (defined)");

    /* wcstoul */
    CHECK(cplus_wcstoul(L"42", &end1, 10) == wcstoul(L"42", &end2, 10) &&
              cplus_wcstoul(L"42", &end1, 10) == 42UL,
          "wcstoul normal/differential");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcstoul(NULL, &end1, 10) == 0UL && errno == EINVAL,
          "wcstoul NULL hardening (defined)");

    /* wcstoull */
    CHECK(cplus_wcstoull(L"18000000007", &end1, 10) ==
              wcstoull(L"18000000007", &end2, 10),
          "wcstoull normal/differential");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcstoull(NULL, &end1, 10) == 0ULL && errno == EINVAL,
          "wcstoull NULL hardening (defined)");

    /* wcstod -- normal, overflow, invalid, NULL */
    errno = 0;
    CHECK(cplus_wcstod(L"3.5e2rest", &end1) == wcstod(L"3.5e2rest", &end2) &&
              cplus_wcstod(L"3.5e2rest", &end1) == 350.0 && errno == 0,
          "wcstod normal/differential");
    errno = 0;
    (void)cplus_wcstod(L"1e999999", &end1);
    CHECK(errno == ERANGE, "wcstod overflow ERANGE");
    errno = 0;
    CHECK(cplus_wcstod(L"nope", &end1) == 0.0 &&
              end1 != NULL && wcscmp(end1, L"nope") == 0,
          "wcstod invalid input");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcstod(NULL, &end1) == 0.0 && end1 == NULL &&
              errno == EINVAL,
          "wcstod NULL hardening (defined)");

    /* wcstof */
    CHECK(cplus_wcstof(L"1.25", &end1) == wcstof(L"1.25", &end2) &&
              cplus_wcstof(L"1.25", &end1) == 1.25f,
          "wcstof normal/differential");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcstof(NULL, &end1) == 0.0f && errno == EINVAL,
          "wcstof NULL hardening (defined)");

    /* wcstold */
    CHECK(cplus_wcstold(L"2.75", &end1) == wcstold(L"2.75", &end2) &&
              cplus_wcstold(L"2.75", &end1) == 2.75L,
          "wcstold normal/differential");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcstold(NULL, &end1) == 0.0L && errno == EINVAL,
          "wcstold NULL hardening (defined)");

    /* wcstoimax / wcstoumax */
    CHECK(cplus_wcstoimax(L"9223372036854775807", &end1, 10) ==
              wcstoimax(L"9223372036854775807", &end2, 10),
          "wcstoimax normal/differential");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcstoimax(NULL, &end1, 10) == 0 && errno == EINVAL,
          "wcstoimax NULL hardening (defined)");
    CHECK(cplus_wcstoumax(L"18446744073709551615", &end1, 10) ==
              wcstoumax(L"18446744073709551615", &end2, 10),
          "wcstoumax normal/differential");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcstoumax(NULL, &end1, 10) == 0 && errno == EINVAL,
          "wcstoumax NULL hardening (defined)");
}

/* ================================================================== */
/* 3. wcsftime                                                         */
/* ================================================================== */
static void test_wcsftime(void) {
    struct tm t;
    wchar_t wa[64], wb[64];

    memset(&t, 0, sizeof t);
    t.tm_year = 121;  /* 2021 */
    t.tm_mon = 0;     /* January */
    t.tm_mday = 2;
    t.tm_hour = 3;
    t.tm_min = 4;
    t.tm_sec = 5;

    memset(wa, 0, sizeof wa);
    memset(wb, 0, sizeof wb);
    CHECK(cplus_wcsftime(wa, sizeof wa / sizeof wa[0], L"%Y-%m-%d", &t) ==
              wcsftime(wb, sizeof wb / sizeof wb[0], L"%Y-%m-%d", &t) &&
              wcscmp(wa, wb) == 0 && wcscmp(wa, L"2021-01-02") == 0,
          "wcsftime normal/differential");

    /* maxsize == 0: length query, host ignores the buffer. */
    CHECK(cplus_wcsftime(wa, 0u, L"%Y-%m-%d", &t) ==
              wcsftime(wb, 0u, L"%Y-%m-%d", &t),
          "wcsftime maxsize==0 boundary/differential");

    /* Invalid format and NULL buffer with a nonzero size are C+ hardening
     * extensions; host is not called for those (UB there). */
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcsftime(wa, sizeof wa / sizeof wa[0], NULL, &t) == 0u &&
              errno == EINVAL,
          "wcsftime NULL format (defined)");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcsftime(NULL, 8u, L"%Y", &t) == 0u && errno == EINVAL,
          "wcsftime NULL buffer (defined)");
}

/* ================================================================== */
/* 4. Restartable multibyte conversions + mbsinit                      */
/* ================================================================== */
static void test_multibyte(void) {
    mbstate_t sa, sb;
    wchar_t wc1, wc2;
    wchar_t wa_buf[16], wb_buf[16];
    char cb1[16], cb2[16];
    const char *sp1, *sp2;
    const wchar_t *wp1, *wp2;
    size_t r1, r2;
    int e1, e2;

    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    r1 = cplus_mbrtowc(&wc1, "A", 1u, &sa);
    r2 = mbrtowc(&wc2, "A", 1u, &sb);
    CHECK(r1 == 1u && r2 == 1u && wc1 == L'A' && wc2 == L'A',
          "mbrtowc normal/differential");

    /* pwc == NULL is a length-only query (valid per ISO C). */
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    CHECK(cplus_mbrtowc(NULL, "A", 1u, &sa) ==
              mbrtowc(NULL, "A", 1u, &sb),
          "mbrtowc NULL pwc length query boundary");

    /* s == NULL is the reset form. */
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    CHECK(cplus_mbrtowc(&wc1, NULL, 0u, &sa) ==
              mbrtowc(&wc2, NULL, 0u, &sb),
          "mbrtowc reset boundary/differential");

    /* Ill-formed byte: the exact outcome is host/locale specific (on this
     * macOS "C" locale a lone 0xff is accepted as one byte; on glibc it is
     * (size_t)-1/EILSEQ). The wrapper delegates, so compare it to the host. */
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    errno = 0;
    r1 = cplus_mbrtowc(&wc1, "\xff", 1u, &sa);
    e1 = errno;
    errno = 0;
    r2 = mbrtowc(&wc2, "\xff", 1u, &sb);
    e2 = errno;
    CHECK(r1 == r2 && e1 == e2 &&
              (r1 == (size_t)-1 || (r1 == 1u && wc1 == wc2)),
          "mbrtowc ill-formed byte/differential");

    /* wcrtomb -- normal and reset (s == NULL) */
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    r1 = cplus_wcrtomb(cb1, L'A', &sa);
    r2 = wcrtomb(cb2, L'A', &sb);
    CHECK(r1 == 1u && r2 == 1u && cb1[0] == 'A' && cb2[0] == 'A',
          "wcrtomb normal/differential");
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    CHECK(cplus_wcrtomb(NULL, L'\0', &sa) == wcrtomb(NULL, L'\0', &sb),
          "wcrtomb reset boundary/differential");

    /* mbsrtowcs -- normal, length-only query, NULL src guard */
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    sp1 = "hello";
    sp2 = "hello";
    memset(wa_buf, 0, sizeof wa_buf);
    r1 = cplus_mbsrtowcs(wa_buf, &sp1, 16u, &sa);
    r2 = mbsrtowcs(wb_buf, &sp2, 16u, &sb);
    CHECK(r1 == 5u && r2 == 5u && sp1 == NULL && sp2 == NULL &&
              wcscmp(wa_buf, wb_buf) == 0 && wcscmp(wa_buf, L"hello") == 0,
          "mbsrtowcs normal/differential");
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    sp1 = "hello";
    sp2 = "hello";
    CHECK(cplus_mbsrtowcs(NULL, &sp1, 0u, &sa) ==
              mbsrtowcs(NULL, &sp2, 0u, &sb) &&
              sp1 != NULL && sp2 != NULL && sp1 == sp2,
          "mbsrtowcs dst==NULL length query boundary");
    errno = 0;
    NULL_GUARD_CHECK(cplus_mbsrtowcs(wa_buf, NULL, 16u, &sa) == (size_t)-1 &&
              errno == EINVAL,
          "mbsrtowcs NULL src guard (defined)");

    /* wcsrtombs -- normal, length-only query, NULL src guard */
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    wp1 = L"world";
    wp2 = L"world";
    r1 = cplus_wcsrtombs(cb1, &wp1, sizeof cb1, &sa);
    r2 = wcsrtombs(cb2, &wp2, sizeof cb2, &sb);
    CHECK(r1 == 5u && r2 == 5u && wp1 == NULL && wp2 == NULL &&
              memcmp(cb1, cb2, 5u) == 0,
          "wcsrtombs normal/differential");
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    wp1 = L"world";
    wp2 = L"world";
    CHECK(cplus_wcsrtombs(NULL, &wp1, 0u, &sa) ==
              wcsrtombs(NULL, &wp2, 0u, &sb) &&
              wp1 != NULL && wp2 != NULL && wp1 == wp2,
          "wcsrtombs dst==NULL length query boundary");
    errno = 0;
    NULL_GUARD_CHECK(cplus_wcsrtombs(cb1, NULL, sizeof cb1, &sa) == (size_t)-1 &&
              errno == EINVAL,
          "wcsrtombs NULL src guard (defined)");

    /* mbsinit -- initial state and the ISO-defined NULL case */
    memset(&sa, 0, sizeof sa);
    CHECK((cplus_mbsinit(&sa) != 0) == (mbsinit(&sa) != 0) &&
              cplus_mbsinit(&sa) != 0,
          "mbsinit initial state/differential");
    CHECK(cplus_mbsinit(NULL) == mbsinit(NULL) && cplus_mbsinit(NULL) != 0,
          "mbsinit NULL (defined)");
}

/* ================================================================== */
/* 5. fgetws / fputws / swprintf                                       */
/* ================================================================== */
static void test_stream_and_format(void) {
    FILE *fa, *fb;
    wchar_t wa[32], wb[32];
    const wchar_t line[] = L"wide line\n";
    int ra, rb;

    fa = tmpfile();
    fb = tmpfile();
    CHECK(fa != NULL && fb != NULL, "tmpfile for wide streams");
    if (fa == NULL || fb == NULL) return;

    /* fputws -- ISO C int return, nonnegative on success */
    ra = cplus_fputws(line, fa);
    rb = fputws(line, fb);
    CHECK(ra >= 0 && rb >= 0 && sgn(ra) == sgn(rb),
          "fputws normal/differential");

    /* fgetws -- normal read */
    rewind(fa);
    rewind(fb);
    memset(wa, 0, sizeof wa);
    memset(wb, 0, sizeof wb);
    CHECK(cplus_fgetws(wa, 32, fa) == wa && fgetws(wb, 32, fb) == wb &&
              wcscmp(wa, wb) == 0 && wcscmp(wa, line) == 0,
          "fgetws normal/differential");

    /* fgetws -- EOF boundary */
    CHECK(cplus_fgetws(wa, 32, fa) == NULL && fgetws(wb, 32, fb) == NULL,
          "fgetws EOF boundary/differential");

    /* fgetws -- n == 1 is host-defined; compare wrapper to host only. */
    rewind(fa);
    rewind(fb);
    memset(wa, 0x7f, sizeof wa);
    memset(wb, 0x7f, sizeof wb);
    {
        wchar_t *pa = cplus_fgetws(wa, 1, fa);
        wchar_t *pb = fgetws(wb, 1, fb);
        CHECK((pa == wa) == (pb == wb) && (pa == NULL) == (pb == NULL) &&
                  memcmp(wa, wb, sizeof wa) == 0,
              "fgetws n==1 boundary/differential");
    }
    fclose(fa);
    fclose(fb);

    /* swprintf -- normal, truncation, NULL format, NULL buffer */
    memset(wa, 0, sizeof wa);
    memset(wb, 0, sizeof wb);
    ra = cplus_swprintf(wa, 32u, L"%d-%ls", 7, L"x");
    rb = swprintf(wb, 32u, L"%d-%ls", 7, L"x");
    CHECK(ra == rb && ra == 3 && wcscmp(wa, wb) == 0 &&
              wcscmp(wa, L"7-x") == 0,
          "swprintf normal/differential");

    memset(wa, 0x3f, sizeof wa);
    memset(wb, 0x3f, sizeof wb);
    ra = cplus_swprintf(wa, 3u, L"%d-%ls", 7, L"abcdef");
    rb = swprintf(wb, 3u, L"%d-%ls", 7, L"abcdef");
    CHECK(ra == rb && memcmp(wa, wb, sizeof wa) == 0 && wa[2] == L'\0',
          "swprintf truncation boundary/differential");

    errno = 0;
    NULL_GUARD_CHECK(cplus_swprintf(wa, 32u, NULL, 1) == -1 && errno == EINVAL,
          "swprintf NULL format (defined)");
    errno = 0;
    NULL_GUARD_CHECK(cplus_swprintf(NULL, 8u, L"%d", 1) == -1 && errno == EINVAL,
          "swprintf NULL buffer (defined)");
}

/* ================================================================== */
/* 6. wctype classification and extensible mappings                    */
/* ================================================================== */
static void test_wctype(void) {
    /* 12 standard isw* classifiers, each normal + differential */
#define CHECK_ISW(fn, yes, no)                                            \
    CHECK(cplus_##fn((wint_t)(yes)) != 0 && cplus_##fn((wint_t)(no)) == 0 && \
              (cplus_##fn((wint_t)(yes)) != 0) == (fn((wint_t)(yes)) != 0) && \
              (cplus_##fn((wint_t)(no)) != 0) == (fn((wint_t)(no)) != 0),   \
          #fn " normal/differential")

    CHECK_ISW(iswalpha, L'a', L'5');
    CHECK_ISW(iswdigit, L'7', L'a');
    CHECK_ISW(iswspace, L' ', L'a');
    CHECK_ISW(iswalnum, L'Z', L'!');
    CHECK_ISW(iswupper, L'Q', L'q');
    CHECK_ISW(iswlower, L'q', L'Q');
    CHECK_ISW(iswxdigit, L'f', L'g');
    CHECK_ISW(iswprint, L'x', L'\n');
    CHECK_ISW(iswgraph, L'x', L' ');
    CHECK_ISW(iswpunct, L'!', L'x');
    CHECK_ISW(iswcntrl, L'\n', L'x');
    CHECK_ISW(iswblank, L'\t', L'x');
#undef CHECK_ISW

    /* towupper / towlower */
    CHECK(cplus_towupper((wint_t)L'a') == (wint_t)L'A' &&
              cplus_towupper((wint_t)L'a') == towupper((wint_t)L'a'),
          "towupper normal/differential");
    CHECK(cplus_towlower((wint_t)L'A') == (wint_t)L'a' &&
              cplus_towlower((wint_t)L'A') == towlower((wint_t)L'A'),
          "towlower normal/differential");

    /* wctype + iswctype */
    {
        wctype_t d1 = cplus_wctype("alpha");
        wctype_t d2 = wctype("alpha");
        CHECK(d1 != (wctype_t)0 && d2 != (wctype_t)0 &&
                  (cplus_iswctype((wint_t)L'a', d1) != 0) ==
                  (iswctype((wint_t)L'a', d2) != 0),
              "wctype/iswctype normal/differential");
    }
    errno = 0;
    NULL_GUARD_CHECK(cplus_wctype(NULL) == (wctype_t)0 && errno == EINVAL,
          "wctype NULL guard (defined)");

    /* wctrans + towctrans */
    {
        wctrans_t t1 = cplus_wctrans("toupper");
        wctrans_t t2 = wctrans("toupper");
        CHECK(t1 != (wctrans_t)0 && t2 != (wctrans_t)0 &&
                  cplus_towctrans((wint_t)L'a', t1) ==
                  towctrans((wint_t)L'a', t2),
              "wctrans/towctrans normal/differential");
    }
    errno = 0;
    NULL_GUARD_CHECK(cplus_wctrans(NULL) == (wctrans_t)0 && errno == EINVAL,
          "wctrans NULL guard (defined)");
}

/* Single pass over every family. Called once with logging OFF (verbose and
 * official) and once with logging ON (quiet) for parity. */
static void run_checks(void) {
    test_string_memory();
    test_numeric();
    test_wcsftime();
    test_multibyte();
    test_stream_and_format();
    test_wctype();
    observe((unsigned long)g_checks);
    observe((unsigned long)g_failures);
}

/* ------------------------------------------------------------------ */
/* Logging capture (POSIX only)                                        */
/* ------------------------------------------------------------------ */
#ifndef _WIN32
typedef struct { int saved, rd, wr, active; } Cap;

static int cap_begin(Cap *c) {
    int fds[2];
    c->saved = c->rd = c->wr = -1;
    c->active = 0;
    if (pipe(fds) != 0) return -1;
    c->rd = fds[0];
    c->wr = fds[1];
    c->saved = dup(STDERR_FILENO);
    if (c->saved < 0) {
        close(c->rd);
        close(c->wr);
        c->rd = c->wr = -1;
        return -1;
    }
    if (dup2(c->wr, STDERR_FILENO) < 0) {
        (void)dup2(c->saved, STDERR_FILENO);
        close(c->saved);
        close(c->rd);
        close(c->wr);
        c->saved = c->rd = c->wr = -1;
        return -1;
    }
    {
        int fl = fcntl(c->rd, F_GETFL, 0);
        if (fl >= 0) (void)fcntl(c->rd, F_SETFL, fl | O_NONBLOCK);
    }
    c->active = 1;
    return 0;
}

static size_t cap_end(Cap *c, char *buf, size_t cap) {
    size_t n = 0;
    if (c->active) {
        (void)fflush(stderr);
        (void)dup2(c->saved, STDERR_FILENO); /* restore before any FINDING */
        close(c->saved);
        close(c->wr); /* last write end closed => EOF after drain */
        c->saved = -1;
        c->wr = -1;
        c->active = 0;
    }
    if (cap == 0u) {
        if (c->rd >= 0) { close(c->rd); c->rd = -1; }
        return 0u;
    }
    for (;;) {
        ssize_t k;
        if (n >= cap - 1u) break;
        k = read(c->rd, buf + n, cap - 1u - n);
        if (k > 0) { n += (size_t)k; continue; }
        if (k == 0) break;                 /* EOF */
        if (errno == EINTR) continue;
        break;                             /* EAGAIN or error: stop */
    }
    buf[n] = '\0';
    if (c->rd >= 0) { close(c->rd); c->rd = -1; }
    return n;
}
#endif /* !_WIN32 */

/* Representative entry markers, one or more from every family. */
#ifndef WCHAR_CHECKED_HOST_ONLY
static const char *const kMarkers[] = {
    "wcslen", "wcscpy", "wcsncpy", "wcscat", "wcscmp", "wcsncmp",
    "wcsstr", "wcschr", "wcsrchr", "wcspbrk", "wcsspn", "wcscspn",
    "wcstok", "wmemcpy", "wmemmove", "wmemset", "wmemcmp", "wmemchr",
    "wcstol", "wcstoll", "wcstoul", "wcstoull", "wcstod", "wcstof",
    "wcstold", "wcstoimax", "wcstoumax", "wcsftime", "mbrtowc",
    "wcrtomb", "mbsrtowcs", "wcsrtombs", "mbsinit", "fgetws", "fputws",
    "swprintf", "iswalpha", "iswdigit", "iswspace", "iswalnum",
    "iswupper", "iswlower", "iswxdigit", "iswprint", "iswgraph",
    "iswpunct", "iswcntrl", "iswblank", "iswctype", "towupper",
    "towlower", "wctype", "wctrans", "towctrans"
};
#endif /* !WCHAR_CHECKED_HOST_ONLY */

int main(void) {
    int total, failed;
    unsigned long digest_off, digest_on, digest_begin;
    int fail_off, fail_on;
#ifndef _WIN32
    static char logbuf[16384];
    Cap cap;
    size_t n;
#ifndef WCHAR_CHECKED_HOST_ONLY
    size_t i;
    int all_markers;
#endif
#endif

    if (setlocale(LC_ALL, "C") == NULL) {
        printf("FINDING: %s:%d: setlocale(LC_ALL,\"C\") failed\n",
               __FILE__, __LINE__);
        return 1;
    }

    /* ---- Pass 1: logging OFF, verbose, official counts --------------- */
    g_quiet = 0;
    cplus_libc_log_set_enabled(0);
    g_checks = 0;
    g_failures = 0;
    g_digest = 2166136261UL;
    run_checks();
    total = g_checks;
    failed = g_failures;
    digest_off = g_digest;
    digest_begin = 2166136261UL;
    fail_off = g_failures;

    /* ---- Pass 2: logging ON, quiet, captured ------------------------- */
    g_quiet = 1;
    g_checks = 0;
    g_failures = 0;
    g_digest = digest_begin;
#ifndef _WIN32
    if (cap_begin(&cap) == 0) {
        cplus_libc_log_set_enabled(1);
        run_checks();
        cplus_libc_log_set_enabled(0);
        n = cap_end(&cap, logbuf, sizeof logbuf);
    } else {
        cplus_libc_log_set_enabled(1);
        run_checks();
        cplus_libc_log_set_enabled(0);
        n = 0u;
    }
#else
    cplus_libc_log_set_enabled(1);
    run_checks();
    cplus_libc_log_set_enabled(0);
#endif
    digest_on = g_digest;
    fail_on = g_failures;

    g_quiet = 0;
    cplus_libc_log_set_enabled(0);

    /* ---- Logging parity checks (counted once) ------------------------ */
    ++total;
    if (digest_off != digest_on) {
        ++failed;
        printf("FINDING: logging parity digest mismatch: off=%lu on=%lu\n",
               digest_off, digest_on);
    }
    ++total;
    if (fail_off != fail_on) {
        ++failed;
        printf("FINDING: logging parity failure count: off=%d on=%d\n",
               fail_off, fail_on);
    }

#ifndef _WIN32
#ifndef WCHAR_CHECKED_HOST_ONLY
    /* Entry markers present in the ON pass (C+ libc layer only). */
    all_markers = 1;
    for (i = 0u; i < sizeof kMarkers / sizeof kMarkers[0]; ++i) {
        char needle[96];
        (void)snprintf(needle, sizeof needle, LOG_PREFIX "%s: enter",
                       kMarkers[i]);
        if (strstr(logbuf, needle) == NULL) {
            all_markers = 0;
            if (!g_quiet)
                printf("FINDING: missing logging marker for %s\n",
                       kMarkers[i]);
        }
    }
    ++total;
    if (!all_markers) ++failed;

    /* No entry markers when logging is disabled. */
    {
        mbstate_t st;
        memset(&st, 0, sizeof st);
        if (cap_begin(&cap) == 0) {
            (void)cplus_wcslen(L"x");
            (void)cplus_iswalpha((wint_t)L'a');
            (void)cplus_mbsinit(&st);
            n = cap_end(&cap, logbuf, sizeof logbuf);
        } else {
            n = 0u;
        }
    }
    ++total;
    if (strstr(logbuf, LOG_PREFIX) != NULL) {
        ++failed;
        printf("FINDING: logging marker emitted while disabled\n");
    }
#else
    printf("note: plain-host layer has no C+ logger; marker checks skipped\n");
#endif
    (void)n;
#else
    printf("note: stderr capture unavailable on Windows; marker parity "
           "checked by source contract only\n");
#endif

    printf("wchar_checked: %d/%d checks ok\n", total - failed, total);
    return failed != 0;
}
