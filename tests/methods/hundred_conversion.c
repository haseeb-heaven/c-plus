/* hundred_conversion.c -- 100 DISTINCT runtime scenarios for numeric
 * conversion and ctype/wctype methods of the C+ hardened libc.
 *
 * Standalone C99.  Links only build/libc/libcplus_hardened.a; the installed
 * host C runtime is the differential oracle.  Every call is a DEFINED call:
 * no NULL is passed to an unguarded wrapper, no signed overflow is performed
 * in this TU, and all ERANGE/EINVAL conditions are produced by the library
 * itself.  The only guarded NULL inputs exercised are atoi/atol,
 * strtoimax/strtoumax, wcstoimax/wcstoumax, wctype and wctrans.
 *
 * Each case prints exactly one line:
 *   hundred_conversion: case <n> <method> <scenario> <OUTCOME>
 * A contract violation or host differential mismatch emits a FINDING: line.
 * The whole 100-case table is executed twice (logging OFF, then logging ON)
 * and any case whose outcome changes is reported as a logging-parity FINDING.
 * main prints "hundred_conversion: <N>/100 scenarios ok" and returns 0.
 */
#include "cplus_libc.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

/* ------------------------------------------------------------------ state */

static unsigned long g_findings = 0;
static unsigned long g_ok = 0;
static int g_quiet = 0; /* suppress duplicate output during the ON pass */

#define FINDING(...)                                                          \
    do {                                                                      \
        ++g_findings;                                                         \
        if (!g_quiet) {                                                       \
            fputs("FINDING: hundred_conversion: ", stdout);                   \
            printf(__VA_ARGS__);                                              \
            fputc('\n', stdout);                                              \
        }                                                                     \
    } while (0)

#define CK(cond, ...)                                                         \
    do {                                                                      \
        if (!(cond)) FINDING(__VA_ARGS__);                                    \
    } while (0)

#define BIG19  "999999999999999999999999999999"
#define BIG30  "999999999999999999999999999999999"
#define BIG32  "99999999999999999999999999999999999"
#define BIG40  "9999999999999999999999999999999999999999999999"
#define WBIG   L"9999999999999999999999999999999999999999"

/* --------------------------------------------------- differential helpers */

static int same_d(double a, double b) {
    return a == b || (isnan(a) && isnan(b)) || memcmp(&a, &b, sizeof a) == 0;
}
static int same_f(float a, float b) {
    return a == b || (isnan(a) && isnan(b)) || memcmp(&a, &b, sizeof a) == 0;
}
static int same_ld(long double a, long double b) {
    return a == b || (isnan(a) && isnan(b)) || memcmp(&a, &b, sizeof a) == 0;
}

