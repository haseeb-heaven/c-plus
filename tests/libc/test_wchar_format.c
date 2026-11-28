/* Standalone C99 test for the cplus_* wide formatted-I/O wrappers.
 * Local extern declarations keep this test independent of cplus_libc.h.
 * Tier-1 cases use only valid format strings (no %n, no untrusted input).
 * Tier-2 cases deliberately pass %n formats to cplus_swprintf_checked with a
 * valid sentinel int* to prove they are refused without a write (F01). */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int cplus_wprintf(const wchar_t *, ...);
int cplus_vwprintf(const wchar_t *, va_list);
int cplus_fwprintf(FILE *, const wchar_t *, ...);
int cplus_vfwprintf(FILE *, const wchar_t *, va_list);
int cplus_swprintf(wchar_t *, size_t, const wchar_t *, ...);
int cplus_swprintf_checked(wchar_t *, size_t, const wchar_t *, ...);
int cplus_vswprintf(wchar_t *, size_t, const wchar_t *, va_list);
void cplus_libc_log_set_enabled(int);
int cplus_wscanf(const wchar_t *, ...);
int cplus_vwscanf(const wchar_t *, va_list);
int cplus_fwscanf(FILE *, const wchar_t *, ...);
int cplus_vfwscanf(FILE *, const wchar_t *, va_list);
int cplus_swscanf(const wchar_t *, const wchar_t *, ...);
int cplus_vswscanf(const wchar_t *, const wchar_t *, va_list);

/* va_list helpers: exercise every v-form's positive path and NULL guards. */
static int call_vwprintf(const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = cplus_vwprintf(fmt, ap);
    va_end(ap); return r;
}
static int call_vfwprintf(FILE *s, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = cplus_vfwprintf(s, fmt, ap);
    va_end(ap); return r;
}
static int call_vswprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = cplus_vswprintf(s, n, fmt, ap);
    va_end(ap); return r;
}
static int call_vwscanf(const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = cplus_vwscanf(fmt, ap);
    va_end(ap); return r;
}
static int call_vfwscanf(FILE *s, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = cplus_vfwscanf(s, fmt, ap);
    va_end(ap); return r;
}
static int call_vswscanf(const wchar_t *str, const wchar_t *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = cplus_vswscanf(str, fmt, ap);
    va_end(ap); return r;
}

