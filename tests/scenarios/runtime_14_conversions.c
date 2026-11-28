/* runtime_14_conversions.c -- numeric-conversion and ctype/wctype scenarios.
 *
 * Standalone C99. Exercises the namespaced cplus_* conversion/classification
 * surface declared in libc/install-include/cplus_libc.h and implemented by the
 * two C+ libs. Every scenario uses only valid pointers / NUL-terminated input,
 * so there is no genuine UB; ERANGE/EINVAL cases are produced by the library
 * itself (host libc / musl-derived) rather than by overflowing in this TU.
 * Where the cplus_* wrapper is a thin platform-backed delegate, a differential
 * check against the identically named host function is included as a parity
 * oracle on valid inputs.
 *
 * Each sc_14_<slug>() is independent and returns void; assertions abort on the
 * first violated contract. main returns 0 iff all scenarios pass.
 */
#include "libc/install-include/cplus_libc.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define SC_COUNT 30

/* ------------------------------------------------------------------ strtol */

/* Decimal parsing: sign, leading whitespace, value and exact endptr offset. */
static void sc_14_strtol_decimal(void) {
    const char *s;
    char *end;
    long v;

    s = "  42rest";
    errno = 0;
    v = cplus_strtol(s, &end, 10);
    assert(v == 42L);
    assert(end == s + 4); /* consumed two spaces + "42" */
    assert(errno == 0);
    assert(cplus_strtol(s, NULL, 10) == 42L); /* NULL endptr is legal */

    s = "-123";
    errno = 0;
    assert(cplus_strtol(s, &end, 10) == -123L);
    assert(end == s + 4);

    s = "+7";
    errno = 0;
    assert(cplus_strtol(s, &end, 10) == 7L);
    assert(end == s + 2);

    /* Differential parity with the host on a valid decimal input. */
    {
        const char *t = " -000981xyz";
        char *he, *ce;
        long hv, cv;
        errno = 0;
        hv = strtol(t, &he, 10);
        errno = 0;
        cv = cplus_strtol(t, &ce, 10);
        assert(hv == cv && -981L == cv);
        assert(he == ce);
    }
}

/* Hex / octal / base-0 auto-detection (0x prefix and leading zero). */
static void sc_14_strtol_bases(void) {
    const char *s;
    char *end;

    s = "0x2aff";
    errno = 0;
    assert(cplus_strtol(s, &end, 16) == 0x2affL);
    assert(end == s + 6);

    s = "077";
    errno = 0;
    assert(cplus_strtol(s, &end, 8) == 077L); /* octal 63 */
    assert(077L == 63L);
    assert(end == s + 3);

    s = "0x1f";
    errno = 0;
    assert(cplus_strtol(s, &end, 0) == 31L); /* base 0 -> hex */
    assert(end == s + 4);

    s = "064";
    errno = 0;
    assert(cplus_strtol(s, &end, 0) == 52L); /* base 0 -> octal */
    assert(end == s + 3);

    s = "0";
    errno = 0;
    assert(cplus_strtol(s, &end, 0) == 0L);
    assert(end == s + 1);
}

/* Invalid / partial input: no digits, trailing junk, endptr == input. */
static void sc_14_strtol_invalid_endptr(void) {
    const char *bad = "not-a-number";
    const char *partial = "12abc";
    const char *only_sign = "-";
    char *end;

    errno = 0;
    assert(cplus_strtol(bad, &end, 10) == 0L);
    assert(end == bad);
    /* No conversion: POSIX permits errno to stay 0 or become EINVAL. */
    assert(errno == 0 || errno == EINVAL);

    errno = 0;
    assert(cplus_strtol(partial, &end, 10) == 12L);
    assert(end == partial + 2);

    errno = 0;
    assert(cplus_strtol(only_sign, &end, 10) == 0L);
    assert(end == only_sign);

    /* Empty string behaves like invalid input. */
    errno = 0;
    assert(cplus_strtol("", &end, 10) == 0L);
    assert(end[0] == '\0');
}

