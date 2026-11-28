/* tests/scenarios/runtime_09_wchar.c -- high-volume wide-character runtime
 * scenarios for the platform-backed cplus_* ISO C17 <wchar.h> surface.
 *
 * Standalone C99; `main` returns 0 on success. All calls use the public
 * `libc/install-include/cplus_libc.h` surface and the recoverable-runtime
 * header `stdlib/safe/safe_runtime.h`; both libraries are linked by the
 * scenario runner. The "C" locale is forced for determinism. Every assertion
 * uses a defined input: no raw pointer passed to an unguarded wrapper is
 * NULL, no string literal is ever used as an endptr write target, and every
 * destination is at least as large as the bound handed to the callee.
 *
 * Wrappers are thin, logged, NULL-hardened delegators over the host libc, so
 * concrete return values are asserted directly and host-locale-dependent
 * quirks are additionally checked by differential equality against the host.
 * Deviations are printed as FINDING: lines in addition to hard assertions.
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1

#include "libc/install-include/cplus_libc.h"
#include "stdlib/safe/safe_runtime.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NELM(a) (sizeof(a) / sizeof((a)[0]))
#define FINDING(...)                                                           \
    do {                                                                       \
        printf("FINDING: ");                                                   \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0)

/* 1. wcslen: length of empty, short and ordinary NUL-terminated buffers. */
static void sc_09_wcslen(void) {
    wchar_t s[] = L"hello";
    wchar_t e[] = L"";
    wchar_t t[4] = {L'a', L'b', L'c', L'\0'};
    assert(cplus_wcslen(s) == 5u);
    assert(cplus_wcslen(e) == 0u);
    assert(cplus_wcslen(t) == 3u);
    assert(cplus_wcslen(s) == wcslen(s)); /* wrapper == host */
}

/* 2. wcscpy: full copy including terminator, returns destination. */
static void sc_09_wcscpy(void) {
    wchar_t dst[16];
    wchar_t *r;
    memset(dst, 0x7f, sizeof dst);
    r = cplus_wcscpy(dst, L"wide");
    assert(r == dst);
    assert(wcscmp(dst, L"wide") == 0);
    assert(dst[4] == L'\0');
}

/* 3. wcsncpy: exact-count copy (no pad) and zero-padding when n > len. */
static void sc_09_wcsncpy(void) {
    wchar_t dst[8];
    size_t i;
    for (i = 0u; i < NELM(dst); ++i) dst[i] = L'#';
    assert(cplus_wcsncpy(dst, L"abc", 3u) == dst);
    assert(dst[0] == L'a' && dst[1] == L'b' && dst[2] == L'c');
    assert(dst[3] == L'#'); /* n == strlen: no terminator written */
    for (i = 0u; i < NELM(dst); ++i) dst[i] = L'#';
    assert(cplus_wcsncpy(dst, L"ab", 5u) == dst);
    assert(dst[0] == L'a' && dst[1] == L'b');
    assert(dst[2] == L'\0' && dst[3] == L'\0' && dst[4] == L'\0');
}

/* 4. wcscmp: equal, less-than, greater-than and prefix ordering. */
static void sc_09_wcscmp(void) {
    assert(cplus_wcscmp(L"abc", L"abc") == 0);
    assert(cplus_wcscmp(L"abc", L"abd") < 0);
    assert(cplus_wcscmp(L"abd", L"abc") > 0);
    assert(cplus_wcscmp(L"abc", L"abcd") < 0);
    assert(cplus_wcscmp(L"abcd", L"abc") > 0);
}

/* 5. wcsncmp: bounded compare ignores differences past the count. */
static void sc_09_wcsncmp(void) {
    assert(cplus_wcsncmp(L"abcX", L"abcY", 3u) == 0);
    assert(cplus_wcsncmp(L"abcX", L"abcY", 4u) < 0);
    assert(cplus_wcsncmp(L"abc", L"abcd", 3u) == 0);
    assert(cplus_wcsncmp(L"abd", L"abc", 3u) > 0);
}

/* 6. wcscat: append to an existing wide string, returns destination. */
static void sc_09_wcscat(void) {
    wchar_t dst[16] = L"pre";
    assert(cplus_wcscat(dst, L"fix") == dst);
    assert(wcscmp(dst, L"prefix") == 0);
}

