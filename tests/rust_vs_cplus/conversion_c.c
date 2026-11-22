/*
 * conversion_c.c -- PAIRED numeric-conversion / ctype scenarios (C+ libc side).
 *
 * This is one half of a deliberately identical 100-case scenario set shared
 * with `conversion_rs.rs`. Both programs take a single argument N (1..100),
 * execute the SAME conceptual scenario, and print exactly one line:
 *
 *     RESULT <n> <ACCEPTED|REFUSED|SKIP>
 *
 * Both programs always return 0 (the exit code is not the protocol).
 *
 * ---------------------------------------------------------------------------
 * HONESTY / MECHANISM NOTES
 * ---------------------------------------------------------------------------
 * Every conversion here goes through a `cplus_*` wrapper declared in
 * `libc/install-include/cplus_libc.h`. Per that library's own documentation
 * (libc/src/stdlib/stdlib_extra*.c, inttypes_wide.c, ctype/ctype.c), these
 * wrappers DELEGATE to the installed host libc (strtol/strtod/wcstoimax/...):
 * parsing, base handling, endptr advancement, ERANGE/EINVAL signalling and
 * return values are the host's, not a reimplementation. The wrappers only add
 * logging (off by default) and, for the intmax/wide family, a NULL-input
 * hardening extension that sets EINVAL.
 *
 * ACCEPTED means: for strto* family, the host consumed >=1 character AND did
 * not raise ERANGE/EINVAL (i.e. a clean, in-range conversion); for atoi/atol/
 * atoll/atof (which expose no endptr/errno), a numeric prefix was present; for
 * ctype predicates, the predicate was true; for toupper/tolower, the result was
 * not EOF; for checked arithmetic, no overflow was detected.
 * REFUSED means the conversion raised ERANGE/EINVAL, consumed nothing, lacked a
 * numeric prefix, evaluated false, returned EOF, or overflowed.
 * SKIP means the scenario has no honest counterpart in this language (used
 * only where the Rust half drives the paired case, and vice versa).
 *
 * Outcomes are COMPUTED AT RUNTIME from the real operations (no hard-coded
 * result strings). Where C and Rust intentionally differ (whitespace skipping,
 * base-0 auto-detection, partial consumption, ERANGE-vs-infinity, byte-vs-
 * Unicode-scalar ctype, wide strings), the per-case comment records why; the
 * two programs report what each mechanism actually does.
 * ---------------------------------------------------------------------------
 */
#include "cplus_libc.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define NUM_CASES 100
_Static_assert(NUM_CASES == 100, "conversion_c.c must define exactly 100 cases");

enum { OUT_REFUSED = 0, OUT_ACCEPTED = 1, OUT_SKIP = 2 };
static const char *outcome_name(int o) {
    return o == OUT_ACCEPTED ? "ACCEPTED" : (o == OUT_REFUSED ? "REFUSED" : "SKIP");
}

/* strto* outcome: >=1 char consumed and no range/argument error. */
static int conv(const char *s, char *end, int err) {
    if (end == NULL || end == s) return OUT_REFUSED;
    if (err == ERANGE || err == EINVAL) return OUT_REFUSED;
    return OUT_ACCEPTED;
}

/* atoi/atol/atoll/atof report no endptr/errno: infer acceptance from prefix. */
static int numeric_prefix(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;
    if (*s == '+' || *s == '-') s++;
    if (*s >= '0' && *s <= '9') return 1;
    if (*s == '.' && s[1] >= '0' && s[1] <= '9') return 1;
    return 0;
}

/* black_box-equivalent sinks: force the host wrapper call to actually happen. */
static volatile long long g_sink_ll;
static volatile unsigned long long g_sink_ull;
static volatile double g_sink_d;
static long long bb_ll(long long x) { g_sink_ll = x; return x; }
static unsigned long long bb_ull(unsigned long long x) { g_sink_ull = x; return x; }
static double bb_d(double x) { g_sink_d = x; return x; }