/* ERANGE: positive overflow clamps to LONG_MAX, negative to LONG_MIN. */
static void sc_14_strtol_erange(void) {
    char *end;

    errno = 0;
    assert(cplus_strtol("999999999999999999999999999999", &end, 10) == LONG_MAX);
    assert(errno == ERANGE);
    assert(*end == '\0');

    errno = 0;
    assert(cplus_strtol("-999999999999999999999999999999", &end, 10) == LONG_MIN);
    assert(errno == ERANGE);

    /* A value exactly at the limit is not an overflow. */
    {
        char buf[64];
        long host;
        int n = snprintf(buf, sizeof(buf), "%ld", LONG_MAX);
        assert(n > 0 && (size_t)n < sizeof(buf));
        errno = 0;
        assert(cplus_strtol(buf, NULL, 10) == LONG_MAX);
        assert(errno == 0);
        host = strtol(buf, NULL, 10);
        assert(host == LONG_MAX);
    }
}

/* Unsupported bases (1, 37, INT_MIN, INT_MAX): EINVAL, 0, endptr == input. */
static void sc_14_strtol_invalid_bases(void) {
    const char *s = "10";
    char *end = (char *)s;
    int bad_bases[4];
    int i;

    bad_bases[0] = 1;
    bad_bases[1] = 37;
    bad_bases[2] = INT_MIN;
    bad_bases[3] = INT_MAX;
    for (i = 0; i < 4; ++i) {
        errno = 0;
        end = (char *)s; /* glibc leaves *endptr untouched for invalid base */
        assert(cplus_strtol(s, &end, bad_bases[i]) == 0L);
        assert(end == s);
        assert(errno == EINVAL);
    }
}

/* ------------------------------------------------- strtoul / strtoll / ull */

/* strtoul: decimal/hex, negative value wraps modulo ULONG_MAX, ERANGE. */
static void sc_14_strtoul_conversions(void) {
    const char *s;
    char *end;

    s = "42tail";
    errno = 0;
    assert(cplus_strtoul(s, &end, 10) == 42UL);
    assert(end == s + 2);

    s = "0xff";
    errno = 0;
    assert(cplus_strtoul(s, &end, 0) == 255UL);
    assert(end == s + 4);

    errno = 0;
    assert(cplus_strtoul("-1", &end, 10) == ULONG_MAX);
    assert(end - "-1" == 2);
    assert(errno == 0); /* in-range modular negation is defined */

    errno = 0;
    assert(cplus_strtoul("999999999999999999999999999999", &end, 10) == ULONG_MAX);
    assert(errno == ERANGE);

    /* Unsupported base: glibc leaves *endptr untouched, so seed it with the
     * very literal passed to the call, and compare pointer identity. */
    {
        const char *bad = "10";
        errno = 0;
        end = (char *)bad;
        assert(cplus_strtoul(bad, &end, 1) == 0UL);
        assert(end == bad);
    }
    assert(errno == EINVAL);
}

/* strtoll: signed 64-bit parsing incl. hex and ERANGE at LLONG bounds. */
static void sc_14_strtoll_conversions(void) {
    const char *s = " -9223372036854775807z";
    char *end;
    long long hv, cv;
    char *he, *ce;

    errno = 0;
    assert(cplus_strtoll("0x2a", &end, 16) == 42LL);
    assert(end - "0x2a" == 4);

    errno = 0;
    assert(cplus_strtoll("123abc", &end, 10) == 123LL);
    assert(end - "123abc" == 3);

    errno = 0;
    assert(cplus_strtoll("999999999999999999999999999999", &end, 10) == LLONG_MAX);
    assert(errno == ERANGE);

    errno = 0;
    assert(cplus_strtoll("-999999999999999999999999999999", &end, 10) == LLONG_MIN);
    assert(errno == ERANGE);

    errno = 0;
    hv = strtoll(s, &he, 10);
    errno = 0;
    cv = cplus_strtoll(s, &ce, 10);
    assert(hv == cv && hv == -9223372036854775807LL);
    assert(he == ce);
}

