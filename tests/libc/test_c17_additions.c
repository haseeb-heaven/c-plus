#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include "install-include/cplus_libc.h"

int main(void) {
    /* 1. <wctype.h> positive and NULL/negative tests */
    assert(cplus_iswalpha(L'A') && !cplus_iswalpha(L'1'));
    assert(cplus_iswdigit(L'9') && !cplus_iswdigit(L'x'));
    assert(cplus_iswspace(L' ') && cplus_iswblank(L'\t'));
    assert(cplus_iswalnum(L'z') && cplus_iswupper(L'Z') && cplus_iswlower(L'z'));
    assert(cplus_iswxdigit(L'F') && !cplus_iswxdigit(L'G'));
    assert(cplus_iswprint(L'!') && cplus_iswgraph(L'!') && cplus_iswpunct(L'!'));
    assert(cplus_iswcntrl(L'\n') && !cplus_iswcntrl(L'A'));
    assert(cplus_towupper(L'a') == L'A');
    assert(cplus_towlower(L'B') == L'b');
    errno = 0;
    assert(cplus_wctype(NULL) == (wctype_t)0 && errno == EINVAL);
    wctype_t wt = cplus_wctype("alpha");
    assert(wt != (wctype_t)0);
    assert(cplus_iswctype(L'a', wt) && !cplus_iswctype(L'5', wt));

    /* 2. Reentrant <time.h> positive and NULL guards */
    time_t epoch = 0;
    struct tm tm_res;
    char tbuf[64];
    assert(cplus_gmtime_r(&epoch, &tm_res) == &tm_res);
    assert(tm_res.tm_year == 70 && tm_res.tm_mon == 0 && tm_res.tm_mday == 1);
    assert(cplus_localtime_r(&epoch, &tm_res) == &tm_res);
    assert(cplus_asctime_r(&tm_res, tbuf) == tbuf && tbuf[0] != '\0');
    assert(cplus_ctime_r(&epoch, tbuf) == tbuf && tbuf[0] != '\0');
    errno = 0; assert(cplus_gmtime_r(NULL, &tm_res) == NULL && errno == EINVAL);
    errno = 0; assert(cplus_localtime_r(&epoch, NULL) == NULL && errno == EINVAL);
    errno = 0; assert(cplus_asctime_r(NULL, tbuf) == NULL && errno == EINVAL);
    errno = 0; assert(cplus_ctime_r(NULL, tbuf) == NULL && errno == EINVAL);

    /* 3. <stdio.h> rewind / fgetc / fputc */
    FILE *fp = cplus_tmpfile();
    assert(fp != NULL);
    assert(cplus_fputc('Z', fp) == 'Z');
    cplus_rewind(fp);
    assert(cplus_fgetc(fp) == 'Z');
    assert(cplus_fclose(fp) == 0);
    errno = 0; assert(cplus_fgetc(NULL) == EOF && errno == EINVAL);
    errno = 0; assert(cplus_fputc('A', NULL) == EOF && errno == EINVAL);
    errno = 0; cplus_rewind(NULL); assert(errno == EINVAL);

    /* 4. <inttypes.h> strtoimax / strtoumax & <stdlib.h> multibyte conversions */
    char *end = NULL;
    assert(cplus_strtoimax("-42", &end, 10) == -42 && *end == '\0');
    assert(cplus_strtoumax("42", &end, 10) == 42u && *end == '\0');
    errno = 0; assert(cplus_strtoimax(NULL, &end, 10) == 0 && errno == EINVAL && end == NULL);
    errno = 0; assert(cplus_strtoumax(NULL, &end, 10) == 0 && errno == EINVAL && end == NULL);

    wchar_t wc = 0;
    char mb[16];
    wchar_t wbuf[8];
    assert(cplus_mblen("A", 1) == 1);
    assert(cplus_mbtowc(&wc, "Q", 1) == 1 && wc == L'Q');
    assert(cplus_wctomb(mb, L'R') == 1 && mb[0] == 'R');
    assert(cplus_mbstowcs(wbuf, "hi", 8) == 2 && wbuf[0] == L'h' && wbuf[1] == L'i');
    assert(cplus_wcstombs(mb, L"hi", sizeof(mb)) == 2 && strcmp(mb, "hi") == 0);
    errno = 0; assert(cplus_mbstowcs(NULL, "hi", 2) == (size_t)-1 && errno == EINVAL);
    errno = 0; assert(cplus_wcstombs(NULL, L"hi", 2) == (size_t)-1 && errno == EINVAL);

    puts("test_c17_additions: ok");
    return 0;
}