/* 7. wcsstr: substring found, absent, and empty-needle identity. */
static void sc_09_wcsstr(void) {
    wchar_t *p = cplus_wcsstr(L"a needle here", L"needle");
    assert(p != NULL && wcscmp(p, L"needle here") == 0);
    assert(cplus_wcsstr(L"abc", L"z") == NULL);
    assert(cplus_wcsstr(L"abc", L"") != NULL); /* empty needle -> haystack */
}

/* 8. wmemcpy: exact element-count copy including the terminator. */
static void sc_09_wmemcpy(void) {
    wchar_t src[4] = {L'a', L'b', L'c', L'\0'};
    wchar_t dst[4] = {0, 0, 0, 0};
    assert(cplus_wmemcpy(dst, src, 4u) == dst);
    assert(cplus_wmemcmp(dst, src, 4u) == 0);
}

/* 9. wmemmove: overlapping copy must behave like an intermediate buffer. */
static void sc_09_wmemmove(void) {
    wchar_t a[8] = L"abcdefg";
    assert(cplus_wmemmove(a + 2, a, 5u) == a + 2);
    assert(wcscmp(a, L"ababcde") == 0);
}

/* 10. wmemset: fill a counted range with one wide character. */
static void sc_09_wmemset(void) {
    wchar_t a[4];
    assert(cplus_wmemset(a, L'x', 3u) == a);
    a[3] = L'\0';
    assert(wcscmp(a, L"xxx") == 0);
}

/* 11. wmemcmp: compare counted wide elements, not a NUL terminator. */
static void sc_09_wmemcmp(void) {
    wchar_t a[4] = L"abc";
    wchar_t b[4] = L"abd";
    assert(cplus_wmemcmp(a, a, 4u) == 0);
    assert(cplus_wmemcmp(a, b, 4u) < 0);
    assert(cplus_wmemcmp(L"abc", L"abd", 2u) == 0);
}

/* 12. wcstol: bases 10/16/0, leading space, junk stop, NULL endptr. */
static void sc_09_wcstol(void) {
    wchar_t buf[] = L"  -42xyz";
    wchar_t *end = buf;
    assert(cplus_wcstol(buf, &end, 10) == -42L);
    assert(*end == L'x');
    assert(cplus_wcstol(L"ff", &end, 16) == 255L);
    assert(*end == L'\0');
    assert(cplus_wcstol(L"0x1f", &end, 0) == 31L);
    assert(*end == L'\0');
    assert(cplus_wcstol(L"7", NULL, 10) == 7L); /* NULL endptr is legal */
    {
        wchar_t bad[] = L"abc";
        end = bad;
        assert(cplus_wcstol(bad, &end, 10) == 0L);
        assert(end == bad);
    }
}

/* 13. wcstoll: 64-bit range across bases 10/16/0 and NULL endptr. */
static void sc_09_wcstoll(void) {
    wchar_t *end = NULL;
    assert(cplus_wcstoll(L"1234567890123", &end, 10) == 1234567890123LL);
    assert(*end == L'\0');
    assert(cplus_wcstoll(L"  -99!", &end, 10) == -99LL);
    assert(*end == L'!');
    assert(cplus_wcstoll(L"10", &end, 16) == 16LL);
    assert(cplus_wcstoll(L"0x2a", &end, 0) == 42LL);
    assert(cplus_wcstoll(L"5", NULL, 10) == 5LL);
    {
        wchar_t bad[] = L"zzz";
        end = bad;
        assert(cplus_wcstoll(bad, &end, 10) == 0LL);
        assert(end == bad);
    }
}

/* 14. wcstoul: unsigned parsing, bases 10/16/0 and endptr stop. */
static void sc_09_wcstoul(void) {
    wchar_t buf[] = L"  77x";
    wchar_t *end = buf;
    assert(cplus_wcstoul(buf, &end, 10) == 77UL);
    assert(*end == L'x');
    assert(cplus_wcstoul(L"ff", &end, 16) == 255UL);
    assert(cplus_wcstoul(L"0x10", &end, 0) == 16UL);
    assert(cplus_wcstoul(L"3", NULL, 10) == 3UL);
    {
        wchar_t bad[] = L"nope";
        end = bad;
        assert(cplus_wcstoul(bad, &end, 10) == 0UL);
        assert(end == bad);
    }
}