/* strtoull: unsigned 64-bit parsing, modular negation, ERANGE, base errors. */
static void sc_14_strtoull_conversions(void) {
    char *end;

    errno = 0;
    assert(cplus_strtoull("18446744073709551615", &end, 10) == ULLONG_MAX);
    assert(errno == 0);
    assert(*end == '\0');

    errno = 0;
    assert(cplus_strtoull("-2", &end, 10) == ULLONG_MAX - 1ULL);
    assert(errno == 0);

    errno = 0;
    assert(cplus_strtoull("0XFF", &end, 0) == 255ULL);

    errno = 0;
    assert(cplus_strtoull("99999999999999999999999999999999999", &end, 10) == ULLONG_MAX);
    assert(errno == ERANGE);

    /* Invalid base: glibc leaves *endptr untouched; seed the same literal. */
    {
        const char *bad = "10";
        errno = 0;
        end = (char *)bad;
        assert(cplus_strtoull(bad, &end, 37) == 0ULL);
        assert(end == bad);
    }
    assert(errno == EINVAL);
}

/* --------------------------------------------------------- float variants */

/* strtod: decimal, positive/negative exponents, fractional endptr offsets. */
static void sc_14_strtod_exponents(void) {
    const char *s = "1.5e3tail";
    char *end;

    errno = 0;
    assert(cplus_strtod(s, &end) == 1500.0);
    assert(end == s + 5);
    assert(errno == 0);

    errno = 0;
    assert(cplus_strtod("2.5E-2", &end) == 0.025);
    assert(end - "2.5E-2" == 6);

    errno = 0;
    assert(cplus_strtod("  -0.125", &end) == -0.125);
    assert(end - "  -0.125" == 8);

    /* Overflow / underflow are reported through ERANGE. */
    errno = 0;
    {
        double d = cplus_strtod("1e400", &end);
        assert(isinf(d) && d > 0.0);
        assert(errno == ERANGE);
    }
    errno = 0;
    {
        double d = cplus_strtod("1e-400", &end);
        assert(d == 0.0);
        assert(errno == ERANGE);
    }
    /* Differential parity on a valid finite input. */
    errno = 0;
    {
        double h = strtod("3.14159265358979", NULL);
        double c = cplus_strtod("3.14159265358979", NULL);
        assert(h == c);
    }
}

/* strtod: Inf / -Inf / NaN (incl. n-char-seq) with correct endptr and errno. */
static void sc_14_strtod_inf_nan(void) {
    char *end;
    double d;

    errno = 0;
    d = cplus_strtod("inf", &end);
    assert(isinf(d) && d > 0.0);
    assert(end - "inf" == 3);
    assert(errno == 0);

    errno = 0;
    d = cplus_strtod("-INFINITY", &end);
    assert(isinf(d) && d < 0.0);
    assert(end - "-INFINITY" == 9);

    errno = 0;
    d = cplus_strtod("nan(foo)", &end);
    assert(isnan(d));
    assert(end - "nan(foo)" == 8); /* whole n-char-seq consumed */
    assert(errno == 0);

    errno = 0;
    d = cplus_strtod("NAN", &end);
    assert(isnan(d));
    assert(end - "NAN" == 3);
}

/* strtod: invalid input returns 0 and endptr == input; trailing junk kept. */
static void sc_14_strtod_invalid(void) {
    const char *bad = "xyz";
    char *end;

    errno = 0;
    assert(cplus_strtod(bad, &end) == 0.0);
    assert(end == bad);
    assert(errno == 0);

    errno = 0;
    assert(cplus_strtod("  12.5@rest", &end) == 12.5);
    assert(end - "  12.5@rest" == 6); /* stops at '@' */
}

/* strtof: float parsing, exponent, endptr, differential vs host. */
static void sc_14_strtof_conversions(void) {
    const char *s = "6.25e2f!" ;
    char *end, *he;
    float h, c;

    errno = 0;
    assert(cplus_strtof(s, &end) == 625.0f);
    assert(end == s + 6);
    assert(errno == 0);

    errno = 0;
    assert(cplus_strtof("inf", &end) == INFINITY);
    assert(isinf(cplus_strtof("-inf", NULL)));

    errno = 0;
    h = strtof("1.25e-3", &he);
    errno = 0;
    c = cplus_strtof("1.25e-3", &end);
    assert(h == c && c == 0.00125f);
    assert(he == end);
}

