/* Standalone C99 tests for the platform-backed cplus_wcstol/wcstoll/wcstoul/
 * wcstoull/wcstod/wcstof/wcstold/wcstok/wcsftime wrappers. Only cplus_* names
 * are exercised; the host functions are never interposed. */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

long cplus_wcstol(const wchar_t *, wchar_t **, int);
long long cplus_wcstoll(const wchar_t *, wchar_t **, int);
unsigned long cplus_wcstoul(const wchar_t *, wchar_t **, int);
unsigned long long cplus_wcstoull(const wchar_t *, wchar_t **, int);
double cplus_wcstod(const wchar_t *, wchar_t **);
float cplus_wcstof(const wchar_t *, wchar_t **);
long double cplus_wcstold(const wchar_t *, wchar_t **);
wchar_t *cplus_wcstok(wchar_t *, const wchar_t *, wchar_t **);
size_t cplus_wcsftime(wchar_t *, size_t, const wchar_t *, const struct tm *);

static void test_wcstol(void) {
    wchar_t *end = NULL;

    assert(cplus_wcstol(L"123", &end, 10) == 123L);
    assert(*end == L'\0');

    assert(cplus_wcstol(L"  -42xyz", &end, 10) == -42L);
    assert(*end == L'x'); /* leading whitespace skipped, stops at first junk */

    assert(cplus_wcstol(L"ff", &end, 16) == 255L);
    assert(*end == L'\0');

    assert(cplus_wcstol(L"0x1f", &end, 0) == 31L); /* base 0 auto-detect */
    assert(*end == L'\0');

    /* invalid input: 0 and endptr left at the start of the string */
    {
        const wchar_t *bad = L"abc";
        assert(cplus_wcstol(bad, &end, 10) == 0L);
        assert(end == bad);
    }

    /* a NULL endptr is legal and must not be rejected */
    assert(cplus_wcstol(L"7", NULL, 10) == 7L);

    /* NULL required string: hardened failure */
    errno = 0;
    assert(cplus_wcstol(NULL, &end, 10) == 0L);
    assert(errno == EINVAL);
    assert(end == NULL);
}

static void test_wcstoll(void) {
    wchar_t *end = NULL;

    assert(cplus_wcstoll(L"1234567890123", &end, 10) == 1234567890123LL);
    assert(*end == L'\0');

    assert(cplus_wcstoll(L"  -99!", &end, 10) == -99LL);
    assert(*end == L'!');

    assert(cplus_wcstoll(L"10", &end, 16) == 16LL);
    assert(*end == L'\0');

    {
        const wchar_t *bad = L"zzz";
        assert(cplus_wcstoll(bad, &end, 10) == 0LL);
        assert(end == bad);
    }

    assert(cplus_wcstoll(L"5", NULL, 10) == 5LL);

    errno = 0;
    assert(cplus_wcstoll(NULL, &end, 10) == 0LL);
    assert(errno == EINVAL);
}

static void test_wcstoul(void) {
    wchar_t *end = NULL;

    assert(cplus_wcstoul(L"42", &end, 10) == 42UL);
    assert(*end == L'\0');

    assert(cplus_wcstoul(L"  77x", &end, 10) == 77UL);
    assert(*end == L'x');

    assert(cplus_wcstoul(L"ff", &end, 16) == 255UL);
    assert(*end == L'\0');

    assert(cplus_wcstoul(L"0x10", &end, 0) == 16UL);
    assert(*end == L'\0');

    {
        const wchar_t *bad = L"nope";
        assert(cplus_wcstoul(bad, &end, 10) == 0UL);
        assert(end == bad);
    }

    assert(cplus_wcstoul(L"3", NULL, 10) == 3UL);

    errno = 0;
    assert(cplus_wcstoul(NULL, &end, 10) == 0UL);
    assert(errno == EINVAL);
}

static void test_wcstoull(void) {
    wchar_t *end = NULL;

    assert(cplus_wcstoull(L"1234567890123", &end, 10) == 1234567890123ULL);
    assert(*end == L'\0');

    assert(cplus_wcstoull(L"  88#", &end, 10) == 88ULL);
    assert(*end == L'#');

    assert(cplus_wcstoull(L"1a", &end, 16) == 26ULL);
    assert(*end == L'\0');

    assert(cplus_wcstoull(L"0x2a", &end, 0) == 42ULL);
    assert(*end == L'\0');

    {
        const wchar_t *bad = L"xyz";
        assert(cplus_wcstoull(bad, &end, 10) == 0ULL);
        assert(end == bad);
    }

    assert(cplus_wcstoull(L"4", NULL, 10) == 4ULL);

    errno = 0;
    assert(cplus_wcstoull(NULL, &end, 10) == 0ULL);
    assert(errno == EINVAL);
}