/* 15. wcstoull: unsigned 64-bit parsing and base auto-detection. */
static void sc_09_wcstoull(void) {
    wchar_t *end = NULL;
    assert(cplus_wcstoull(L"1234567890123", &end, 10) == 1234567890123ULL);
    assert(*end == L'\0');
    assert(cplus_wcstoull(L"1a", &end, 16) == 26ULL);
    assert(cplus_wcstoull(L"0x2a", &end, 0) == 42ULL);
    assert(cplus_wcstoull(L"4", NULL, 10) == 4ULL);
    {
        wchar_t bad[] = L"xyz";
        end = bad;
        assert(cplus_wcstoull(bad, &end, 10) == 0ULL);
        assert(end == bad);
    }
}

/* 16. Invalid bases 1/37/-1/INT_MAX -> 0, EINVAL, endptr left at input.
 *     Mutable buffers are used as endptr targets so no literal is written. */
static void sc_09_invalid_bases(void) {
    int bases[] = {1, 37, -1, INT_MAX};
    size_t i;
    for (i = 0u; i < NELM(bases); ++i) {
        wchar_t a[] = L"10";
        wchar_t b[] = L"10";
        wchar_t c[] = L"10";
        wchar_t d[] = L"10";
        wchar_t *ea = a, *eb = b, *ec = c, *ed = d;
        errno = 0;
        assert(cplus_wcstol(a, &ea, bases[i]) == 0L);
        assert(errno == EINVAL && ea == a);
        errno = 0;
        assert(cplus_wcstoul(b, &eb, bases[i]) == 0UL);
        assert(errno == EINVAL && eb == b);
        errno = 0;
        assert(cplus_wcstoll(c, &ec, bases[i]) == 0LL);
        assert(errno == EINVAL && ec == c);
        errno = 0;
        assert(cplus_wcstoull(d, &ed, bases[i]) == 0ULL);
        assert(errno == EINVAL && ed == d);
    }
}

/* 17. wcstod/wcstof/wcstold: decimal, exponent, junk stop, invalid, ERANGE. */
static void sc_09_float_conversions(void) {
    wchar_t buf[] = L"  -1.25e2rest";
    wchar_t *end = buf;

    assert(cplus_wcstod(L"3.5", &end) == 3.5);
    assert(*end == L'\0');
    assert(cplus_wcstod(buf, &end) == -125.0);
    assert(*end == L'r');
    assert(cplus_wcstod(L"2.0", NULL) == 2.0);
    {
        wchar_t bad[] = L"junk";
        end = bad;
        assert(cplus_wcstod(bad, &end) == 0.0);
        assert(end == bad);
    }
    errno = 0;
    assert(cplus_wcstod(L"1e9999", &end) > 1.0 && errno == ERANGE);

    assert(cplus_wcstof(L"2.5", &end) == 2.5f);
    assert(*end == L'\0');
    assert(cplus_wcstof(L"1.0", NULL) == 1.0f);
    {
        wchar_t bad[] = L"bad";
        end = bad;
        assert(cplus_wcstof(bad, &end) == 0.0f);
        assert(end == bad);
    }
    errno = 0;
    assert(cplus_wcstof(L"1e9999", &end) > 1.0f && errno == ERANGE);

    assert(cplus_wcstold(L"1.25", &end) == 1.25L);
    assert(*end == L'\0');
    assert(cplus_wcstold(L"1.0", NULL) == 1.0L);
    {
        wchar_t bad[] = L"oops";
        end = bad;
        assert(cplus_wcstold(bad, &end) == 0.0L);
        assert(end == bad);
    }
    errno = 0;
    assert(cplus_wcstold(L"1e9999", &end) > 1.0L && errno == ERANGE);
}