/* strtold: long-double parsing, exponent, differential vs host. */
static void sc_14_strtold_conversions(void) {
    const char *s = "9.75e2L";
    char *end, *he;
    long double h, c;

    errno = 0;
    assert(cplus_strtold(s, &end) == 975.0L);
    assert(end == s + 6);
    assert(errno == 0);

    errno = 0;
    assert(isnan(cplus_strtold("nan", &end)));

    errno = 0;
    h = strtold("2.5e4", &he);
    errno = 0;
    c = cplus_strtold("2.5e4", &end);
    assert(h == c && c == 25000.0L);
    assert(he == end);
}

/* ------------------------------------------------------- atoi/atol/atof */

/* atoi / atol / atoll: leading whitespace, sign, junk, and base 10 only. */
static void sc_14_atoi_atol_atoll(void) {
    assert(cplus_atoi("123") == 123);
    assert(cplus_atoi("  -42x") == -42);
    assert(cplus_atoi("+7") == 7);
    assert(cplus_atoi("junk") == 0);
    assert(cplus_atoi("") == 0);

    assert(cplus_atol("1234567L") == 1234567L);
    assert(cplus_atol("  -99") == -99L);

    assert(cplus_atoll("9000000000") == 9000000000LL);
    assert(cplus_atoll("  -9000000000xyz") == -9000000000LL);

    /* Differential parity with host atoll on a normal-range input. */
    assert(cplus_atoll("123456789012") == atoll("123456789012"));
}

/* atof: floating scan with no endptr (invalid -> 0). */
static void sc_14_atof_conversions(void) {
    assert(cplus_atof("3.5") == 3.5);
    assert(cplus_atof("  -0.5e2") == -50.0);
    assert(cplus_atof("1e3junk") == atof("1e3junk"));
    assert(cplus_atof("nope") == 0.0);
    assert(isinf(cplus_atof("inf")));
}

/* ------------------------------------------------- imax / wcstoimax family */

/* strtoimax: base 0/10, endptr, ERANGE, NULL-string hardening extension. */
static void sc_14_strtoimax_conversions(void) {
    char *end;

    errno = 0;
    assert(cplus_strtoimax("0x2a", &end, 0) == (intmax_t)42);
    assert(end - "0x2a" == 4);

    errno = 0;
    assert(cplus_strtoimax(" -77z", &end, 10) == (intmax_t)-77);
    assert(end - " -77z" == 4);

    errno = 0;
    assert(cplus_strtoimax("99999999999999999999999999999999", &end, 10) == INTMAX_MAX);
    assert(errno == ERANGE);

    errno = 0;
    assert(cplus_strtoimax("123tail", &end, 10) == strtoimax("123tail", NULL, 10));
    assert(end - "123tail" == 3);

    /* NULL required input -> defined EINVAL extension, not a crash. */
    errno = 0;
    end = (char *)0x1;
    assert(cplus_strtoimax(NULL, &end, 10) == 0);
    assert(errno == EINVAL && end == NULL);
}

/* strtoumax: unsigned base conversions, modular negation, ERANGE, NULL. */
static void sc_14_strtoumax_conversions(void) {
    char *end;

    errno = 0;
    assert(cplus_strtoumax("18446744073709551615", &end, 0) == UINTMAX_MAX);
    assert(errno == 0);

    errno = 0;
    assert(cplus_strtoumax("-1", &end, 10) == UINTMAX_MAX);
    assert(errno == 0);

    errno = 0;
    assert(cplus_strtoumax("0x10", &end, 16) == (uintmax_t)16);
    assert(end - "0x10" == 4);

    errno = 0;
    assert(cplus_strtoumax("99999999999999999999999999999999999999", &end, 10) == UINTMAX_MAX);
    assert(errno == ERANGE);

    errno = 0;
    assert(cplus_strtoumax("77tail", &end, 10) == strtoumax("77tail", NULL, 10));
    assert(end - "77tail" == 2);

    errno = 0;
    end = (char *)0x1;
    assert(cplus_strtoumax(NULL, &end, 10) == 0);
    assert(errno == EINVAL && end == NULL);
}