static int di_l(const char *s, int base) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    long cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtol(s, &ce, base); cerr = errno;
    errno = 0; hv = strtol(s, &he, base);       herr = errno;
    if (cv != hv) FINDING("strtol(base=%d): value %ld vs host %ld", base, cv, hv);
    if (ce != he) FINDING("strtol(base=%d): endptr %td vs host %td", base,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtol(base=%d): errno %d vs host %d", base, cerr, herr);
    return g_findings == f0;
}
static int di_l_nullend(const char *s, int base) {
    long cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtol(s, NULL, base);
    errno = 0; hv = strtol(s, NULL, base);
    if (cv != hv) FINDING("strtol(NULL endptr,base=%d): %ld vs host %ld", base, cv, hv);
    return g_findings == f0;
}
static int di_ul(const char *s, int base) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    unsigned long cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtoul(s, &ce, base); cerr = errno;
    errno = 0; hv = strtoul(s, &he, base);       herr = errno;
    if (cv != hv) FINDING("strtoul(base=%d): value %lu vs host %lu", base, cv, hv);
    if (ce != he) FINDING("strtoul(base=%d): endptr %td vs host %td", base,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtoul(base=%d): errno %d vs host %d", base, cerr, herr);
    return g_findings == f0;
}
static int di_ll(const char *s, int base) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    long long cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtoll(s, &ce, base); cerr = errno;
    errno = 0; hv = strtoll(s, &he, base);       herr = errno;
    if (cv != hv)
        FINDING("strtoll(base=%d): value %lld vs host %lld", base, cv, hv);
    if (ce != he) FINDING("strtoll(base=%d): endptr %td vs host %td", base,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtoll(base=%d): errno %d vs host %d", base, cerr, herr);
    return g_findings == f0;
}
static int di_ull(const char *s, int base) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    unsigned long long cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtoull(s, &ce, base); cerr = errno;
    errno = 0; hv = strtoull(s, &he, base);       herr = errno;
    if (cv != hv)
        FINDING("strtoull(base=%d): value %llu vs host %llu", base, cv, hv);
    if (ce != he) FINDING("strtoull(base=%d): endptr %td vs host %td", base,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtoull(base=%d): errno %d vs host %d", base, cerr, herr);
    return g_findings == f0;
}
static int di_d(const char *s) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    double cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtod(s, &ce); cerr = errno;
    errno = 0; hv = strtod(s, &he);       herr = errno;
    if (!same_d(cv, hv)) FINDING("strtod('%s'): %.17g vs host %.17g", s, cv, hv);
    if (ce != he) FINDING("strtod('%s'): endptr %td vs host %td", s,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtod('%s'): errno %d vs host %d", s, cerr, herr);
    return g_findings == f0;
}
static int di_f(const char *s) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    float cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtof(s, &ce); cerr = errno;
    errno = 0; hv = strtof(s, &he);       herr = errno;
    if (!same_f(cv, hv))
        FINDING("strtof('%s'): %.9g vs host %.9g", s, (double)cv, (double)hv);
    if (ce != he) FINDING("strtof('%s'): endptr %td vs host %td", s,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtof('%s'): errno %d vs host %d", s, cerr, herr);
    return g_findings == f0;
}
static int di_ld(const char *s) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    long double cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtold(s, &ce); cerr = errno;
    errno = 0; hv = strtold(s, &he);       herr = errno;
    if (!same_ld(cv, hv))
        FINDING("strtold('%s'): %.17Lg vs host %.17Lg", s, cv, hv);
    if (ce != he) FINDING("strtold('%s'): endptr %td vs host %td", s,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtold('%s'): errno %d vs host %d", s, cerr, herr);
    return g_findings == f0;
}
static int di_ai(const char *s) {
    int cv, hv;
    unsigned long f0 = g_findings;
    cv = cplus_atoi(s);
    hv = atoi(s);
    if (cv != hv) FINDING("atoi('%s'): %d vs host %d", s, cv, hv);
    return g_findings == f0;
}
static int di_al(const char *s) {
    long cv, hv;
    unsigned long f0 = g_findings;
    cv = cplus_atol(s);
    hv = atol(s);
    if (cv != hv) FINDING("atol('%s'): %ld vs host %ld", s, cv, hv);
    return g_findings == f0;
}
static int di_all(const char *s) {
    long long cv, hv;
    unsigned long f0 = g_findings;
    cv = cplus_atoll(s);
    hv = atoll(s);
    if (cv != hv) FINDING("atoll('%s'): %lld vs host %lld", s, cv, hv);
    return g_findings == f0;
}
static int di_af(const char *s) {
    double cv, hv;
    unsigned long f0 = g_findings;
    cv = cplus_atof(s);
    hv = atof(s);
    if (!same_d(cv, hv)) FINDING("atof('%s'): %.17g vs host %.17g", s, cv, hv);
    return g_findings == f0;
}
static int di_imax(const char *s, int base) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    intmax_t cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtoimax(s, &ce, base); cerr = errno;
    errno = 0; hv = strtoimax(s, &he, base);       herr = errno;
    if (cv != hv)
        FINDING("strtoimax(base=%d): %lld vs host %lld", base,
                (long long)cv, (long long)hv);
    if (ce != he) FINDING("strtoimax(base=%d): endptr %td vs host %td", base,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtoimax(base=%d): errno %d vs host %d", base, cerr, herr);
    return g_findings == f0;
}
static int di_umax(const char *s, int base) {
    char *ce = NULL, *he = NULL;
    int cerr, herr;
    uintmax_t cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_strtoumax(s, &ce, base); cerr = errno;
    errno = 0; hv = strtoumax(s, &he, base);       herr = errno;
    if (cv != hv)
        FINDING("strtoumax(base=%d): %llu vs host %llu", base,
                (unsigned long long)cv, (unsigned long long)hv);
    if (ce != he) FINDING("strtoumax(base=%d): endptr %td vs host %td", base,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("strtoumax(base=%d): errno %d vs host %d", base, cerr, herr);
    return g_findings == f0;
}
static int di_wimax(const wchar_t *s, int base) {
    wchar_t *ce = NULL, *he = NULL;
    int cerr, herr;
    intmax_t cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_wcstoimax(s, &ce, base); cerr = errno;
    errno = 0; hv = wcstoimax(s, &he, base);       herr = errno;
    if (cv != hv)
        FINDING("wcstoimax(base=%d): %lld vs host %lld", base,
                (long long)cv, (long long)hv);
    if (ce != he) FINDING("wcstoimax(base=%d): endptr %td vs host %td", base,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("wcstoimax(base=%d): errno %d vs host %d", base, cerr, herr);
    return g_findings == f0;
}
static int di_wumax(const wchar_t *s, int base) {
    wchar_t *ce = NULL, *he = NULL;
    int cerr, herr;
    uintmax_t cv, hv;
    unsigned long f0 = g_findings;
    errno = 0; cv = cplus_wcstoumax(s, &ce, base); cerr = errno;
    errno = 0; hv = wcstoumax(s, &he, base);       herr = errno;
    if (cv != hv)
        FINDING("wcstoumax(base=%d): %llu vs host %llu", base,
                (unsigned long long)cv, (unsigned long long)hv);
    if (ce != he) FINDING("wcstoumax(base=%d): endptr %td vs host %td", base,
                          ce ? ce - s : (ptrdiff_t)-1,
                          he ? he - s : (ptrdiff_t)-1);
    if (cerr != herr) FINDING("wcstoumax(base=%d): errno %d vs host %d", base, cerr, herr);
    return g_findings == f0;
}

/* Narrow ctype predicate differential across ASCII, EOF and high-bit bytes. */
static int dc(const char *name, int (*cf)(int), int (*hf)(int)) {
    static const int in[] = { 'A', 'z', '0', '9', ' ', '\t', '\n', '!',
                              0x7f, EOF, (int)(unsigned char)0x80,
                              (int)(unsigned char)0xff };
    unsigned long f0 = g_findings;
    size_t i;
    for (i = 0; i < sizeof in / sizeof in[0]; ++i) {
        int a = cf(in[i]), b = hf(in[i]);
        if ((a != 0) != (b != 0))
            FINDING("%s(%d)=%d vs host %d", name, in[i], a, b);
    }
    return g_findings == f0;
}

/* Wide ctype predicate differential across ASCII, WEOF and high wint. */
static int dw(const char *name, int (*cf)(wint_t), int (*hf)(wint_t)) {
    static const wint_t in[] = { L'a', L'Z', L'0', L'9', L' ', L'\t', L'!',
                                 (wint_t)0x80, (wint_t)0xff, WEOF };
    unsigned long f0 = g_findings;
    size_t i;
    for (i = 0; i < sizeof in / sizeof in[0]; ++i) {
        int a = cf(in[i]), b = hf(in[i]);
        if ((a != 0) != (b != 0))
            FINDING("%s(%lu)=%d vs host %d", name, (unsigned long)in[i], a, b);
    }
    return g_findings == f0;
}

/* ------------------------------------------------------------ case table */

typedef struct {
    const char *method;
    const char *scenario;
} Case;

static const Case cases[] = {
    /* 00-09 strtol */
    { "strtol", "decimal_ws_sign" },      { "strtol", "hex_base16" },
    { "strtol", "octal_base8" },          { "strtol", "base0_hex" },
    { "strtol", "base0_octal" },          { "strtol", "invalid_endptr" },
    { "strtol", "erange_pos" },           { "strtol", "erange_neg" },
    { "strtol", "base1_invalid" },        { "strtol", "null_endptr" },
    /* 10-15 strtoul */
    { "strtoul", "decimal" },             { "strtoul", "hex" },
    { "strtoul", "base0_octal" },         { "strtoul", "negative_wrap" },
    { "strtoul", "erange" },              { "strtoul", "base_INT_MIN_invalid" },
    /* 16-21 strtoll */
    { "strtoll", "decimal_neg" },         { "strtoll", "hex" },
    { "strtoll", "erange_pos" },          { "strtoll", "erange_neg" },
    { "strtoll", "invalid_endptr" },      { "strtoll", "base37_invalid" },
    /* 22-27 strtoull */
    { "strtoull", "decimal_max" },        { "strtoull", "modular_neg" },
    { "strtoull", "base0_hex" },          { "strtoull", "erange" },
    { "strtoull", "embedded_nul" },       { "strtoull", "base_INT_MAX_invalid" },
    /* 28-30 strtof */
    { "strtof", "decimal" },              { "strtof", "exponent" },
    { "strtof", "inf_nan" },
    /* 31-35 strtod */
    { "strtod", "decimal" },              { "strtod", "exponent_endptr" },
    { "strtod", "inf_nan" },              { "strtod", "erange_overflow" },
    { "strtod", "invalid" },
    /* 36-38 strtold */
    { "strtold", "decimal" },             { "strtold", "exponent" },
    { "strtold", "nan" },
    /* 39-41 atoi */
    { "atoi", "decimal" },                { "atoi", "junk_ws" },
    { "atoi", "null_guarded" },
    /* 42-44 atol */
    { "atol", "decimal" },                { "atol", "sign" },
    { "atol", "null_guarded" },
    /* 45-47 atoll */
    { "atoll", "large" },                 { "atoll", "junk" },
    { "atoll", "differential" },
    /* 48-50 atof */
    { "atof", "decimal" },                { "atof", "exponent_junk" },
    { "atof", "inf" },
    /* 51-54 strtoimax */
    { "strtoimax", "base0_hex" },         { "strtoimax", "decimal_ws" },
    { "strtoimax", "erange" },            { "strtoimax", "null_guarded" },
    /* 55-58 strtoumax */
    { "strtoumax", "base0" },             { "strtoumax", "modular_neg" },
    { "strtoumax", "erange" },            { "strtoumax", "null_guarded" },
    /* 59-62 wcstoimax */
    { "wcstoimax", "decimal" },           { "wcstoimax", "hex_base0" },
    { "wcstoimax", "erange" },            { "wcstoimax", "null_guarded" },
    /* 63-66 wcstoumax */
    { "wcstoumax", "decimal" },           { "wcstoumax", "modular_neg" },
    { "wcstoumax", "erange" },            { "wcstoumax", "null_guarded" },
    /* 67-68 imaxabs */
    { "imaxabs", "sign" },                { "imaxabs", "max" },
    /* 69-70 imaxdiv */
    { "imaxdiv", "mixed_signs" },         { "imaxdiv", "extremes" },
    /* 71-72 abs */
    { "abs", "negative" },                { "abs", "positive_max" },
    /* 73-74 labs */
    { "labs", "negative" },               { "labs", "positive_max" },
    /* 75-76 llabs */
    { "llabs", "negative" },              { "llabs", "positive_max" },
    /* 77-78 div */
    { "div", "mixed_signs" },             { "div", "positive" },
    /* 79-80 ldiv */
    { "ldiv", "mixed_signs" },            { "ldiv", "positive" },
    /* 81-82 lldiv */
    { "lldiv", "mixed_signs" },           { "lldiv", "positive" },
    /* 83-90 ctype */
    { "cplus_is*", "alpha_digit_alnum" }, { "cplus_is*", "upper_lower" },
    { "cplus_is*", "xdigit" },            { "cplus_is*", "print_graph_punct" },
    { "cplus_is*", "cntrl_blank_space" }, { "cplus_is*", "isascii_toascii" },
    { "toupper", "eof_and_identity" },    { "tolower", "high_bit" },
    /* 91-95 wctype */
    { "cplus_isw*", "alpha_digit_alnum" },{ "cplus_isw*", "upper_lower_xdigit" },
    { "cplus_isw*", "print_graph_etc" },  { "towupper", "towlower_eof" },
    { "wctype", "iswctype_wctrans_towctrans" },
    /* 96-99 logging edge inputs */
    { "logging", "off_on_parity_strtol" },{ "logging", "on_off_parity_ctype" },
    { "strtol", "high_bit_byte" },        { "strtod", "embedded_nul" },
};

typedef char assert_case_count[(sizeof cases / sizeof cases[0]) == 100 ? 1 : -1];

/* ----------------------------------------------------------- dispatcher */

static int run_case(int id) {
    unsigned long f = g_findings;
    switch (id) {
    /* 00-09 strtol */
    case 0:  return di_l("  42rest", 10) && g_findings == f;
    case 1:  return di_l("0x2aff", 16) && g_findings == f;
    case 2:  return di_l("077", 8) && g_findings == f;
    case 3:  return di_l("0x1f", 0) && g_findings == f;
    case 4:  return di_l("064", 0) && g_findings == f;
    case 5:  return di_l("not-a-number", 10) && g_findings == f;
    case 6: { char *e; long v; errno = 0; v = cplus_strtol(BIG19, &e, 10);
              CK(v == LONG_MAX && errno == ERANGE, "strtol ERANGE+: v=%ld errno=%d", v, errno);
              (void)di_l(BIG19, 10); return g_findings == f; }
    case 7: { char *e; long v; errno = 0; v = cplus_strtol("-" BIG19, &e, 10);
              CK(v == LONG_MIN && errno == ERANGE, "strtol ERANGE-: v=%ld errno=%d", v, errno);
              (void)di_l("-" BIG19, 10); return g_findings == f; }
    case 8:  return di_l("10", 1) && g_findings == f;
    case 9:  return di_l_nullend(" -981xyz", 10) && g_findings == f;

    /* 10-15 strtoul */
    case 10: return di_ul("42tail", 10) && g_findings == f;
    case 11: return di_ul("0xff", 16) && g_findings == f;
    case 12: return di_ul("077", 0) && g_findings == f;
    case 13: { char *e; unsigned long v; errno = 0; v = cplus_strtoul("-1", &e, 10);
               CK(v == ULONG_MAX && errno == 0, "strtoul -1: %lu errno=%d", v, errno);
               (void)di_ul("-1", 10); return g_findings == f; }
    case 14: { char *e; unsigned long v; errno = 0; v = cplus_strtoul(BIG30, &e, 10);
               CK(v == ULONG_MAX && errno == ERANGE, "strtoul ERANGE: %lu errno=%d", v, errno);
               (void)di_ul(BIG30, 10); return g_findings == f; }
    case 15: return di_ul("10", INT_MIN) && g_findings == f;

    /* 16-21 strtoll */
    case 16: return di_ll(" -9223372036854775807z", 10) && g_findings == f;
    case 17: return di_ll("0x2a", 16) && g_findings == f;
    case 18: { char *e; long long v; errno = 0; v = cplus_strtoll(BIG30, &e, 10);
               CK(v == LLONG_MAX && errno == ERANGE, "strtoll ERANGE+: %lld errno=%d", v, errno);
               (void)di_ll(BIG30, 10); return g_findings == f; }
    case 19: { char *e; long long v; errno = 0; v = cplus_strtoll("-" BIG30, &e, 10);
               CK(v == LLONG_MIN && errno == ERANGE, "strtoll ERANGE-: %lld errno=%d", v, errno);
               (void)di_ll("-" BIG30, 10); return g_findings == f; }
    case 20: return di_ll("nope", 10) && g_findings == f;
    case 21: return di_ll("10", 37) && g_findings == f;

    /* 22-27 strtoull */
    case 22: { char *e; unsigned long long v; errno = 0;
               v = cplus_strtoull("18446744073709551615", &e, 10);
               CK(v == ULLONG_MAX && errno == 0, "strtoull max: %llu errno=%d", v, errno);
               (void)di_ull("18446744073709551615", 10); return g_findings == f; }
    case 23: { char *e; unsigned long long v; errno = 0; v = cplus_strtoull("-2", &e, 10);
               CK(v == ULLONG_MAX - 1ULL && errno == 0, "strtoull -2: %llu errno=%d", v, errno);
               (void)di_ull("-2", 10); return g_findings == f; }
    case 24: return di_ull("0XFF", 0) && g_findings == f;
    case 25: { char *e; unsigned long long v; errno = 0; v = cplus_strtoull(BIG40, &e, 10);
               CK(v == ULLONG_MAX && errno == ERANGE, "strtoull ERANGE: %llu errno=%d", v, errno);
               (void)di_ull(BIG40, 10); return g_findings == f; }
    case 26: return di_ull("12\0 34", 10) && g_findings == f;
    case 27: return di_ull("10", INT_MAX) && g_findings == f;

    /* 28-30 strtof */
    case 28: return di_f("6.25e2") && g_findings == f;
    case 29: return di_f("1.25e-3") && g_findings == f;
    case 30: return di_f("inf") && di_f("nan(foo)") && g_findings == f;

    /* 31-35 strtod */
    case 31: return di_d("3.14159265358979") && g_findings == f;
    case 32: return di_d("1.5e3tail") && g_findings == f;
    case 33: return di_d("-INFINITY") && di_d("nan(foo)") && g_findings == f;
    case 34: { char *e; double v; errno = 0; v = cplus_strtod("1e400", &e);
               CK(isinf(v) && errno == ERANGE, "strtod 1e400: %.17g errno=%d", v, errno);
               (void)di_d("1e400"); return g_findings == f; }
    case 35: return di_d("xyz") && g_findings == f;

    /* 36-38 strtold */
    case 36: return di_ld("2.5e4") && g_findings == f;
    case 37: return di_ld("9.75e2") && g_findings == f;
    case 38: return di_ld("nan") && g_findings == f;

    /* 39-41 atoi */
    case 39: return di_ai("123") && g_findings == f;
    case 40: return di_ai("  -42x") && g_findings == f;
    case 41: CK(cplus_atoi(NULL) == 0, "atoi(NULL) != 0"); return g_findings == f;

    /* 42-44 atol */
    case 42: return di_al("1234567") && g_findings == f;
    case 43: return di_al("  -99") && g_findings == f;
    case 44: CK(cplus_atol(NULL) == 0L, "atol(NULL) != 0"); return g_findings == f;

    /* 45-47 atoll */
    case 45: return di_all("9000000000") && g_findings == f;
    case 46: return di_all("  -9000000000xyz") && g_findings == f;
    case 47: return di_all("123456789012") && g_findings == f;

    /* 48-50 atof */
    case 48: return di_af("3.5") && g_findings == f;
    case 49: return di_af("1e3junk") && g_findings == f;
    case 50: { double v = cplus_atof("inf");
               CK(isinf(v), "atof(inf) not infinite");
               (void)di_af("inf"); return g_findings == f; }

    /* 51-54 strtoimax */
    case 51: return di_imax("0x2a", 0) && g_findings == f;
    case 52: return di_imax(" -77z", 10) && g_findings == f;
    case 53: { char *e; intmax_t v; errno = 0; v = cplus_strtoimax(BIG32, &e, 10);
               CK(v == INTMAX_MAX && errno == ERANGE, "strtoimax ERANGE: %lld errno=%d",
                   (long long)v, errno);
               (void)di_imax(BIG32, 10); return g_findings == f; }
    case 54: { char *e = (char *)0x1; intmax_t v; errno = 0;
               v = cplus_strtoimax(NULL, &e, 10);
               CK(v == 0 && errno == EINVAL && e == NULL,
                  "strtoimax(NULL): v=%lld errno=%d e=%p", (long long)v, errno, (void *)e);
               return g_findings == f; }

    /* 55-58 strtoumax */
    case 55: return di_umax("0x10", 16) && g_findings == f;
    case 56: { char *e; uintmax_t v; errno = 0; v = cplus_strtoumax("-1", &e, 10);
               CK(v == UINTMAX_MAX && errno == 0, "strtoumax -1: %llu errno=%d",
                  (unsigned long long)v, errno);
               (void)di_umax("-1", 10); return g_findings == f; }
    case 57: { char *e; uintmax_t v; errno = 0; v = cplus_strtoumax(BIG40, &e, 10);
               CK(v == UINTMAX_MAX && errno == ERANGE, "strtoumax ERANGE: %llu errno=%d",
                  (unsigned long long)v, errno);
               (void)di_umax(BIG40, 10); return g_findings == f; }
    case 58: { char *e = (char *)0x1; uintmax_t v; errno = 0;
               v = cplus_strtoumax(NULL, &e, 10);
               CK(v == 0 && errno == EINVAL && e == NULL,
                  "strtoumax(NULL): v=%llu errno=%d e=%p", (unsigned long long)v, errno, (void *)e);
               return g_findings == f; }

    /* 59-62 wcstoimax */
    case 59: return di_wimax(L" -42", 10) && g_findings == f;
    case 60: return di_wimax(L"0x2a", 0) && g_findings == f;
    case 61: { wchar_t *e; intmax_t v; errno = 0; v = cplus_wcstoimax(WBIG, &e, 10);
               CK(v == INTMAX_MAX && errno == ERANGE, "wcstoimax ERANGE: %lld errno=%d",
                   (long long)v, errno);
               (void)di_wimax(WBIG, 10); return g_findings == f; }
    case 62: { wchar_t *e = (wchar_t *)0x1; intmax_t v; errno = 0;
               v = cplus_wcstoimax(NULL, &e, 10);
               CK(v == 0 && errno == EINVAL && e == NULL,
                  "wcstoimax(NULL): v=%lld errno=%d", (long long)v, errno);
               return g_findings == f; }

    /* 63-66 wcstoumax */
    case 63: return di_wumax(L"8888rest", 10) && g_findings == f;
    case 64: { wchar_t *e; uintmax_t v; errno = 0; v = cplus_wcstoumax(L"-1", &e, 10);
               CK(v == UINTMAX_MAX && errno == 0, "wcstoumax -1: %llu errno=%d",
                  (unsigned long long)v, errno);
               (void)di_wumax(L"-1", 10); return g_findings == f; }
    case 65: { wchar_t *e; uintmax_t v; errno = 0; v = cplus_wcstoumax(WBIG, &e, 10);
               CK(v == UINTMAX_MAX && errno == ERANGE, "wcstoumax ERANGE: %llu errno=%d",
                  (unsigned long long)v, errno);
               (void)di_wumax(WBIG, 10); return g_findings == f; }
    case 66: { wchar_t *e = (wchar_t *)0x1; uintmax_t v; errno = 0;
               v = cplus_wcstoumax(NULL, &e, 10);
               CK(v == 0 && errno == EINVAL && e == NULL,
                  "wcstoumax(NULL): v=%llu errno=%d", (unsigned long long)v, errno);
               return g_findings == f; }

    /* 67-68 imaxabs */
    case 67: CK(cplus_imaxabs((intmax_t)-77) == imaxabs((intmax_t)-77) &&
                cplus_imaxabs((intmax_t)12345) == (intmax_t)12345,
                "imaxabs sign mismatch"); return g_findings == f;
    case 68: CK(cplus_imaxabs(INTMAX_MAX) == INTMAX_MAX,
                "imaxabs(INTMAX_MAX) mismatch"); return g_findings == f;

    /* 69-70 imaxdiv */
    case 69: { imaxdiv_t a = cplus_imaxdiv(-7, 3), b = imaxdiv(-7, 3);
               CK(a.quot == b.quot && a.rem == b.rem && a.quot == -2 && a.rem == -1,
                  "imaxdiv(-7,3) mismatch"); return g_findings == f; }
    case 70: { imaxdiv_t a = cplus_imaxdiv(INTMAX_MAX, 1), b = imaxdiv(INTMAX_MAX, 1);
               CK(a.quot == b.quot && a.rem == b.rem && a.quot == INTMAX_MAX,
                  "imaxdiv(INTMAX_MAX,1) mismatch"); return g_findings == f; }

    /* 71-72 abs */
    case 71: CK(cplus_abs(-42) == abs(-42) && cplus_abs(-42) == 42,
                "abs(-42) mismatch"); return g_findings == f;
    case 72: CK(cplus_abs(INT_MAX) == INT_MAX, "abs(INT_MAX) mismatch");
             return g_findings == f;

    /* 73-74 labs */
    case 73: CK(cplus_labs(-1234567L) == labs(-1234567L), "labs mismatch");
             return g_findings == f;
    case 74: CK(cplus_labs(LONG_MAX) == LONG_MAX, "labs(LONG_MAX) mismatch");
             return g_findings == f;

    /* 75-76 llabs */
    case 75: CK(cplus_llabs(-9000000007LL) == llabs(-9000000007LL), "llabs mismatch");
             return g_findings == f;
    case 76: CK(cplus_llabs(LLONG_MAX) == LLONG_MAX, "llabs(LLONG_MAX) mismatch");
             return g_findings == f;

    /* 77-78 div */
    case 77: { div_t a = cplus_div(-17, 5), b = div(-17, 5);
               CK(a.quot == b.quot && a.rem == b.rem && a.quot == -3 && a.rem == -2,
                  "div(-17,5) mismatch"); return g_findings == f; }
    case 78: { div_t a = cplus_div(100, 7), b = div(100, 7);
               CK(a.quot == b.quot && a.rem == b.rem, "div(100,7) mismatch");
               return g_findings == f; }

    /* 79-80 ldiv */
    case 79: { ldiv_t a = cplus_ldiv(-17L, 5L), b = ldiv(-17L, 5L);
               CK(a.quot == b.quot && a.rem == b.rem, "ldiv mismatch");
               return g_findings == f; }
    case 80: { ldiv_t a = cplus_ldiv(100003L, 7L), b = ldiv(100003L, 7L);
               CK(a.quot == b.quot && a.rem == b.rem, "ldiv large mismatch");
               return g_findings == f; }

    /* 81-82 lldiv */
    case 81: { lldiv_t a = cplus_lldiv(-9000000007LL, 13LL),
                  b = lldiv(-9000000007LL, 13LL);
               CK(a.quot == b.quot && a.rem == b.rem, "lldiv mismatch");
               return g_findings == f; }
    case 82: { lldiv_t a = cplus_lldiv(100LL, 7LL), b = lldiv(100LL, 7LL);
               CK(a.quot == b.quot && a.rem == b.rem, "lldiv small mismatch");
               return g_findings == f; }

    /* 83-90 ctype */
    case 83: return dc("cplus_isalpha", cplus_isalpha, isalpha) &&
                dc("cplus_isdigit", cplus_isdigit, isdigit) &&
                dc("cplus_isalnum", cplus_isalnum, isalnum) && g_findings == f;
    case 84: return dc("cplus_isupper", cplus_isupper, isupper) &&
                dc("cplus_islower", cplus_islower, islower) && g_findings == f;
    case 85: return dc("cplus_isxdigit", cplus_isxdigit, isxdigit) && g_findings == f;
    case 86: return dc("cplus_isprint", cplus_isprint, isprint) &&
                dc("cplus_isgraph", cplus_isgraph, isgraph) &&
                dc("cplus_ispunct", cplus_ispunct, ispunct) && g_findings == f;
    case 87: return dc("cplus_iscntrl", cplus_iscntrl, iscntrl) &&
                dc("cplus_isblank", cplus_isblank, isblank) &&
                dc("cplus_isspace", cplus_isspace, isspace) && g_findings == f;
    case 88: CK(cplus_isascii('A') == 1 && cplus_isascii(0x80) == 0 &&
                cplus_isascii(-1) == 0 && cplus_toascii(0xE1) == 0x61,
                "isascii/toascii mismatch"); return g_findings == f;
    case 89: CK(cplus_toupper(EOF) == EOF && cplus_toupper('a') == 'A' &&
                cplus_toupper('A') == 'A' && cplus_toupper('7') == '7' &&
                cplus_tolower(EOF) == EOF && cplus_tolower('Z') == 'z' &&
                cplus_tolower('z') == 'z' && cplus_tolower('7') == '7',
                "toupper/tolower EOF or identity mismatch"); return g_findings == f;
    case 90: { int hi = (int)(unsigned char)0x80, hf = (int)(unsigned char)0xff;
               CK(cplus_toupper(hi) == toupper(hi) &&
                  cplus_tolower(hi) == tolower(hi) &&
                  cplus_toupper(hf) == toupper(hf) &&
                  cplus_tolower(hf) == tolower(hf),
                  "toupper/tolower high-bit mismatch"); return g_findings == f; }

    /* 91-95 wctype */
    case 91: return dw("cplus_iswalpha", cplus_iswalpha, iswalpha) &&
                dw("cplus_iswdigit", cplus_iswdigit, iswdigit) &&
                dw("cplus_iswalnum", cplus_iswalnum, iswalnum) && g_findings == f;
    case 92: return dw("cplus_iswupper", cplus_iswupper, iswupper) &&
                dw("cplus_iswlower", cplus_iswlower, iswlower) &&
                dw("cplus_iswxdigit", cplus_iswxdigit, iswxdigit) && g_findings == f;
    case 93: return dw("cplus_iswprint", cplus_iswprint, iswprint) &&
                dw("cplus_iswgraph", cplus_iswgraph, iswgraph) &&
                dw("cplus_iswpunct", cplus_iswpunct, iswpunct) &&
                dw("cplus_iswcntrl", cplus_iswcntrl, iswcntrl) &&
                dw("cplus_iswblank", cplus_iswblank, iswblank) &&
                dw("cplus_iswspace", cplus_iswspace, iswspace) && g_findings == f;
    case 94: CK(cplus_towupper(L'a') == (wint_t)L'A' &&
                cplus_towlower(L'A') == (wint_t)L'a' &&
                cplus_towupper(WEOF) == towupper(WEOF) &&
                cplus_towlower(WEOF) == towlower(WEOF),
                "towupper/towlower mismatch"); return g_findings == f;
    case 95: { wctype_t al = cplus_wctype("alpha");
               wctrans_t up = cplus_wctrans("toupper");
               CK(al != (wctype_t)0 && up != (wctrans_t)0, "descriptor zero");
               CK(cplus_iswctype((wint_t)L'x', al) == (iswctype((wint_t)L'x', wctype("alpha")) != 0) &&
                  cplus_iswctype((wint_t)L'3', al) == (iswctype((wint_t)L'3', wctype("alpha")) != 0),
                  "iswctype mismatch");
               CK(cplus_towctrans((wint_t)L'a', up) == towctrans((wint_t)L'a', wctrans("toupper")),
                  "towctrans mismatch");
               errno = 0; CK(cplus_wctype(NULL) == (wctype_t)0 && errno == EINVAL,
                             "wctype(NULL) guard");
               errno = 0; CK(cplus_wctrans(NULL) == (wctrans_t)0 && errno == EINVAL,
                             "wctrans(NULL) guard");
               return g_findings == f; }

    /* 96-99 logging + edge inputs */
    case 96: { int off, on, host;
               cplus_libc_log_set_enabled(0);
               errno = 0; off = (int)cplus_strtol(" -123z", NULL, 10);
               cplus_libc_log_set_enabled(1);
               errno = 0; on = (int)cplus_strtol(" -123z", NULL, 10);
               cplus_libc_log_set_enabled(0);
               host = (int)strtol(" -123z", NULL, 10);
               CK(off == on && off == host, "logging strtol parity: off=%d on=%d host=%d",
                  off, on, host);
               return g_findings == f; }
    case 97: { int off, on, host;
               cplus_libc_log_set_enabled(0);
               off = cplus_isalpha('A');
               cplus_libc_log_set_enabled(1);
               on = cplus_isalpha('A');
               cplus_libc_log_set_enabled(0);
               host = isalpha('A') != 0;
               CK(off == on && off == host, "logging ctype parity: off=%d on=%d host=%d",
                  off, on, host);
               return g_findings == f; }
    case 98: { const char hb[] = { (char)0x80, '7', '\0' };
               return di_l(hb, 10) && g_findings == f; }
    case 99: return di_d("3.5\0junk") && g_findings == f;

    default: FINDING("unknown case id %d", id); return 0;
    }
}

/* ------------------------------------------------------------------- main */

int main(void) {
    unsigned char st_off[100], st_on[100];
    int i, saved = -1, devnull = -1;

    for (i = 0; i < 100; ++i) st_off[i] = 0;

    /* Pass 1: logging OFF -- this is the pass whose output is printed. */
    cplus_libc_log_set_enabled(0);
    g_quiet = 0;
    for (i = 0; i < 100; ++i) {
        int ok;
        cplus_libc_log_set_enabled(0);
        ok = run_case(i);
        st_off[i] = (unsigned char)(ok != 0);
        if (st_off[i]) ++g_ok;
        printf("hundred_conversion: case %d %s %s %s\n", i + 1,
               cases[i].method, cases[i].scenario,
               st_off[i] ? "ok" : "FINDING");
    }

    /* Pass 2: logging ON -- silent; compare each outcome with pass 1. */
    saved = dup(2);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0 && saved >= 0) (void)dup2(devnull, 2);
    cplus_libc_log_set_enabled(1);
    g_quiet = 1;
    for (i = 0; i < 100; ++i) {
        cplus_libc_log_set_enabled(1);
        st_on[i] = (unsigned char)(run_case(i) != 0);
    }
    g_quiet = 0;
    cplus_libc_log_set_enabled(0);
    if (saved >= 0) {
        (void)dup2(saved, 2);
        (void)close(saved);
    }
    if (devnull >= 0) (void)close(devnull);

    for (i = 0; i < 100; ++i) {
        if (st_on[i] != st_off[i])
            FINDING("logging parity case %d (%s %s): off=%d on=%d",
                    i + 1, cases[i].method, cases[i].scenario,
                    (int)st_off[i], (int)st_on[i]);
    }

    printf("hundred_conversion: %lu/100 scenarios ok\n", g_ok);
    return 0;
}