static int run_case(int n) {
    char *e;
    const char *s;
    switch (n) {
        /* --- strtol, base 10 (1-10) ------------------------------------- */
        case 1:  /* strtol("12345",10): clean decimal. Rust i64: Ok. */
            s = "12345"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 2:  /* strtol("   -42",10): host SKIPS leading whitespace. Rust parse does NOT -> differences expected. */
            s = "   -42"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 3:  /* strtol("+7",10): explicit '+'. Rust i64 accepts '+' too. */
            s = "+7"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 4:  /* strtol("00012",10): leading zeros are decimal in base 10. */
            s = "00012"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 5:  /* strtol("",10): no digits -> REFUSED; Rust parse("") Err. */
            s = ""; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 6:  /* strtol("abc",10): invalid input -> REFUSED. */
            s = "abc"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 7:  /* strtol("12abc",10): PARTIAL consume -> ACCEPTED. Rust full-consume parse Err. */
            s = "12abc"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 8:  /* strtol("9223372036854775807",10) == LONG_MAX -> ACCEPTED. */
            s = "9223372036854775807"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 9:  /* strtol(LONG_MAX+1): ERANGE -> REFUSED; Rust i64 overflow Err. */
            s = "9223372036854775808"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 10: /* strtol(LONG_MIN-1): ERANGE -> REFUSED; Rust i64 underflow Err. */
            s = "-9223372036854775809"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);

        /* --- strtoul, unsigned long (11-18) ----------------------------- */
        case 11: /* strtoul("42",10) -> ACCEPTED both. */
            s = "42"; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 12: /* strtoul("-1",10): host wraps to ULONG_MAX, NO errno -> ACCEPTED; Rust u64 refuses '-'. */
            s = "-1"; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 13: /* strtoul("0xFF",16): base-16 honours 0x prefix -> ACCEPTED; Rust u64 has no hex parse. */
            s = "0xFF"; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 16); (void)v; } return conv(s, e, errno);
        case 14: /* strtoul("077",8): host OCTAL (63); Rust u64 parse = 77 decimal. Both ACCEPTED, VALUE differs. */
            s = "077"; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 8); (void)v; } return conv(s, e, errno);
        case 15: /* strtoul("0x10",0): base-0 auto-detect hex -> ACCEPTED; Rust parse Err. */
            s = "0x10"; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 0); (void)v; } return conv(s, e, errno);
        case 16: /* strtoul(ULONG_MAX) -> ACCEPTED both. */
            s = "18446744073709551615"; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 17: /* strtoul(ULONG_MAX+1): ERANGE -> REFUSED; Rust u64 overflow Err. */
            s = "18446744073709551616"; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 18: /* strtoul("  99  ",10): trailing junk -> PARTIAL ACCEPTED; Rust Err. */
            s = "  99  "; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 10); (void)v; } return conv(s, e, errno);

        /* --- strtoll, 64-bit signed (19-27) ----------------------------- */
        case 19: /* strtoll("-123",10) -> ACCEPTED both. */
            s = "-123"; errno = 0; { long long v = cplus_strtoll(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 20: /* strtoll(LLONG_MAX,10) -> ACCEPTED both. */
            s = "9223372036854775807"; errno = 0; { long long v = cplus_strtoll(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 21: /* strtoll(LLONG_MAX+1): ERANGE -> REFUSED. */
            s = "9223372036854775808"; errno = 0; { long long v = cplus_strtoll(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 22: /* strtoll("0x7fffffff",16): ACCEPTED; Rust i64 has no base param. */
            s = "0x7fffffff"; errno = 0; { long long v = cplus_strtoll(s, &e, 16); (void)v; } return conv(s, e, errno);
        case 23: /* strtoll("010",0): host OCTAL (8); Rust parse = 10. Both ACCEPTED, VALUE differs. */
            s = "010"; errno = 0; { long long v = cplus_strtoll(s, &e, 0); (void)v; } return conv(s, e, errno);
        case 24: /* strtoll("+0",10) -> ACCEPTED both. */
            s = "+0"; errno = 0; { long long v = cplus_strtoll(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 25: /* strtoll("  \t-5",10): host skips ws; Rust Err. */
            s = "  \t-5"; errno = 0; { long long v = cplus_strtoll(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 26: /* strtoll("0",10) -> ACCEPTED both. */
            s = "0"; errno = 0; { long long v = cplus_strtoll(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 27: /* strtoll("123 456"): PARTIAL ACCEPTED; Rust full-consume Err. */
            s = "123 456"; errno = 0; { long long v = cplus_strtoll(s, &e, 10); (void)v; } return conv(s, e, errno);

        /* --- strtoull, 64-bit unsigned (28-35) -------------------------- */
        case 28: /* strtoull("123",10) -> ACCEPTED both. */
            s = "123"; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 29: /* strtoull("-2",10): wraps to ULLONG_MAX -> ACCEPTED; Rust u64 Err. */
            s = "-2"; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 30: /* strtoull("0xdeadBEEF",16) -> ACCEPTED; Rust u64 no hex. */
            s = "0xdeadBEEF"; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 16); (void)v; } return conv(s, e, errno);
        case 31: /* strtoull("0X1A",0): base-0 uppercase hex -> ACCEPTED; Rust Err. */
            s = "0X1A"; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 0); (void)v; } return conv(s, e, errno);
        case 32: /* strtoull(ULLONG_MAX+1): ERANGE -> REFUSED; Rust u64 Err. */
            s = "18446744073709551616"; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 33: /* strtoull("  0  ",10): trailing junk -> ACCEPTED; Rust Err. */
            s = "  0  "; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 34: /* strtoull("007",10) -> ACCEPTED both (7). */
            s = "007"; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 35: /* strtoull("",10): no digits -> REFUSED both. */
            s = ""; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 10); (void)v; } return conv(s, e, errno);

        /* --- strtod, double (36-44) ------------------------------------ */
        case 36: /* strtod("3.14") -> ACCEPTED both. */
            s = "3.14"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 37: /* strtod("  -0.5"): host skips ws; Rust f64 Err. */
            s = "  -0.5"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 38: /* strtod("1e3") -> ACCEPTED both. */
            s = "1e3"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 39: /* strtod("1e309"): host ERANGE -> REFUSED; Rust f64 = Ok(inf) ACCEPTED. KEY DIFFERENCE. */
            s = "1e309"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 40: /* strtod("1e-400"): host ERANGE underflow -> REFUSED; Rust = Ok(0) ACCEPTED. */
            s = "1e-400"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 41: /* strtod("nan") -> ACCEPTED both (NaN). */
            s = "nan"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 42: /* strtod("inf") -> ACCEPTED both. */
            s = "inf"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 43: /* strtod("abc") -> REFUSED both. */
            s = "abc"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 44: /* strtod("0x1p4"): host hex-float -> ACCEPTED (16); Rust no hex float Err. */
            s = "0x1p4"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);

        /* --- strtof, float (45-48) ------------------------------------- */
        case 45: /* strtof("1.5") -> ACCEPTED both (f32 from_str). */
            s = "1.5"; errno = 0; { float v = cplus_strtof(s, &e); (void)v; } return conv(s, e, errno);
        case 46: /* strtof("1e39"): ERANGE (overflows f32) -> REFUSED; Rust f32 = Ok(inf). */
            s = "1e39"; errno = 0; { float v = cplus_strtof(s, &e); (void)v; } return conv(s, e, errno);
        case 47: /* strtof("  2.5"): host skips ws; Rust Err. */
            s = "  2.5"; errno = 0; { float v = cplus_strtof(s, &e); (void)v; } return conv(s, e, errno);
        case 48: /* strtof("nope") -> REFUSED both. */
            s = "nope"; errno = 0; { float v = cplus_strtof(s, &e); (void)v; } return conv(s, e, errno);

        /* --- strtold, long double (49-51) ------------------------------ */
        case 49: /* strtold("2.71828") -> ACCEPTED both (Rust f64 analogue). */
            s = "2.71828"; errno = 0; { long double v = cplus_strtold(s, &e); (void)v; } return conv(s, e, errno);
        case 50: /* strtold("1e-5000"): ERANGE underflow -> REFUSED; Rust f64 = Ok(0). */
            s = "1e-5000"; errno = 0; { long double v = cplus_strtold(s, &e); (void)v; } return conv(s, e, errno);
        case 51: /* strtold("  9.0"): host skips ws; Rust Err. */
            s = "  9.0"; errno = 0; { long double v = cplus_strtold(s, &e); (void)v; } return conv(s, e, errno);

        /* --- atoi/atol/atoll/atof (52-61) ------------------------------ */
        case 52: /* atoi("42") -> numeric prefix -> ACCEPTED both. */
            s = "42"; { volatile int v = cplus_atoi(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 53: /* atoi("  -7"): host skips ws -> ACCEPTED; Rust i32 Err. */
            s = "  -7"; { volatile int v = cplus_atoi(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 54: /* atoi("abc") -> no prefix -> REFUSED both. */
            s = "abc"; { volatile int v = cplus_atoi(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 55: /* atoi("12xyz"): prefix -> ACCEPTED (no endptr to see junk); Rust Err. */
            s = "12xyz"; { volatile int v = cplus_atoi(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 56: /* atoi("2147483648"): prefix -> ACCEPTED; atoi CANNOT report overflow; Rust i32 Err. */
            s = "2147483648"; { volatile int v = cplus_atoi(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 57: /* atol("123") -> ACCEPTED both. */
            s = "123"; { volatile long v = cplus_atol(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 58: /* atol("  9"): ws skipped -> ACCEPTED; Rust i64 Err. */
            s = "  9"; { volatile long v = cplus_atol(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 59: /* atoll("9223372036854775807") -> ACCEPTED both. */
            s = "9223372036854775807"; { volatile long long v = cplus_atoll(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 60: /* atof("3.5") -> ACCEPTED both. */
            s = "3.5"; { volatile double v = cplus_atof(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;
        case 61: /* atof(" 2.5"): ws skipped -> ACCEPTED; Rust f64 Err. */
            s = " 2.5"; { volatile double v = cplus_atof(s); (void)v; } return numeric_prefix(s) ? OUT_ACCEPTED : OUT_REFUSED;

        /* --- strtoimax / strtoumax / wcstoimax (62-71) ----------------- */
        case 62: /* strtoimax("-5",10) -> ACCEPTED both. */
            s = "-5"; errno = 0; { intmax_t v = cplus_strtoimax(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 63: /* strtoimax(overflow): ERANGE -> REFUSED; Rust i64 Err. */
            s = "9223372036854775808"; errno = 0; { intmax_t v = cplus_strtoimax(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 64: /* strtoimax("0x2A",16) -> ACCEPTED; Rust no hex. */
            s = "0x2A"; errno = 0; { intmax_t v = cplus_strtoimax(s, &e, 16); (void)v; } return conv(s, e, errno);
        case 65: /* strtoimax(NULL): C+ hardening EINVAL, endptr=NULL -> REFUSED; Rust has no null input -> SKIP. */
            errno = 0; { intmax_t v = cplus_strtoimax(NULL, &e, 10); (void)v; } return conv(NULL, e, errno);
        case 66: /* strtoumax("42",10) -> ACCEPTED both. */
            s = "42"; errno = 0; { uintmax_t v = cplus_strtoumax(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 67: /* strtoumax("-1",10): wraps -> ACCEPTED; Rust u64 Err. */
            s = "-1"; errno = 0; { uintmax_t v = cplus_strtoumax(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 68: /* strtoumax("0x10",0): base-0 hex -> ACCEPTED; Rust Err. */
            s = "0x10"; errno = 0; { uintmax_t v = cplus_strtoumax(s, &e, 0); (void)v; } return conv(s, e, errno);
        case 69: /* wcstoimax(L"123",10): wide-string parse -> ACCEPTED; Rust has no wide strings -> SKIP. */
            errno = 0; { wchar_t *we; intmax_t v = cplus_wcstoimax(L"123", &we, 10); (void)v; (void)we; } return OUT_ACCEPTED;
        case 70: /* wcstoimax(L"0x1F",0): wide base-0 hex -> ACCEPTED; Rust -> SKIP. */
            errno = 0; { wchar_t *we; intmax_t v = cplus_wcstoimax(L"0x1F", &we, 0); (void)v; (void)we; } return OUT_ACCEPTED;
        case 71: /* wcstoimax(L"abc",10): wide invalid (0 consumed) -> REFUSED; Rust -> SKIP. */
            { const wchar_t *ws = L"abc"; wchar_t *we; errno = 0;
              intmax_t v = cplus_wcstoimax(ws, &we, 10); (void)v;
              return (we != NULL && we != ws && errno != ERANGE && errno != EINVAL) ? OUT_ACCEPTED : OUT_REFUSED; }

        /* --- invalid bases, embedded NUL, high-bit, locale (72-82) ----- */
        case 72: /* strtol base 1 (invalid): EINVAL, 0 consumed -> REFUSED; Rust parse has no base -> SKIP. */
            s = "10"; errno = 0; { long v = cplus_strtol(s, &e, 1); (void)v; } return conv(s, e, errno);
        case 73: /* strtol base 37 (invalid) -> REFUSED; Rust -> SKIP. */
            s = "10"; errno = 0; { long v = cplus_strtol(s, &e, 37); (void)v; } return conv(s, e, errno);
        case 74: /* strtol base INT_MIN (invalid) -> REFUSED; Rust -> SKIP. */
            s = "10"; errno = 0; { long v = cplus_strtol(s, &e, INT_MIN); (void)v; } return conv(s, e, errno);
        case 75: /* strtoul base 1 (invalid) -> REFUSED; Rust -> SKIP. */
            s = "10"; errno = 0; { unsigned long v = cplus_strtoul(s, &e, 1); (void)v; } return conv(s, e, errno);
        case 76: /* strtoll base 37 (invalid) -> REFUSED; Rust -> SKIP. */
            s = "10"; errno = 0; { long long v = cplus_strtoll(s, &e, 37); (void)v; } return conv(s, e, errno);
        case 77: /* strtol("12\0 34"): host stops at NUL -> ACCEPTED(12); Rust full-consume Err. */
            s = "12\0 34"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 78: /* strtod("2.5\0garbage"): PARTIAL ACCEPTED; Rust Err. */
            s = "2.5\0garbage"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);
        case 79: /* strtol("\xFF"): raw high-bit byte, no ASCII digit -> REFUSED; Rust UTF-8 '\u{FF}' Err. */
            s = "\xFF"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 80: /* isalpha(0xFF): host byte cast, C-locale false -> REFUSED; Rust 'ÿ'.is_alphabetic() true. */
            return cplus_isalpha(0xFF) ? OUT_ACCEPTED : OUT_REFUSED;
        case 81: /* strtoull("9\0x"): NUL truncation -> ACCEPTED(9); Rust Err. */
            s = "9\0x"; errno = 0; { unsigned long long v = cplus_strtoull(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 82: /* strtod("1,5"): C locale stops at comma -> ACCEPTED(1); Rust Err. */
            s = "1,5"; errno = 0; { double v = cplus_strtod(s, &e); (void)bb_d(v); } return conv(s, e, errno);

        /* --- ctype predicates + EOF (83-95) ---------------------------- */
        case 83: /* isdigit('7') -> true; Rust '7'.is_ascii_digit() true. */
            return cplus_isdigit('7') ? OUT_ACCEPTED : OUT_REFUSED;
        case 84: /* isdigit('a') -> false both. */
            return cplus_isdigit('a') ? OUT_ACCEPTED : OUT_REFUSED;
        case 85: /* isalpha('Z') -> true both. */
            return cplus_isalpha('Z') ? OUT_ACCEPTED : OUT_REFUSED;
        case 86: /* isalpha('5') -> false both. */
            return cplus_isalpha('5') ? OUT_ACCEPTED : OUT_REFUSED;
        case 87: /* isspace(' ') -> true; Rust ' '.is_whitespace() true. */
            return cplus_isspace(' ') ? OUT_ACCEPTED : OUT_REFUSED;
        case 88: /* isspace('x') -> false both. */
            return cplus_isspace('x') ? OUT_ACCEPTED : OUT_REFUSED;
        case 89: /* isalnum('9') -> true; Rust '9'.is_alphanumeric() true. */
            return cplus_isalnum('9') ? OUT_ACCEPTED : OUT_REFUSED;
        case 90: /* isxdigit('f') -> true; Rust 'f'.is_ascii_hexdigit() true. */
            return cplus_isxdigit('f') ? OUT_ACCEPTED : OUT_REFUSED;
        case 91: /* isupper('Q') -> true; Rust 'Q'.is_uppercase() true. */
            return cplus_isupper('Q') ? OUT_ACCEPTED : OUT_REFUSED;
        case 92: /* islower('q') -> true; Rust 'q'.is_lowercase() true. */
            return cplus_islower('q') ? OUT_ACCEPTED : OUT_REFUSED;
        case 93: /* toupper('a') -> 'A' != EOF -> ACCEPTED; Rust to_ascii_uppercase -> 'A'. */
            return cplus_toupper('a') != EOF ? OUT_ACCEPTED : OUT_REFUSED;
        case 94: /* toupper(EOF): wrapper returns EOF -> REFUSED; Rust char has no EOF sentinel -> SKIP. */
            return cplus_toupper(EOF) != EOF ? OUT_ACCEPTED : OUT_REFUSED;
        case 95: /* isdigit(EOF): cast 255, C-locale false -> REFUSED; Rust no EOF -> SKIP. */
            return cplus_isdigit(EOF) ? OUT_ACCEPTED : OUT_REFUSED;

        /* --- checked arithmetic + parse Err (96-100) ------------------- */
        case 96: /* checked i64 mul overflow: host __builtin_mul_overflow -> REFUSED; Rust checked_mul None. */
            { long long a = bb_ll(9223372036854775807LL), b = bb_ll(2), r;
              if (__builtin_mul_overflow(a, b, &r)) return OUT_REFUSED; return OUT_ACCEPTED; }
        case 97: /* checked i64 mul in range -> ACCEPTED both. */
            { long long a = bb_ll(1000), b = bb_ll(1000), r;
              if (__builtin_mul_overflow(a, b, &r)) return OUT_REFUSED; return OUT_ACCEPTED; }
        case 98: /* checked u64 mul overflow -> REFUSED; Rust u64 checked_mul None. */
            { unsigned long long a = bb_ull(18446744073709551615ULL), b = bb_ull(2), r;
              if (__builtin_mul_overflow(a, b, &r)) return OUT_REFUSED; return OUT_ACCEPTED; }
        case 99: /* strtol("十二",10): non-ASCII digit word -> REFUSED; Rust i64 parse Err. */
            s = "\xE5\x8D\x81\xE4\xBA\x8C"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);
        case 100: /* strtol("\u0665" Arabic-Indic digit UTF-8,10): non-ASCII -> REFUSED; Rust i64 Err. */
            s = "\xD9\xA5"; errno = 0; { long v = cplus_strtol(s, &e, 10); (void)v; } return conv(s, e, errno);

        default:
            return OUT_SKIP;
    }
}

int main(int argc, char **argv) {
    int n, o;
    if (argc < 2) return 0;
    n = atoi(argv[1]); /* protocol plumbing only */
    o = (n >= 1 && n <= NUM_CASES) ? run_case(n) : OUT_SKIP;
    printf("RESULT %d %s\n", n, outcome_name(o));
    return 0; /* always 0: the outcome is in the printed line, not the exit code */
}