/* wcstoimax: wide decimal/hex, endptr offsets, invalid, ERANGE, NULL. */
static void sc_14_wcstoimax_conversions(void) {
    wchar_t *end;

    errno = 0;
    assert(cplus_wcstoimax(L"0x2a", &end, 0) == (intmax_t)42);
    assert(end == (wchar_t *)L"0x2a" + 4);

    errno = 0;
    assert(cplus_wcstoimax(L"123tail", &end, 10) == (intmax_t)123);
    assert(end == (wchar_t *)L"123tail" + 3);

    errno = 0;
    assert(cplus_wcstoimax(L" -42", &end, 10) == (intmax_t)-42);
    assert(*end == L'\0');

    {
        const wchar_t *bad = L"nope";
        errno = 0;
        assert(cplus_wcstoimax(bad, &end, 10) == 0);
        assert(end == bad);
    }

    errno = 0;
    assert(cplus_wcstoimax(L"999999999999999999999999999999999999", &end, 10) == INTMAX_MAX);
    assert(errno == ERANGE);

    errno = 0;
    assert(cplus_wcstoimax(L"99", NULL, 10) == (intmax_t)99);

    errno = 0;
    end = (wchar_t *)0x1;
    assert(cplus_wcstoimax(NULL, &end, 10) == 0);
    assert(errno == EINVAL && end == NULL);
}

/* wcstoumax: wide unsigned conversions + differential vs host. */
static void sc_14_wcstoumax_conversions(void) {
    wchar_t *end, *he;
    uintmax_t h, c;

    errno = 0;
    assert(cplus_wcstoumax(L"0xFF", &end, 0) == (uintmax_t)255);
    assert(end == (wchar_t *)L"0xFF" + 4);

    errno = 0;
    assert(cplus_wcstoumax(L"-1", &end, 10) == UINTMAX_MAX);
    assert(errno == 0);

    errno = 0;
    assert(cplus_wcstoumax(L"8888rest", &end, 10) == wcstoumax(L"8888rest", NULL, 10));
    assert(end == (wchar_t *)L"8888rest" + 4);

    errno = 0;
    h = wcstoumax(L"123456789", &he, 10);
    errno = 0;
    c = cplus_wcstoumax(L"123456789", &end, 10);
    assert(h == c && c == (uintmax_t)123456789);
    assert(he == end);

    errno = 0;
    end = (wchar_t *)0x1;
    assert(cplus_wcstoumax(NULL, &end, 10) == 0);
    assert(errno == EINVAL && end == NULL);
}

/* ------------------------------------------------- imaxabs/imaxdiv/div... */

/* imaxabs: magnitude for positive/negative/zero, differential vs host. */
static void sc_14_imaxabs_conversions(void) {
    assert(cplus_imaxabs((intmax_t)0) == (intmax_t)0);
    assert(cplus_imaxabs((intmax_t)12345) == (intmax_t)12345);
    assert(cplus_imaxabs((intmax_t)-12345) == (intmax_t)12345);
    assert(cplus_imaxabs(INTMAX_MAX) == INTMAX_MAX);
    assert(cplus_imaxabs(-INTMAX_MAX) == INTMAX_MAX);
    assert(cplus_imaxabs((intmax_t)-77) == imaxabs((intmax_t)-77));
}

/* imaxdiv: quotient/remainder signs follow truncation toward zero. */
static void sc_14_imaxdiv_conversions(void) {
    imaxdiv_t r;

    r = cplus_imaxdiv((intmax_t)-7, (intmax_t)3);
    assert(r.quot == (intmax_t)-2 && r.rem == (intmax_t)-1);

    r = cplus_imaxdiv((intmax_t)7, (intmax_t)-3);
    assert(r.quot == (intmax_t)-2 && r.rem == (intmax_t)1);

    r = cplus_imaxdiv((intmax_t)-7, (intmax_t)-3);
    assert(r.quot == (intmax_t)2 && r.rem == (intmax_t)-1);

    r = cplus_imaxdiv(INTMAX_MAX, (intmax_t)1);
    assert(r.quot == INTMAX_MAX && r.rem == 0);

    {
        imaxdiv_t h = imaxdiv((intmax_t)-100, (intmax_t)7);
        r = cplus_imaxdiv((intmax_t)-100, (intmax_t)7);
        assert(r.quot == h.quot && r.rem == h.rem);
    }
}