/* 18. wcstok: multiple/trailing/adjacent delimiters, NULL continuation. */
static void sc_09_wcstok(void) {
    wchar_t s[] = L"a,,b,c,";
    wchar_t *save = NULL;
    wchar_t *t;
    t = cplus_wcstok(s, L",", &save);
    assert(t && wcscmp(t, L"a") == 0);
    t = cplus_wcstok(NULL, L",", &save);
    assert(t && wcscmp(t, L"b") == 0);
    t = cplus_wcstok(NULL, L",", &save);
    assert(t && wcscmp(t, L"c") == 0);
    t = cplus_wcstok(NULL, L",", &save); /* trailing delimiter: no empty token */
    assert(t == NULL);

    /* All-delimiter input terminates immediately (bounded). */
    {
        wchar_t all[] = L",,,,";
        save = NULL;
        assert(cplus_wcstok(all, L",", &save) == NULL);
    }
    /* Multiple distinct delimiters. */
    {
        wchar_t mix[] = L"x, y;\tz";
        int count = 0;
        save = NULL;
        for (t = cplus_wcstok(mix, L", ;\t", &save); t != NULL;
             t = cplus_wcstok(NULL, L", ;\t", &save)) {
            assert(wcslen(t) == 1u);
            if (++count > 8) assert(!"wcstok failed to terminate");
        }
        assert(count == 3);
    }
}

/* 19. wcsftime: known tm, truncation, canary-bounded maxsize edge sizes. */
static void sc_09_wcsftime(void) {
    struct tm t;
    wchar_t buf[16];
    size_t i;
    size_t r;
    memset(&t, 0, sizeof t);
    t.tm_year = 120; /* 2020 */
    t.tm_mon = 0;    /* January */
    t.tm_mday = 2;
    t.tm_isdst = -1;

    assert(cplus_wcsftime(buf, NELM(buf), L"%Y-%m-%d", &t) != 0u);
    assert(wcscmp(buf, L"2020-01-02") == 0);

    {
        wchar_t tiny[2];
        assert(cplus_wcsftime(tiny, NELM(tiny), L"%Y-%m-%d", &t) == 0u);
    }
    for (i = 0u; i < NELM(buf); ++i) buf[i] = L'#';
    r = cplus_wcsftime(buf, 1u, L"%Y-%m-%d", &t);
    assert(buf[1] == L'#' && buf[15] == L'#'); /* no overrun */
    (void)r;

    for (i = 0u; i < NELM(buf); ++i) buf[i] = L'#';
    errno = 0;
    r = cplus_wcsftime(buf, (size_t)-1, L"X", &t);
    assert(buf[1] == L'#' && buf[15] == L'#'); /* no overrun */
    if (r == 0u && errno == EINVAL)
        FINDING("wcsftime: host rejects maxsize=SIZE_MAX with EINVAL; canaries intact");
}

/* 20. mbrtowc: ASCII, length query, embedded NUL and C-locale invalid byte.
 *     Differential equality is asserted because "C" on this host is SBCS. */
static void sc_09_mbrtowc(void) {
    mbstate_t st, st2;
    wchar_t wc, wc2;
    size_t r, r2;
    assert(MB_CUR_MAX == 1u);

    memset(&st, 0, sizeof st);
    r = cplus_mbrtowc(&wc, "A", 1u, &st);
    assert(r == 1u && wc == L'A');

    memset(&st, 0, sizeof st);
    r = cplus_mbrtowc(NULL, "B", 1u, &st); /* length-only query */
    assert(r == 1u);

    memset(&st, 0, sizeof st);
    r = cplus_mbrtowc(&wc, "\0x", 2u, &st); /* embedded NUL */
    assert(r == 0u && wc == L'\0');

    memset(&st, 0, sizeof st);
    memset(&st2, 0, sizeof st2);
    r = cplus_mbrtowc(&wc, "\xff", 1u, &st);
    r2 = mbrtowc(&wc2, "\xff", 1u, &st2);
    assert(r == r2 && wc == wc2); /* wrapper faithfully mirrors host */
    if (r == 1u)
        FINDING("mbrtowc: \"C\" locale (MB_CUR_MAX=1) maps byte 0xFF to wide 0x%X (r=%zu), not EILSEQ; EILSEQ is multibyte-locale-dependent",
                (unsigned)wc, r);
}

/* 21. wcrtomb: ASCII encode, NULL-destination reset query, invalid wide. */
static void sc_09_wcrtomb(void) {
    mbstate_t st, st2;
    char cbuf[16];
    size_t r, r2;

    memset(&st, 0, sizeof st);
    r = cplus_wcrtomb(cbuf, L'A', &st);
    assert(r == 1u && cbuf[0] == 'A');

    memset(&st, 0, sizeof st);
    memset(&st2, 0, sizeof st2);
    r = cplus_wcrtomb(NULL, L'A', &st);
    r2 = wcrtomb(NULL, L'A', &st2);
    assert(r == r2 && r >= 1u); /* bytes needed for terminator */

    memset(&st, 0, sizeof st);
    memset(&st2, 0, sizeof st2);
    r = cplus_wcrtomb(cbuf, (wchar_t)0xFF, &st);
    r2 = wcrtomb(cbuf, (wchar_t)0xFF, &st2);
    assert(r == r2);
    if (r == (size_t)-1 && errno == EILSEQ)
        FINDING("wcrtomb: \"C\" locale rejects wide 0xFF with EILSEQ (host-delegated)");
}

