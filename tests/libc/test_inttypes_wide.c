/* Standalone tests for the PLATFORM-BACKED cplus_wcstoimax / cplus_wcstoumax
 * namespaced wrappers. Host wcstoimax/wcstoumax are used only as a differential
 * oracle; the cplus_* functions under test delegate to those same host backends. */
#include <assert.h>
#include <inttypes.h>
#include <wchar.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

/* Functions under test (namespaced wrappers, hosted in inttypes_wide.c). */
extern intmax_t cplus_wcstoimax(const wchar_t *restrict, wchar_t **restrict, int);
extern uintmax_t cplus_wcstoumax(const wchar_t *restrict, wchar_t **restrict, int);

int main(void) {
    wchar_t *end;
    intmax_t si;
    uintmax_t ui;

    /* ---- cplus_wcstoimax: decimal / base-0 hex / leading whitespace ---- */
    errno = 0;
    si = cplus_wcstoimax(L"0x2a", &end, 0);
    assert(si == 42);
    assert(end && wcscmp(end, L"") == 0);

    errno = 0;
    si = cplus_wcstoimax(L"-42", &end, 10);
    assert(si == -42);
    assert(end && *end == L'\0');

    errno = 0;
    si = cplus_wcstoimax(L" 77", &end, 10);
    assert(si == 77);
    assert(end && *end == L'\0');

    /* endptr points just past the last consumed wide char, not the whole buffer. */
    errno = 0;
    si = cplus_wcstoimax(L"123tail", &end, 10);
    assert(si == 123);
    assert(end && wcscmp(end, L"tail") == 0);
    assert(end == (wchar_t *)L"123tail" + 3);

    /* No conversion -> 0, endptr == input. */
    errno = 0;
    {
        const wchar_t *bad = L"not-a-number";
        si = cplus_wcstoimax(bad, &end, 10);
        assert(si == 0);
        assert(end == bad);
    }

    /* NULL endptr is valid and must be accepted. */
    errno = 0;
    assert(cplus_wcstoimax(L"99", NULL, 10) == 99);

    /* Range overflow: INTMAX_MAX with ERANGE (classification/tolerance). */
    errno = 0;
    si = cplus_wcstoimax(L"999999999999999999999999999999999999999999999999999", &end, 10);
    assert(errno == ERANGE);
    assert(si == INTMAX_MAX || si == INTMAX_MIN);

    /* NULL string hardening path: EINVAL, 0, endptr NULL. */
    errno = 0;
    end = (wchar_t *)0x1;
    si = cplus_wcstoimax(NULL, &end, 10);
    assert(si == 0);
    assert(errno == EINVAL);
    assert(end == NULL);
    errno = 0;
    assert(cplus_wcstoimax(NULL, NULL, 10) == 0);
    assert(errno == EINVAL);

    /* ---- cplus_wcstoumax ---- */
    errno = 0;
    ui = cplus_wcstoumax(L"0x2a", &end, 0);
    assert(ui == 42);
    assert(end && *end == L'\0');

    errno = 0;
    ui = cplus_wcstoumax(L"42", &end, 10);
    assert(ui == 42);
    assert(end && *end == L'\0');

    errno = 0;
    ui = cplus_wcstoumax(L" 77", &end, 10);
    assert(ui == 77);
    assert(end && *end == L'\0');

    errno = 0;
    ui = cplus_wcstoumax(L"456tail", &end, 10);
    assert(ui == 456);
    assert(end && wcscmp(end, L"tail") == 0);
    assert(end == (wchar_t *)L"456tail" + 3);

    errno = 0;
    {
        const wchar_t *bad = L"xyz";
        ui = cplus_wcstoumax(bad, &end, 10);
        assert(ui == 0);
        assert(end == bad);
    }

    errno = 0;
    assert(cplus_wcstoumax(L"88", NULL, 10) == 88);

    /* Range overflow: UINTMAX_MAX with ERANGE. */
    errno = 0;
    ui = cplus_wcstoumax(L"999999999999999999999999999999999999999999999999999", &end, 10);
    assert(errno == ERANGE);
    assert(ui == UINTMAX_MAX);

    errno = 0;
    end = (wchar_t *)0x1;
    ui = cplus_wcstoumax(NULL, &end, 10);
    assert(ui == 0);
    assert(errno == EINVAL);
    assert(end == NULL);
    errno = 0;
    assert(cplus_wcstoumax(NULL, NULL, 10) == 0);
    assert(errno == EINVAL);

    /* ---- differential check vs host on valid inputs ---- */
    {
        const wchar_t *samples[] = { L"0", L"-42", L" 77", L"0x2a", L"123abc", L"+17" };
        size_t i;
        for (i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
            wchar_t *he = NULL, *ce = NULL;
            intmax_t hs, cs;
            uintmax_t hu, cu;
            int base = (i == 3) ? 0 : 10;
            errno = 0;
            hs = wcstoimax(samples[i], &he, base);
            errno = 0;
            cs = cplus_wcstoimax(samples[i], &ce, base);
            assert(cs == hs);
            assert(ce == he);
            assert((ce == NULL) == (he == NULL));
            if (ce && he) assert(wcscmp(ce, he) == 0);

            errno = 0;
            hu = wcstoumax(samples[i], &he, base);
            errno = 0;
            cu = cplus_wcstoumax(samples[i], &ce, base);
            assert(cu == hu);
            assert(ce == he);
            assert((ce == NULL) == (he == NULL));
            if (ce && he) assert(wcscmp(ce, he) == 0);
        }
    }

    return 0;
}