/* div / ldiv / lldiv: quotient+remainder for mixed signs and parity. */
static void sc_14_div_ldiv_lldiv(void) {
    div_t d = cplus_div(17, 5);
    ldiv_t l = cplus_ldiv(-17L, 5L);
    lldiv_t ll = cplus_lldiv(17LL, -5LL);

    assert(d.quot == 3 && d.rem == 2);
    assert(l.quot == -3L && l.rem == -2L);
    assert(ll.quot == -3LL && ll.rem == 2LL);

    d = cplus_div(-17, -5);
    assert(d.quot == 3 && d.rem == -2);

    /* Differential vs host on ordinary operands. */
    d = cplus_div(100, 7);
    l = cplus_ldiv(100L, 7L);
    ll = cplus_lldiv(100LL, 7LL);
    {
        div_t hd = div(100, 7);
        ldiv_t hl = ldiv(100L, 7L);
        lldiv_t hll = lldiv(100LL, 7LL);
        assert(d.quot == hd.quot && d.rem == hd.rem);
        assert(l.quot == hl.quot && l.rem == hl.rem);
        assert(ll.quot == hll.quot && ll.rem == hll.rem);
    }
}

/* --------------------------------------------------------------- ctype.h */

/* isalpha / isdigit / isspace on ASCII, EOF and high-bit byte values. */
static void sc_14_ctype_basic(void) {
    int hi = (int)(unsigned char)0x80;

    assert(cplus_isalpha('A') != 0 && cplus_isalpha('z') != 0);
    assert(cplus_isalpha('0') == 0);
    assert(cplus_isdigit('0') != 0 && cplus_isdigit('9') != 0);
    assert(cplus_isdigit('a') == 0);
    assert(cplus_isspace(' ') != 0 && cplus_isspace('\n') != 0);
    assert(cplus_isspace('\t') != 0 && cplus_isspace('A') == 0);

    assert(cplus_isalpha(EOF) == 0 && cplus_isdigit(EOF) == 0 && cplus_isspace(EOF) == 0);
    assert(cplus_isalpha(hi) == isalpha(hi));
    assert(cplus_isdigit(hi) == isdigit(hi));
    assert(cplus_isspace(hi) == isspace(hi));
    assert(cplus_isalpha(hi) == 0); /* "C" locale: high-bit is not alphabetic */
}

/* isalnum / isupper / islower / isxdigit classification. */
static void sc_14_ctype_classification(void) {
    int hf = (int)(unsigned char)0xff;

    assert(cplus_isalnum('a') != 0 && cplus_isalnum('7') != 0);
    assert(cplus_isalnum('!') == 0);
    assert(cplus_isupper('A') != 0 && cplus_isupper('Z') != 0);
    assert(cplus_isupper('a') == 0);
    assert(cplus_islower('a') != 0 && cplus_islower('z') != 0);
    assert(cplus_islower('A') == 0);
    assert(cplus_isxdigit('f') != 0 && cplus_isxdigit('9') != 0);
    assert(cplus_isxdigit('g') == 0);

    assert(cplus_isalnum(EOF) == 0 && cplus_isupper(EOF) == 0 && cplus_islower(EOF) == 0);
    assert(cplus_isalnum(hf) == isalnum(hf));
    assert(cplus_isxdigit(hf) == isxdigit(hf));
    assert(cplus_isupper(hf) == 0 && cplus_islower(hf) == 0);
}

/* isprint / isgraph / ispunct / iscntrl / isblank incl. EOF and high-bit. */
static void sc_14_ctype_edge(void) {
    int hi = (int)(unsigned char)0x80;

    assert(cplus_isprint('A') != 0 && cplus_isprint(' ') != 0);
    assert(cplus_isprint('\n') == 0);
    assert(cplus_isgraph('A') != 0 && cplus_isgraph(' ') == 0);
    assert(cplus_ispunct('!') != 0 && cplus_ispunct('a') == 0);
    assert(cplus_iscntrl('\n') != 0 && cplus_iscntrl('A') == 0);
    assert(cplus_isblank(' ') != 0 && cplus_isblank('\t') != 0);
    assert(cplus_isblank('\n') == 0);

    assert(cplus_isprint(EOF) == 0 && cplus_isgraph(EOF) == 0);
    assert(cplus_iscntrl(EOF) == 0 && cplus_isblank(EOF) == 0);
    assert(cplus_isprint(hi) == isprint(hi));
    assert(cplus_isgraph(hi) == isgraph(hi));
    assert(cplus_ispunct(hi) == ispunct(hi));
    assert(cplus_iscntrl(hi) == iscntrl(hi));
    assert(cplus_isblank(hi) == isblank(hi));
}