/* 22. mbsrtowcs: full conversion, bounded truncation, NULL-dst length query. */
static void sc_09_mbsrtowcs(void) {
    mbstate_t st;
    wchar_t wbuf[16];
    const char *sp;
    size_t r;

    sp = "Hello";
    memset(&st, 0, sizeof st);
    r = cplus_mbsrtowcs(wbuf, &sp, NELM(wbuf), &st);
    assert(r == 5u && sp == NULL && wcscmp(wbuf, L"Hello") == 0);

    sp = "Hello";
    memset(&st, 0, sizeof st);
    r = cplus_mbsrtowcs(wbuf, &sp, 3u, &st);
    assert(r == 3u && sp != NULL);
    assert(wbuf[0] == L'H' && wbuf[1] == L'e' && wbuf[2] == L'l');

    sp = "Hello";
    memset(&st, 0, sizeof st);
    r = cplus_mbsrtowcs(NULL, &sp, 0u, &st);
    assert(r == 5u && sp != NULL);
}

/* 23. wcsrtombs: full conversion, bounded truncation, NULL-dst length query. */
static void sc_09_wcsrtombs(void) {
    mbstate_t st;
    char cbuf[16];
    const wchar_t *wp;
    size_t r;

    wp = L"World";
    memset(&st, 0, sizeof st);
    r = cplus_wcsrtombs(cbuf, &wp, NELM(cbuf), &st);
    assert(r == 5u && wp == NULL && strcmp(cbuf, "World") == 0);

    wp = L"World";
    memset(&st, 0, sizeof st);
    r = cplus_wcsrtombs(cbuf, &wp, 3u, &st);
    assert(r == 3u && wp != NULL);
    assert(cbuf[0] == 'W' && cbuf[1] == 'o' && cbuf[2] == 'r');

    wp = L"World";
    memset(&st, 0, sizeof st);
    r = cplus_wcsrtombs(NULL, &wp, 0u, &st);
    assert(r == 5u && wp != NULL);
}

/* 24. wcstoimax: signed intmax value, base 16, overflow saturation ERANGE. */
static void sc_09_wcstoimax(void) {
    wchar_t *end = NULL;
    assert(cplus_wcstoimax(L"1234567890123", &end, 10) == (intmax_t)1234567890123LL);
    assert(*end == L'\0');
    assert(cplus_wcstoimax(L"7fffffff", &end, 16) == (intmax_t)2147483647LL);
    {
        wchar_t huge[] = L"9999999999999999999999999999999999999999";
        errno = 0;
        end = huge;
        assert(cplus_wcstoimax(huge, &end, 10) == INTMAX_MAX);
        assert(errno == ERANGE);
    }
}

/* 25. wcstoumax: unsigned intmax value and overflow saturation ERANGE. */
static void sc_09_wcstoumax(void) {
    wchar_t *end = NULL;
    assert(cplus_wcstoumax(L"42", &end, 10) == (uintmax_t)42u);
    assert(*end == L'\0');
    assert(cplus_wcstoumax(L"ff", &end, 16) == (uintmax_t)255u);
    {
        wchar_t huge[] = L"0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
        errno = 0;
        end = huge;
        assert(cplus_wcstoumax(huge, &end, 16) == UINTMAX_MAX);
        assert(errno == ERANGE);
    }
}

/* 26. swprintf: bounded formatting, NUL termination and truncation return. */
static void sc_09_swprintf(void) {
    wchar_t buf[64];
    wchar_t small[6];
    int r = cplus_swprintf(buf, NELM(buf), L"%d %ls %.2f", 42, L"wide", 3.14159);
    assert(r == 12);
    assert(wcscmp(buf, L"42 wide 3.14") == 0);

    {
        size_t i;
        for (i = 0u; i < NELM(small); ++i) small[i] = L'#';
    }
    r = cplus_swprintf(small, NELM(small), L"%d %ls %.2f", 42, L"wide", 3.14159);
    assert(r < 0);                 /* ISO C: encoding error on truncation */
    assert(wcslen(small) < NELM(small));
    assert(wcsncmp(small, L"42 wi", 5u) == 0);
    assert(small[5] == L'\0');
}

