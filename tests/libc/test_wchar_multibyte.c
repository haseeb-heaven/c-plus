/* Standalone conformance smoke test for the cplus_* restartable multibyte and
 * wide-stream wrappers. Uses only ASCII (valid UTF-8) under a guarded locale. */
#include <assert.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

/* local extern declarations (no private headers) */
extern size_t cplus_mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
extern size_t cplus_wcrtomb(char *, wchar_t, mbstate_t *);
extern size_t cplus_mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
extern size_t cplus_wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);
extern wint_t cplus_fputwc(wint_t, FILE *);
extern wint_t cplus_fgetwc(FILE *);
extern wint_t cplus_putwc(wint_t, FILE *);
extern wint_t cplus_getwc(FILE *);
extern wint_t cplus_putwchar(wint_t);
extern wint_t cplus_getwchar(void);
extern wint_t cplus_ungetwc(wint_t, FILE *);

static void reset_state(mbstate_t *st) { memset(st, 0, sizeof(*st)); }

int main(void) {
    mbstate_t st;
    wchar_t wc;
    char cbuf[16];
    wchar_t wbuf[16];
    const char *sp;
    const wchar_t *wp;
    size_t r;

    /* Prefer a UTF-8 locale; fall back to "C" (ASCII still valid in both). */
    if (!setlocale(LC_ALL, "C.UTF-8")) setlocale(LC_ALL, "C");

    /* --- mbrtowc / wcrtomb ASCII round-trip, zeroed state ----------------- */
    reset_state(&st);
    r = cplus_mbrtowc(&wc, "A", 1, &st);
    assert(r == 1 && wc == L'A');

    /* length-only query: pwc == NULL is valid */
    reset_state(&st);
    r = cplus_mbrtowc(NULL, "B", 1, &st);
    assert(r == 1);

    /* embedded NUL -> mbrtowc returns 0 */
    reset_state(&st);
    r = cplus_mbrtowc(&wc, "\0", 1, &st);
    assert(r == 0 && wc == L'\0');

    reset_state(&st);
    r = cplus_wcrtomb(cbuf, L'A', &st);
    assert(r == 1 && cbuf[0] == 'A');

    /* s == NULL is a valid reset query (bytes needed for L'\0') */
    reset_state(&st);
    r = cplus_wcrtomb(NULL, L'A', &st);
    assert(r >= 1);

    /* --- mbsrtowcs / wcsrtombs, bounded destinations + truncation -------- */
    /* full conversion */
    sp = "Hello";
    reset_state(&st);
    r = cplus_mbsrtowcs(wbuf, &sp, 16, &st);
    assert(r == 5 && sp == NULL && wcscmp(wbuf, L"Hello") == 0);

    /* truncation: C17/host return equals n when the destination fills and
     * *sp is left pointing past the last converted char (not NULL). */
    sp = "Hello";
    reset_state(&st);
    r = cplus_mbsrtowcs(wbuf, &sp, 3, &st);
    assert(r == 3 && sp != NULL);
    assert(wbuf[0] == L'H' && wbuf[1] == L'e' && wbuf[2] == L'l');

    /* dst == NULL: length-only, *sp unchanged */
    sp = "Hello";
    reset_state(&st);
    r = cplus_mbsrtowcs(NULL, &sp, 0, &st);
    assert(r == 5 && sp != NULL);

    wp = L"World";
    reset_state(&st);
    r = cplus_wcsrtombs(cbuf, &wp, 16, &st);
    assert(r == 5 && wp == NULL && strcmp(cbuf, "World") == 0);

    /* truncation: r == n and *wp left non-NULL (destination filled). */
    wp = L"World";
    reset_state(&st);
    r = cplus_wcsrtombs(cbuf, &wp, 3, &st);
    assert(r == 3 && wp != NULL);
    assert(cbuf[0] == 'W' && cbuf[1] == 'o' && cbuf[2] == 'r');

    wp = L"World";
    reset_state(&st);
    r = cplus_wcsrtombs(NULL, &wp, 0, &st);
    assert(r == 5 && wp != NULL);

    /* genuinely required endptr NULL -> EINVAL, (size_t)-1 */
    errno = 0;
    reset_state(&st);
    r = cplus_mbsrtowcs(wbuf, NULL, 16, &st);
    assert(r == (size_t)-1 && errno == EINVAL);
    errno = 0;
    reset_state(&st);
    r = cplus_wcsrtombs(cbuf, NULL, 16, &st);
    assert(r == (size_t)-1 && errno == EINVAL);

    /* --- wide stream I/O round-trip via tmpfile() ------------------------ */
    {
        FILE *fp = tmpfile();
        assert(fp != NULL);
        assert(cplus_fputwc(L'X', fp) != WEOF);
        assert(cplus_fputwc(L'Y', fp) != WEOF);
        rewind(fp);
        assert(cplus_fgetwc(fp) == L'X');
        assert(cplus_fgetwc(fp) == L'Y');
        assert(cplus_fgetwc(fp) == WEOF); /* EOF */

        /* putwc/getwc delegate to fputwc/fgetwc */
        rewind(fp);
        assert(cplus_putwc(L'Z', fp) != WEOF);
        rewind(fp);
        assert(cplus_getwc(fp) == L'Z');

        /* ungetwc pushes a wide char back */
        rewind(fp);
        assert(cplus_fputwc(L'Q', fp) != WEOF);
        rewind(fp);
        assert(cplus_fgetwc(fp) == L'Q');
        assert(cplus_ungetwc(L'R', fp) != WEOF);
        assert(cplus_fgetwc(fp) == L'R');
        fclose(fp);
    }

    /* --- NULL FILE* error paths ------------------------------------------ */
    errno = 0; assert(cplus_fputwc(L'A', NULL) == WEOF && errno == EINVAL);
    errno = 0; assert(cplus_fgetwc(NULL) == WEOF && errno == EINVAL);
    errno = 0; assert(cplus_putwc(L'A', NULL) == WEOF && errno == EINVAL);
    errno = 0; assert(cplus_getwc(NULL) == WEOF && errno == EINVAL);
    errno = 0; assert(cplus_ungetwc(L'A', NULL) == WEOF && errno == EINVAL);

    return 0;
}