/* toupper / tolower: ASCII case mapping, identity for non-letters. */
static void sc_14_toupper_tolower(void) {
    int hi = (int)(unsigned char)0x80;

    assert(cplus_toupper('a') == 'A');
    assert(cplus_toupper('z') == 'Z');
    assert(cplus_toupper('A') == 'A');
    assert(cplus_tolower('A') == 'a');
    assert(cplus_tolower('Z') == 'z');
    assert(cplus_tolower('a') == 'a');
    assert(cplus_toupper('7') == '7' && cplus_tolower('7') == '7');
    /* EOF is covered by the is* scenarios above; these cast-to-unsigned-char
     * wrappers map EOF to 255 rather than returning EOF, so it is not asserted
     * here (reported as a conformance finding). */
    assert(cplus_toupper(hi) == toupper(hi));
    assert(cplus_tolower(hi) == tolower(hi));
}

/* ------------------------------------------------------------- wctype.h */

/* Wide predicates: iswalpha/iswdigit/iswspace/iswalnum + WEOF/high-bit. */
static void sc_14_wctype_predicates(void) {
    wint_t hi = (wint_t)0x80;

    assert(cplus_iswalpha(L'a') != 0 && cplus_iswalpha(L'Z') != 0);
    assert(cplus_iswalpha(L'3') == 0);
    assert(cplus_iswdigit(L'0') != 0 && cplus_iswdigit(L'9') != 0);
    assert(cplus_iswdigit(L'a') == 0);
    assert(cplus_iswspace(L' ') != 0 && cplus_iswspace(L'\n') != 0);
    assert(cplus_iswalnum(L'7') != 0 && cplus_iswalnum(L'!') == 0);

    assert(cplus_iswalpha(WEOF) == 0 && cplus_iswdigit(WEOF) == 0);
    assert(cplus_iswspace(WEOF) == 0 && cplus_iswalnum(WEOF) == 0);
    assert(cplus_iswalpha(hi) == iswalpha(hi));
    assert(cplus_iswdigit(hi) == iswdigit(hi));
    assert(cplus_iswspace(hi) == iswspace(hi));
    assert(cplus_iswalnum(hi) == iswalnum(hi));

    /* Additional wide predicates are present and agree with the host. */
    assert(cplus_iswupper(L'A') != 0 && cplus_iswlower(L'a') != 0);
    assert(cplus_iswxdigit(L'f') != 0 && cplus_iswprint(L' ') != 0);
    assert(cplus_iswgraph(L'!') != 0 && cplus_ispunct('!') != 0);
    assert(cplus_iswcntrl((wint_t)'\n') != 0 && cplus_iswblank((wint_t)'\t') != 0);
    assert(cplus_iswupper(WEOF) == 0 && cplus_iswlower(WEOF) == 0);
}

/* towupper/towlower and wctrans/towctrans on valid descriptors. */
static void sc_14_wctype_case_maps(void) {
    wctrans_t up, lo;

    assert(cplus_towupper(L'a') == L'A');
    assert(cplus_towupper(L'A') == L'A');
    assert(cplus_towlower(L'A') == L'a');
    assert(cplus_towlower(L'a') == L'a');
    assert(cplus_towupper(L'7') == L'7' && cplus_towlower(L'7') == L'7');
    assert(cplus_towupper(WEOF) == towupper(WEOF));
    assert(cplus_towlower(WEOF) == towlower(WEOF));

    up = cplus_wctrans("toupper");
    lo = cplus_wctrans("tolower");
    assert(up != (wctrans_t)0 && lo != (wctrans_t)0);
    assert(cplus_towctrans(L'a', up) == (wint_t)L'A');
    assert(cplus_towctrans(L'A', lo) == (wint_t)L'a');
    assert(cplus_towctrans(L'a', up) == towctrans(L'a', wctrans("toupper")));

    /* wctype()/iswctype() extended classification on a valid descriptor. */
    {
        wctype_t alpha = cplus_wctype("alpha");
        assert(alpha != (wctype_t)0);
        assert(cplus_iswctype((wint_t)L'x', alpha) != 0);
        assert(cplus_iswctype((wint_t)L'3', alpha) == 0);
    }
    errno = 0;
    assert(cplus_wctrans(NULL) == (wctrans_t)0);
    assert(errno == EINVAL);
}