int main(void) {
    /* 1. swprintf: integers, strings and floats; exact string + count. */
    wchar_t buf[64];
    int r = cplus_swprintf(buf, 64, L"%d %ls %.2f", 42, L"wide", 3.14159);
    assert(r == 12);
    assert(wcscmp(buf, L"42 wide 3.14") == 0);

    /* 2. Truncation: ISO C requires a negative return on output error;
     *    the destination is still NUL-terminated within n characters. */
    wchar_t small[6];
    int t = cplus_swprintf(small, 6, L"%d %ls %.2f", 42, L"wide", 3.14159);
    assert(t < 0);
    assert(wcslen(small) < 6);
    assert(wcsncmp(small, L"42 wi", 5) == 0);

    /* 3. vswprintf va_list path. */
    wchar_t vbuf[32];
    int vr = call_vswprintf(vbuf, 32, L"n=%d s=%ls", 7, L"xy");
    assert(vr == 8);
    assert(wcscmp(vbuf, L"n=7 s=xy") == 0);

    /* size 0 / NULL destination is a host-defined query, not a guard case:
     * it is passed straight through, so the host's own return/errno apply. */
    assert(cplus_swprintf(NULL, 0, L"abc") < 0);

    /* 4. fwprintf / fwscanf round-trip via tmpfile() with rewind. */
    FILE *fp = tmpfile();
    assert(fp != NULL);
    int wr = cplus_fwprintf(fp, L"%d %ls %.2f", 17, L"roundtrip", 2.5);
    assert(wr == 17);
    rewind(fp);
    int ri = 0;
    wchar_t rw[32];
    double rd = 0.0;
    int rr = cplus_fwscanf(fp, L"%d %ls %lf", &ri, rw, &rd);
    assert(rr == 3);
    assert(ri == 17);
    assert(wcscmp(rw, L"roundtrip") == 0);
    assert(rd == 2.5);

    /* v-forms of the file round-trip (fresh file: a shorter rewrite into the
     * same file would leave stale non-whitespace bytes for %ls to consume). */
    fclose(fp);
    fp = tmpfile();
    assert(fp != NULL);
    int vwr = call_vfwprintf(fp, L"%d %ls", 5, L"vstream");
    assert(vwr == 9); /* "5 vstream" */
    rewind(fp);
    int vri = 0;
    wchar_t vrw[32];
    int vrr = call_vfwscanf(fp, L"%d %ls", &vri, vrw);
    assert(vrr == 2);
    assert(vri == 5);
    assert(wcscmp(vrw, L"vstream") == 0);
    fclose(fp);

    /* 5. swscanf into wide and narrow destinations. */
    int si = 0;
    double sd = 0.0;
    wchar_t sw[32];
    char sn[32];
    int sr = cplus_swscanf(L"42 hello world 3.5", L"%d %ls %s %lf",
                           &si, sw, sn, &sd);
    assert(sr == 4);
    assert(si == 42);
    assert(wcscmp(sw, L"hello") == 0);
    assert(strcmp(sn, "world") == 0);
    assert(sd == 3.5);

    /* vswscanf va_list path. */
    int vi = 0;
    assert(call_vswscanf(L"99", L"%d", &vi) == 1);
    assert(vi == 99);

    /* 6. NULL argument error paths (defined hardening extension). */
    errno = 0; assert(cplus_wprintf(NULL) == -1);               assert(errno == EINVAL);
    errno = 0; assert(call_vwprintf(NULL) == -1);               assert(errno == EINVAL);
    errno = 0; assert(cplus_fwprintf(NULL, L"%d", 1) == -1);    assert(errno == EINVAL);
    errno = 0; assert(call_vfwprintf(NULL, L"%d", 1) == -1);    assert(errno == EINVAL);
    errno = 0; assert(cplus_swprintf(NULL, 8, L"%d", 1) == -1); assert(errno == EINVAL);
    errno = 0; assert(call_vswprintf(NULL, 8, L"%d", 1) == -1); assert(errno == EINVAL);
    errno = 0; assert(cplus_wscanf(NULL) == EOF);               assert(errno == EINVAL);
    errno = 0; assert(call_vwscanf(NULL) == EOF);               assert(errno == EINVAL);
    errno = 0; assert(cplus_fwscanf(NULL, L"%d", &si) == EOF);  assert(errno == EINVAL);
    errno = 0; assert(call_vfwscanf(NULL, L"%d", &si) == EOF);  assert(errno == EINVAL);
    errno = 0; assert(cplus_swscanf(NULL, L"%d", &si) == EOF);  assert(errno == EINVAL);
    errno = 0; assert(cplus_swscanf(L"1", NULL) == EOF);        assert(errno == EINVAL);
    errno = 0; assert(call_vswscanf(NULL, L"%d", &si) == EOF);  assert(errno == EINVAL);
    errno = 0; assert(call_vswscanf(L"1", NULL) == EOF);        assert(errno == EINVAL);

    /* 7. Backward compatibility: the standard (Tier-1) swprintf still passes
     *    %n through to the host unchanged (the caller-trusted contract). */
    {
        wchar_t out[32];
        int written = -1;
        assert(cplus_swprintf(out, 32, L"ab%n", &written) == 2);
        assert(written == 2);
        assert(wcscmp(out, L"ab") == 0);
    }

    /* 8. Tier-2 checked sibling: every %n form is refused with -1/EINVAL
     *    before the host is called, so neither the sentinel int nor the
     *    destination buffer is touched. Checked with logging OFF and ON to
     *    prove parity (the logger must not change return value or errno). */
    {
        const wchar_t *refusals[] = {
            L"%n", L"%hn", L"%hhn", L"%ln", L"%lln", L"%jn", L"%zn",
            L"%tn", L"%1$n", L"%2$hn", L"%*n", L"%*1$n", L"%05n",
            L"prefix %d %n", L"%%%n"
        };
        size_t i;
        for (i = 0; i < sizeof(refusals) / sizeof(refusals[0]); ++i) {
            int logging;
            for (logging = 0; logging <= 1; ++logging) {
                wchar_t out[32];
                int sentinel = 0x7E;
                size_t k;
                int rc;
                for (k = 0; k < 32; ++k) out[k] = L'X';
                cplus_libc_log_set_enabled(logging);
                errno = 0;
                rc = cplus_swprintf_checked(out, 32, refusals[i], &sentinel, 7, 9);
                cplus_libc_log_set_enabled(0);
                assert(rc == -1);
                assert(errno == EINVAL);
                assert(sentinel == 0x7E);   /* no %n write-back */
                for (k = 0; k < 32; ++k) assert(out[k] == L'X'); /* no write at all */
            }
        }
    }

    /* 9. The checked tier still formats normally, and "%%n" is a literal
     *    percent followed by 'n' -- not a %n conversion -- so it is allowed. */
    {
        wchar_t out[32];
        errno = 0;
        assert(cplus_swprintf_checked(out, 32, L"%d %ls", 42, L"ok") == 5);
        assert(wcscmp(out, L"42 ok") == 0);
        assert(errno == 0);
        assert(cplus_swprintf_checked(out, 32, L"%%n") == 2);
        assert(wcscmp(out, L"%n") == 0);
    }

    /* 10. Checked tier mirrors the standard tier's NULL guards. */
    errno = 0; assert(cplus_swprintf_checked(NULL, 8, L"%d", 1) == -1); assert(errno == EINVAL);
    errno = 0; assert(cplus_swprintf_checked(NULL, 8, NULL) == -1);       assert(errno == EINVAL);

    cplus_libc_log_set_enabled(0); /* leave opt-in logging off for the harness */
    return 0;
}