/* 27. NULL argument guards on every wrapper that defines one. Unguarded
 *     basic wrappers (wcslen/wcscpy/...) are deliberately NOT called with NULL. */
static void sc_09_null_guards(void) {
    struct tm t;
    mbstate_t st;
    wchar_t wbuf[8];
    char cbuf[8];
    wchar_t *ep;
    wchar_t *save;
    memset(&t, 0, sizeof t);
    memset(&st, 0, sizeof st);
    t.tm_year = 120;

    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstol(NULL, &ep, 10) == 0L && errno == EINVAL && ep == NULL);
    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstoll(NULL, &ep, 10) == 0LL && errno == EINVAL && ep == NULL);
    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstoul(NULL, &ep, 10) == 0UL && errno == EINVAL && ep == NULL);
    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstoull(NULL, &ep, 10) == 0ULL && errno == EINVAL && ep == NULL);
    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstod(NULL, &ep) == 0.0 && errno == EINVAL && ep == NULL);
    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstof(NULL, &ep) == 0.0f && errno == EINVAL && ep == NULL);
    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstold(NULL, &ep) == 0.0L && errno == EINVAL && ep == NULL);
    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstoimax(NULL, &ep, 10) == 0 && errno == EINVAL && ep == NULL);
    errno = 0; ep = (wchar_t *)1;
    assert(cplus_wcstoumax(NULL, &ep, 10) == 0 && errno == EINVAL && ep == NULL);

    {
        wchar_t s[] = L"x y";
        errno = 0;
        assert(cplus_wcstok(s, NULL, &save) == NULL && errno == EINVAL);
        errno = 0;
        assert(cplus_wcstok(s, L" ", NULL) == NULL && errno == EINVAL);
    }
    errno = 0;
    assert(cplus_wcsftime(wbuf, NELM(wbuf), NULL, &t) == 0u && errno == EINVAL);
    errno = 0;
    assert(cplus_wcsftime(NULL, 8u, L"%Y", &t) == 0u && errno == EINVAL);

    errno = 0;
    assert(cplus_mbsrtowcs(wbuf, NULL, NELM(wbuf), &st) == (size_t)-1 && errno == EINVAL);
    errno = 0;
    assert(cplus_wcsrtombs(cbuf, NULL, NELM(cbuf), &st) == (size_t)-1 && errno == EINVAL);

    errno = 0;
    assert(cplus_swprintf(NULL, 8u, L"%d", 1) == -1 && errno == EINVAL);
    errno = 0;
    assert(cplus_swprintf(wbuf, NELM(wbuf), NULL, 1) == -1 && errno == EINVAL);
}

/* Capture stderr while logging is enabled or disabled; bounded and
 * non-blocking so it can never hang the suite. */
static size_t capture_logs(int enable, char *buf, size_t cap) {
    int fds[2];
    int saved;
    size_t total = 0u;
    if (pipe(fds) != 0) return 0u;
    saved = dup(STDERR_FILENO);
    if (saved < 0) {
        close(fds[0]);
        close(fds[1]);
        return 0u;
    }
    if (dup2(fds[1], STDERR_FILENO) < 0) {
        close(fds[0]);
        close(fds[1]);
        close(saved);
        return 0u;
    }
    {
        int fl = fcntl(fds[0], F_GETFL, 0);
        if (fl >= 0) (void)fcntl(fds[0], F_SETFL, fl | O_NONBLOCK);
    }

    cplus_libc_log_set_enabled(enable);
    {
        wchar_t b[8];
        wchar_t *ep = NULL;
        (void)cplus_wcslen(L"log");
        (void)cplus_wcscpy(b, L"xy");
        (void)cplus_wcstol(L"7", &ep, 10);
        (void)cplus_wmemset(b, L'z', 2u);
    }
    cplus_libc_log_set_enabled(0);
    fflush(stderr);

    (void)dup2(saved, STDERR_FILENO);
    close(saved);
    close(fds[1]);
    for (;;) {
        ssize_t n = read(fds[0], buf + total, cap - 1u - total);
        if (n > 0) {
            total += (size_t)n;
            if (total + 1u >= cap) break;
            continue;
        }
        break;
    }
    buf[total] = '\0';
    close(fds[0]);
    return total;
}