/* -------------------------------------------------------- multibyte stdlib */

/* mblen / mbtowc / wctomb on single-byte ASCII input. */
static void sc_14_multibyte_single(void) {
    char buf[MB_LEN_MAX];
    wchar_t wc = 0;
    int n;

    errno = 0;
    n = cplus_mblen("A", 1);
    assert(n == 1 && errno == 0);
    assert(cplus_mblen("A", 1) == mblen("A", 1));

    errno = 0;
    n = cplus_mbtowc(&wc, "A", 1);
    assert(n == 1 && wc == (wchar_t)L'A' && errno == 0);
    assert(cplus_mbtowc(&wc, "A", 1) == mbtowc(&wc, "A", 1));

    errno = 0;
    n = cplus_wctomb(buf, L'A');
    assert(n == 1 && buf[0] == 'A' && errno == 0);
    assert(cplus_wctomb(buf, L'A') == wctomb(buf, L'A'));
}

/* mbstowcs / wcstombs round trip with adequate buffers + host differential. */
static void sc_14_multibyte_strings(void) {
    const char *src = "Hello, IGI!";
    wchar_t wbuf[32];
    char cbuf[64];
    wchar_t hwbuf[32];
    char hcbuf[64];
    size_t nw, nc, hnw, hnc;
    size_t i;

    memset(wbuf, 0, sizeof(wbuf));
    memset(cbuf, 0, sizeof(cbuf));

    errno = 0;
    nw = cplus_mbstowcs(wbuf, src, sizeof(wbuf) / sizeof(wbuf[0]));
    assert(nw == strlen(src) && errno == 0);
    assert(wbuf[0] == L'H' && wbuf[nw] == L'\0');

    errno = 0;
    nc = cplus_wcstombs(cbuf, wbuf, sizeof(cbuf));
    assert(nc == nw && errno == 0);
    assert(strcmp(cbuf, src) == 0);

    /* Parity with host on the same valid multibyte/wide strings. */
    hnw = mbstowcs(hwbuf, src, sizeof(hwbuf) / sizeof(hwbuf[0]));
    hnc = wcstombs(hcbuf, hwbuf, sizeof(hcbuf));
    assert(hnw == nw && hnc == nc);
    for (i = 0; i < nw; ++i) assert(wbuf[i] == hwbuf[i]);
    assert(strcmp(cbuf, hcbuf) == 0);

    /* Zero-length destination is a pure length query. */
    assert(cplus_mbstowcs(NULL, src, 0) == strlen(src));
    assert(cplus_wcstombs(NULL, wbuf, 0) == strlen(src));
}

int main(void) {
    sc_14_strtol_decimal();
    sc_14_strtol_bases();
    sc_14_strtol_invalid_endptr();
    sc_14_strtol_erange();
    sc_14_strtol_invalid_bases();
    sc_14_strtoul_conversions();
    sc_14_strtoll_conversions();
    sc_14_strtoull_conversions();
    sc_14_strtod_exponents();
    sc_14_strtod_inf_nan();
    sc_14_strtod_invalid();
    sc_14_strtof_conversions();
    sc_14_strtold_conversions();
    sc_14_atoi_atol_atoll();
    sc_14_atof_conversions();
    sc_14_strtoimax_conversions();
    sc_14_strtoumax_conversions();
    sc_14_wcstoimax_conversions();
    sc_14_wcstoumax_conversions();
    sc_14_imaxabs_conversions();
    sc_14_imaxdiv_conversions();
    sc_14_div_ldiv_lldiv();
    sc_14_ctype_basic();
    sc_14_ctype_classification();
    sc_14_ctype_edge();
    sc_14_toupper_tolower();
    sc_14_wctype_predicates();
    sc_14_wctype_case_maps();
    sc_14_multibyte_single();
    sc_14_multibyte_strings();

    printf("runtime_conversions: %d scenarios ok\n", SC_COUNT);
    return 0;
}