static void test_float_conversions(void) {
    wchar_t *end = NULL;

    assert(cplus_wcstod(L"3.5", &end) == 3.5);
    assert(*end == L'\0');

    assert(cplus_wcstod(L"  -1.25e2rest", &end) == -125.0);
    assert(*end == L'r');

    {
        const wchar_t *bad = L"junk";
        assert(cplus_wcstod(bad, &end) == 0.0);
        assert(end == bad);
    }
    assert(cplus_wcstod(L"2.0", NULL) == 2.0);

    errno = 0;
    assert(cplus_wcstod(NULL, &end) == 0.0);
    assert(errno == EINVAL);

    assert(cplus_wcstof(L"2.5", &end) == 2.5f);
    assert(*end == L'\0');
    {
        const wchar_t *bad = L"bad";
        assert(cplus_wcstof(bad, &end) == 0.0f);
        assert(end == bad);
    }
    assert(cplus_wcstof(L"1.0", NULL) == 1.0f);
    errno = 0;
    assert(cplus_wcstof(NULL, &end) == 0.0f);
    assert(errno == EINVAL);

    assert(cplus_wcstold(L"1.25", &end) == 1.25L);
    assert(*end == L'\0');
    {
        const wchar_t *bad = L"oops";
        assert(cplus_wcstold(bad, &end) == 0.0L);
        assert(end == bad);
    }
    assert(cplus_wcstold(L"1.0", NULL) == 1.0L);
    errno = 0;
    assert(cplus_wcstold(NULL, &end) == 0.0L);
    assert(errno == EINVAL);
}

static void test_wcstok(void) {
    wchar_t s[] = L"a,b,,c,"; /* leading/trailing/adjacent delimiters */
    wchar_t *save = NULL;
    wchar_t *tok;

    tok = cplus_wcstok(s, L",", &save);
    assert(tok && wcscmp(tok, L"a") == 0);
    tok = cplus_wcstok(NULL, L",", &save);
    assert(tok && wcscmp(tok, L"b") == 0);
    tok = cplus_wcstok(NULL, L",", &save);
    assert(tok && wcscmp(tok, L"c") == 0);
    tok = cplus_wcstok(NULL, L",", &save);
    assert(tok == NULL); /* trailing delimiter yields no empty token */

    /* NULL delimiter and NULL saveptr are hardened failures; a NULL first
     * argument is valid (continuation), so it is intentionally not guarded. */
    {
        wchar_t t[] = L"x y";
        errno = 0;
        assert(cplus_wcstok(t, NULL, &save) == NULL);
        assert(errno == EINVAL);
        errno = 0;
        assert(cplus_wcstok(t, L" ", NULL) == NULL);
        assert(errno == EINVAL);
    }
}

static void test_wcsftime(void) {
    struct tm t;
    wchar_t buf[64];

    memset(&t, 0, sizeof(t));
    t.tm_year = 120; /* 2020 */
    t.tm_mon = 0;    /* January */
    t.tm_mday = 2;
    t.tm_hour = 0;
    t.tm_min = 0;
    t.tm_sec = 0;
    t.tm_isdst = -1;

    assert(cplus_wcsftime(buf, sizeof(buf) / sizeof(buf[0]), L"%Y-%m-%d", &t) != 0);
    assert(wcscmp(buf, L"2020-01-02") == 0);

    /* truncation: tiny buffer returns 0; its contents are indeterminate, so
     * only the return value is asserted. */
    {
        wchar_t tiny[2];
        assert(cplus_wcsftime(tiny, 2, L"%Y-%m-%d", &t) == 0);
    }

    /* NULL format and NULL destination (with a nonzero size) are failures. */
    errno = 0;
    assert(cplus_wcsftime(buf, sizeof(buf) / sizeof(buf[0]), NULL, &t) == 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_wcsftime(NULL, 32, L"%Y", &t) == 0);
    assert(errno == EINVAL);
}

int main(void) {
    test_wcstol();
    test_wcstoll();
    test_wcstoul();
    test_wcstoull();
    test_float_conversions();
    test_wcstok();
    test_wcsftime();
    return 0;
}