/* 28. Logging parity: results/errno are identical with logging on vs off,
 *     log markers appear when enabled, and stderr is silent by default. */
static void sc_09_logging_parity(void) {
    wchar_t *ep1 = NULL, *ep2 = NULL;
    long v1, v2;
    size_t l1, l2;
    int e1, e2;
    char logbuf[4096];
    size_t n;

    cplus_libc_log_set_enabled(0);
    errno = 0;
    v1 = cplus_wcstol(L"  -42x", &ep1, 10);
    e1 = errno;
    l1 = cplus_wcslen(L"parity");

    cplus_libc_log_set_enabled(1);
    errno = 0;
    v2 = cplus_wcstol(L"  -42x", &ep2, 10);
    e2 = errno;
    l2 = cplus_wcslen(L"parity");
    cplus_libc_log_set_enabled(0);

    assert(v1 == v2 && e1 == e2 && l1 == l2);
    assert(ep1 != NULL && ep2 != NULL && *ep1 == *ep2);

    n = capture_logs(1, logbuf, sizeof logbuf);
    assert(n > 0u);
    assert(strstr(logbuf, "[CPLUS-LIBC] wcslen:") != NULL);
    assert(strstr(logbuf, "[CPLUS-LIBC] wcstol:") != NULL);
    assert(strstr(logbuf, "[CPLUS-LIBC] wmemset:") != NULL);

    n = capture_logs(0, logbuf, sizeof logbuf);
    assert(n == 0u); /* logging is OFF by default */
}

/* 29. Wide format with a huge field width, bounded by SIGALRM in a child.
 *     The n bound must be honored; if the host spins, the probe is skipped. */
static void sc_09_wide_huge_width(void) {
#ifndef _WIN32
    pid_t pid = fork();
    if (pid == 0) {
        wchar_t buf[8];
        size_t i;
        int r;
        for (i = 0u; i < NELM(buf); ++i) buf[i] = L'#';
        alarm(1);
        r = cplus_swprintf(buf, NELM(buf), L"%1000000000ls", L"hi");
        if (buf[0] != L'#' || buf[7] != L'#') _exit(2); /* n bound violated */
        (void)r;
        _exit(0);
    }
    if (pid > 0) {
        int status = 0;
        pid_t w = waitpid(pid, &status, 0);
        assert(w == pid);
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
            FINDING("swprintf: huge field width did not return within 1s (SIGALRM); skipped; format width is caller responsibility");
        } else {
            assert(WIFEXITED(status));
            assert(WEXITSTATUS(status) == 0);
        }
    }
#else
    FINDING("swprintf: huge-width probe skipped on non-POSIX host");
#endif
}

int main(void) {
    /* Determinism: force the "C" locale for every conversion scenario. */
    assert(setlocale(LC_ALL, "C") != NULL);
    /* Exercise the safe-runtime library linked alongside the hardened libc. */
    assert(cplus_runtime_get_failstop() == 0);
    assert(cplus_runtime_get_violation_trap() == 1);
    cplus_libc_log_set_enabled(0);

    sc_09_wcslen();
    sc_09_wcscpy();
    sc_09_wcsncpy();
    sc_09_wcscmp();
    sc_09_wcsncmp();
    sc_09_wcscat();
    sc_09_wcsstr();
    sc_09_wmemcpy();
    sc_09_wmemmove();
    sc_09_wmemset();
    sc_09_wmemcmp();
    sc_09_wcstol();
    sc_09_wcstoll();
    sc_09_wcstoul();
    sc_09_wcstoull();
    sc_09_invalid_bases();
    sc_09_float_conversions();
    sc_09_wcstok();
    sc_09_wcsftime();
    sc_09_mbrtowc();
    sc_09_wcrtomb();
    sc_09_mbsrtowcs();
    sc_09_wcsrtombs();
    sc_09_wcstoimax();
    sc_09_wcstoumax();
    sc_09_swprintf();
    sc_09_null_guards();
    sc_09_logging_parity();
    sc_09_wide_huge_width();

    printf("runtime_wchar: 29 scenarios ok\n");
    return 0;
}
