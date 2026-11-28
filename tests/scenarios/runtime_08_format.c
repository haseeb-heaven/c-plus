/* runtime_08_format.c -- checked formatting/scanning runtime scenarios.
 *
 * Standalone C99. Covers cplus_snprintf_s / cplus_sprintf_s / cplus_fprintf_s /
 * cplus_printf_s and the scanf family plus the C+ unprefixed `_s` macros.
 *
 * Two deliberately unsafe cases are compile-time probes only and are NOT
 * executed here (they would be genuine UB if run):
 *   - `%d` supplied with a pointer instead of an int;
 *   - `%ls` supplied with a narrow char* instead of a wchar_t*.
 * The Clang `format` attributes on the wrappers reject both at compile time
 * (verified with -Werror=format in /tmp); the runtime tests therefore use
 * correctly typed arguments and a proper wide source.
 */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_format.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_stats.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

static int g_fail = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_fail;                                                          \
            fprintf(stderr, "runtime_08: line %d: %s\n", __LINE__, #cond);     \
        }                                                                      \
    } while (0)

/* ---- canary helpers ---------------------------------------------------- */
#define SC_PRE  8
#define SC_BODY 32
#define SC_SUF  8

typedef struct {
    unsigned char pre[SC_PRE];
    unsigned char body[SC_BODY];
    unsigned char suf[SC_SUF];
} sc_buf;

typedef struct {
    unsigned char pre[SC_PRE];
    unsigned char body[2100];
    unsigned char suf[SC_SUF];
} sc_bigbuf;

static unsigned char sc_pat(size_t i) {
    return (unsigned char)(0xA5u ^ ((i * 37u) & 0xFFu));
}
static void sc_fill(unsigned char *p, size_t n, size_t base) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = sc_pat(base + i);
}
static int sc_ok(const unsigned char *p, size_t n, size_t base) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != sc_pat(base + i)) return 0;
    return 1;
}
static void sc_canary(sc_buf *b) {
    sc_fill((unsigned char *)b, sizeof(*b), 0);
}
static int sc_guards(const sc_buf *b) {
    return sc_ok(b->pre, SC_PRE, 0) &&
           sc_ok(b->suf, SC_SUF, SC_PRE + SC_BODY);
}
static int sc_body_untouched_from(const sc_buf *b, size_t from) {
    return sc_ok(b->body + from, SC_BODY - from, SC_PRE + from);
}
static int sc_prefix(const unsigned char *p, const char *lit, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != (unsigned char)lit[i]) return 0;
    return 1;
}
static int sc_cstr(const unsigned char *p, const char *lit, size_t n) {
    return sc_prefix(p, lit, n) && p[n] == '\0';
}

/* Runtime-derived (non-constant) format string: __builtin_constant_p == 0. */
static const char *sc_runtime_fmt(void) {
    static char f[4];
    f[0] = '%'; f[1] = 'd'; f[2] = '\0';
    return f;
}

/* ------------------------- snprintf_s family ---------------------------- */

/* exact-fit output: NUL lands exactly in the last byte of dmax */
static void sc_08_snprintf_exact_fit(void) {
    sc_buf b;
    sc_canary(&b);
    errno = 0;
    CHECK(cplus_snprintf_s((char *)b.body, 3, "%d", 42) == 2);
    CHECK(errno == 0);
    CHECK(sc_cstr(b.body, "42", 2));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 3));

    sc_canary(&b);
    CHECK(snprintf_s((char *)b.body, SC_BODY, "%d", 7) == 1);
    CHECK(sc_cstr(b.body, "7", 1));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 2));
}

/* one-short truncation: would-be length returned, buffer NUL-terminated */
static void sc_08_snprintf_one_short_truncation(void) {
    sc_buf b;
    sc_canary(&b);
    errno = 0;
    CHECK(cplus_snprintf_s((char *)b.body, 6, "abcdef") == 6);
    CHECK(errno == 0);
    CHECK(sc_prefix(b.body, "abcde", 5));
    CHECK(b.body[5] == '\0');
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 6));
}

/* zero-size destination: C99 length probe, no byte written */
static void sc_08_snprintf_zero_size_dest(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, 0, "abc%d", 7) == 4);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));
}

/* NULL dest with dmax>0 refused; NULL dest with dmax==0 is a legal probe */
static void sc_08_snprintf_null_dest(void) {
    errno = 0;
    CHECK(cplus_snprintf_s(NULL, 8, "%d", 1) < 0);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(cplus_snprintf_s(NULL, 0, "xyz") == 3);
}

/* NULL format refused with EINVAL, destination untouched */
static void sc_08_snprintf_null_format(void) {
    sc_buf b;
    const char *nf = NULL;
    sc_canary(&b);
    errno = 0;
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY, nf, 1) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));
}

/* literal format with several correctly typed arguments */
static void sc_08_snprintf_literal_args(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY,
                           "%s/%d/%c/%.2f", "ab", -7, 'Z', 3.14159) == 12);
    CHECK(sc_cstr(b.body, "ab/-7/Z/3.14", 12));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 13));
}

/* %n in every length/positional form is refused and never writes */
static void sc_08_snprintf_percent_n_rejected(void) {
    const char *fmts[] = {"%n", "%hhn", "%lln", "%1$n", "before%n"};
    size_t i;
    int sink = -1;
    for (i = 0; i < sizeof(fmts) / sizeof(fmts[0]); ++i) {
        sc_buf b;
        sc_canary(&b);
        errno = 0;
        /* extra dummy arg keeps -Wformat-security quiet; the call is refused */
        CHECK(cplus_snprintf_s_checked(1, SC_BODY, (char *)b.body, SC_BODY,
                                       fmts[i], &sink) < 0);
        CHECK(errno == EINVAL);
        CHECK(sink == -1);
        CHECK(sc_guards(&b));
        CHECK(sc_body_untouched_from(&b, 0));
    }
    /* same refusal through the unprefixed macro with a correctly typed arg */
    {
        sc_buf b;
        sc_canary(&b);
        errno = 0;
        CHECK(snprintf_s((char *)b.body, SC_BODY, "%n", &sink) < 0);
        CHECK(errno == EINVAL);
        CHECK(sink == -1);
        CHECK(sc_guards(&b));
        CHECK(sc_body_untouched_from(&b, 0));
        errno = 0;
        CHECK(printf_s("%n", &sink) < 0);
        CHECK(errno == EINVAL);
        CHECK(sink == -1);
    }
}

/* non-literal format refused by the unprefixed macro and by explicit flag 0 */
static void sc_08_snprintf_nonliteral_rejected(void) {
    sc_buf b;
    sc_canary(&b);
    errno = 0;
    CHECK(snprintf_s((char *)b.body, SC_BODY, sc_runtime_fmt(), 42) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    sc_canary(&b);
    errno = 0;
    CHECK(cplus_snprintf_s_checked(0, SC_BODY, (char *)b.body, SC_BODY,
                                   "%d", 42) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));
}

/* huge width/precision are bounded: would-be length, no overwrite */
static void sc_08_snprintf_huge_width(void) {
    sc_bigbuf b;
    sc_fill((unsigned char *)&b, sizeof(b), 0);
    CHECK(cplus_snprintf_s((char *)b.body, 2100, "%2000d", 7) == 2000);
    CHECK(b.body[1999] == '7');
    CHECK(b.body[2000] == '\0');
    CHECK(sc_ok(b.pre, SC_PRE, 0));
    CHECK(sc_ok(b.suf, SC_SUF, SC_PRE + 2100));
    CHECK(sc_ok(b.body + 2001, 2100 - 2001, SC_PRE + 2001));

    sc_fill((unsigned char *)&b, sizeof(b), 0);
    CHECK(cplus_snprintf_s((char *)b.body, 2100, "%.1000d", 5) == 1000);
    CHECK(b.body[999] == '5');
    CHECK(b.body[1000] == '\0');
    CHECK(sc_ok(b.pre, SC_PRE, 0));
    CHECK(sc_ok(b.suf, SC_SUF, SC_PRE + 2100));
    CHECK(sc_ok(b.body + 1001, 2100 - 1001, SC_PRE + 1001));
}

/* a width literal wider than int is diagnosed, not expanded into output */
static void sc_08_snprintf_width_overflow(void) {
    sc_buf b;
    int r;
    sc_canary(&b);
    errno = 0;
    r = cplus_snprintf_s((char *)b.body, SC_BODY,
                         "%99999999999999999999d", 1);
    CHECK(r < 0);
    CHECK(errno == EOVERFLOW || errno == EINVAL);
    CHECK(sc_guards(&b));
    /* FINDING: glibc writes a single leading NUL before it detects the width
     * overflow, so bytes 1.. are still canary and the guards are intact. */
    CHECK(b.body[0] == '\0');
    CHECK(sc_body_untouched_from(&b, 1));
}

/* negative precision arguments mean "precision omitted" */
static void sc_08_snprintf_negative_precision(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY,
                           "%.*d|%.*d", -1, 42, -5, -17) == 6);
    CHECK(sc_cstr(b.body, "42|-17", 6));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 7));
}

/* POSIX positional arguments are accepted and ordered correctly */
static void sc_08_snprintf_positional_args(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY,
                           "%2$d/%1$d/%2$d", 10, 20) == 8);
    CHECK(sc_cstr(b.body, "20/10/20", 8));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 9));
}

/* %% is an escape, never a conversion (and must not trip the %n scanner) */
static void sc_08_snprintf_percent_escape(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY, "100%% done") == 9);
    CHECK(sc_cstr(b.body, "100% done", 9));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 10));
}

/* embedded NUL in the source string and in the format string */
static void sc_08_snprintf_embedded_nul(void) {
    const char src[] = {'A', 'B', '\0', 'C', 'D', '\0'};
    char fmt[] = {'O', 'K', '\0', '%', 'n', '\0'};
    int dummy = 0;
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY, "<%s>", src) == 4);
    CHECK(sc_cstr(b.body, "<AB>", 4));
    CHECK(sc_guards(&b));
    sc_canary(&b);
    /* checked with literal=1 but format held in a variable: the scanner must
     * stop at the embedded NUL and must not see the hidden %n. */
    errno = 0;
    CHECK(cplus_snprintf_s_checked(1, SC_BODY, (char *)b.body, SC_BODY,
                                   fmt, &dummy) == 2);
    CHECK(errno == 0);
    CHECK(sc_cstr(b.body, "OK", 2));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 3));
}

/* surplus variadic arguments are harmless for printf-family functions */
static void sc_08_snprintf_too_many_args(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s_checked(1, SC_BODY, (char *)b.body, SC_BODY,
                                   sc_runtime_fmt(), 7, 8, 9, 10) == 1);
    CHECK(sc_cstr(b.body, "7", 1));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 2));
}

/* formats with no conversions: snprintf_s and printf_s produce nothing extra */
static void sc_08_snprintf_no_args(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY, "plain text") == 10);
    CHECK(sc_cstr(b.body, "plain text", 10));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 11));
    CHECK(cplus_printf_s("") == 0);
}

/* -------------------------- sprintf_s family ---------------------------- */

/* sprintf_s exact fit and one-short truncation (bounded alias semantics) */
static void sc_08_sprintf_exact_and_trunc(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_sprintf_s((char *)b.body, 4, "%d", 123) == 3);
    CHECK(sc_cstr(b.body, "123", 3));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 4));

    sc_canary(&b);
    CHECK(sprintf_s((char *)b.body, 3, "%d", 12345) == 5);
    CHECK(sc_cstr(b.body, "12", 2));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 3));
}

/* sprintf_s refusals: %n, non-literal, oversized dmax, NULL fmt/dest */
static void sc_08_sprintf_refusals(void) {
    sc_buf b;
    const char *nf = NULL;
    int sink = -1;
    sc_canary(&b);
    errno = 0;
    CHECK(sprintf_s((char *)b.body, SC_BODY, "%n", &sink) < 0);
    CHECK(errno == EINVAL && sink == -1);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    sc_canary(&b);
    errno = 0;
    CHECK(sprintf_s((char *)b.body, SC_BODY, sc_runtime_fmt(), 1) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    sc_canary(&b);
    errno = 0;
    CHECK(cplus_sprintf_s_checked(1, SC_BODY, (char *)b.body,
                                  SC_BODY + 1, "%d", 1) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    sc_canary(&b);
    errno = 0;
    CHECK(cplus_sprintf_s((char *)b.body, SC_BODY, nf, 1) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    errno = 0;
    CHECK(cplus_sprintf_s(NULL, SC_BODY, "%d", 1) < 0);
    CHECK(errno == EINVAL);
}

/* -------------------------- fprintf_s family ---------------------------- */

/* literal fprintf_s plus NULL stream / NULL format refusals */
static void sc_08_fprintf_literal_and_null_stream(void) {
    FILE *f = tmpfile();
    char s[8] = {0};
    int v = 0;
    const char *nf = NULL;
    CHECK(f != NULL);
    CHECK(cplus_fprintf_s(f, "%s %d", "hi", 3) == 4);
    rewind(f);
    CHECK(cplus_fscanf_s(f, "%7s %d", s, &v) == 2);
    CHECK(s[0] == 'h' && s[1] == 'i' && s[2] == '\0' && v == 3);

    errno = 0;
    CHECK(cplus_fprintf_s(f, nf, 1) < 0);
    CHECK(errno == EINVAL);
    fclose(f);

    errno = 0;
    CHECK(cplus_fprintf_s(NULL, "%d", 1) < 0);
    CHECK(errno == EINVAL);
}

/* fprintf_s refuses %n and non-literal formats without writing a byte */
static void sc_08_fprintf_refusals(void) {
    FILE *f = tmpfile();
    int sink = -1;
    CHECK(f != NULL);
    errno = 0;
    CHECK(fprintf_s(f, "%n", &sink) < 0);
    CHECK(errno == EINVAL && sink == -1);
    CHECK(ftell(f) == 0);

    errno = 0;
    CHECK(fprintf_s(f, sc_runtime_fmt(), 42) < 0);
    CHECK(errno == EINVAL);
    CHECK(ftell(f) == 0);

    errno = 0;
    CHECK(cplus_fprintf_s_checked(0, f, "%d", 7) < 0);
    CHECK(errno == EINVAL);
    CHECK(ftell(f) == 0);
    fclose(f);
}

/* -------------------------- scanf_s family ------------------------------ */

/* bounded conversions succeed with correct counts and values */
static void sc_08_sscanf_bounded(void) {
    int a = 0, b = 0;
    char s[8] = {0};
    CHECK(cplus_sscanf_s("12 34", "%d %d", &a, &b) == 2);
    CHECK(a == 12 && b == 34);
    CHECK(cplus_sscanf_s("hello world", "%7s", s) == 1);
    CHECK(s[0] == 'h' && s[4] == 'o' && s[5] == '\0');
}

/* unbounded %s is refused; destination canary survives the refusal */
static void sc_08_sscanf_unbounded_s_rejected(void) {
    sc_buf b;
    sc_canary(&b);
    errno = 0;
    CHECK(sscanf_s("abc", "%s", (char *)b.body) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));
}

/* bounded scansets and assignment suppression are allowed; unbounded %[ is not */
static void sc_08_sscanf_scanset_and_suppress(void) {
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_sscanf_s("abc123", "%3[a-z]", (char *)b.body) == 1);
    CHECK(sc_cstr(b.body, "abc", 3));
    CHECK(sc_guards(&b));

    sc_canary(&b);
    errno = 0;
    CHECK(cplus_sscanf_s("abc", "%[a-z]", (char *)b.body) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    sc_canary(&b);
    CHECK(cplus_sscanf_s("one two", "%*s %3s", (char *)b.body) == 1);
    CHECK(sc_cstr(b.body, "two", 3));
    CHECK(sc_guards(&b));
}

/* scanf %n and non-literal formats are refused; sink never written */
static void sc_08_sscanf_refusals(void) {
    int sink = -1;
    sc_buf b;
    sc_canary(&b);
    errno = 0;
    CHECK(cplus_sscanf_s("abc", "%n", &sink) < 0);
    CHECK(errno == EINVAL && sink == -1);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    sc_canary(&b);
    errno = 0;
    CHECK(sscanf_s("42", sc_runtime_fmt(), &sink) < 0);
    CHECK(errno == EINVAL && sink == -1);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    sc_canary(&b);
    errno = 0;
    CHECK(cplus_sscanf_s_checked(0, "42", "%d", &sink) < 0);
    CHECK(errno == EINVAL && sink == -1);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));
}

/* NULL input / format rejected for sscanf_s */
static void sc_08_sscanf_null_args(void) {
    int v = 0;
    const char *nf = NULL;
    errno = 0;
    CHECK(cplus_sscanf_s(NULL, "%d", &v) < 0);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(cplus_sscanf_s("1", nf, &v) < 0);
    CHECK(errno == EINVAL);
}

/* fscanf_s success plus scanf_s refusals (stdin is never consumed) */
static void sc_08_fscanf_and_scanf_rejections(void) {
    FILE *f = tmpfile();
    int v = 0, sink = -1;
    char s[8] = {0};
    sc_buf b;
    CHECK(f != NULL);
    CHECK(cplus_fprintf_s(f, "42 hello\n") == 9);
    rewind(f);
    CHECK(cplus_fscanf_s(f, "%d %7s", &v, s) == 2);
    CHECK(v == 42 && s[0] == 'h' && s[4] == 'o' && s[5] == '\0');
    fclose(f);

    sc_canary(&b);
    errno = 0;
    CHECK(scanf_s("%s", (char *)b.body) < 0);
    CHECK(errno == EINVAL);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    errno = 0;
    CHECK(scanf_s("%n", &sink) < 0);
    CHECK(errno == EINVAL && sink == -1);

    errno = 0;
    CHECK(scanf_s(sc_runtime_fmt(), &sink) < 0);
    CHECK(errno == EINVAL && sink == -1);
}

/* --------------------------- va_list helpers ---------------------------- */

static int sc_vsnprintf_helper(char *d, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsnprintf_s(d, n, fmt, ap);
    va_end(ap);
    return r;
}
static int sc_vsprintf_helper(char *d, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsprintf_s(d, n, fmt, ap);
    va_end(ap);
    return r;
}

/* v-variants format correctly and still refuse %n and overlong output */
static void sc_08_vformat_variants(void) {
    sc_buf b;
    int sink = -1;
    sc_canary(&b);
    CHECK(sc_vsnprintf_helper((char *)b.body, SC_BODY, "%s=%d", "x", 9) == 3);
    CHECK(sc_cstr(b.body, "x=9", 3));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 4));

    sc_canary(&b);
    CHECK(sc_vsprintf_helper((char *)b.body, SC_BODY, "%d-%d", 1, 2) == 3);
    CHECK(sc_cstr(b.body, "1-2", 3));
    CHECK(sc_guards(&b));

    sc_canary(&b);
    errno = 0;
    CHECK(sc_vsnprintf_helper((char *)b.body, SC_BODY, "%n", &sink) < 0);
    CHECK(errno == EINVAL && sink == -1);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    sc_canary(&b);
    CHECK(sc_vsnprintf_helper((char *)b.body, 4, "abcdef") == 6);
    CHECK(sc_cstr(b.body, "abc", 3));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 4));
}

/* %ls needs a genuine wide source; a narrow source is a compile-time error */
static void sc_08_wide_ls(void) {
    const wchar_t w[] = {L'W', L'i', L'd', L'e', 0};
    const wchar_t w2[] = {L'A', 0, L'B', 0};
    sc_buf b;
    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY, "[%ls]", w) == 6);
    CHECK(sc_cstr(b.body, "[Wide]", 6));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 7));

    sc_canary(&b);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY, "<%ls>", w2) == 3);
    CHECK(sc_cstr(b.body, "<A>", 3));
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 4));
}

/* success vs refusal are reflected one-for-one in telemetry, and log level roundtrips */
static void sc_08_logging_parity(void) {
    cplus_safe_stat before, after;
    cplus_safe_log_level old = cplus_safe_log_get_level();
    sc_buf b;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    CHECK(cplus_safe_log_get_level() == CPLUS_SAFE_LOG_TRACE);

    sc_canary(&b);
    CHECK(cplus_safe_stats_get("snprintf_s", &before) == 1);
    CHECK(cplus_snprintf_s((char *)b.body, SC_BODY, "%d", 7) == 1);
    CHECK(cplus_safe_stats_get("snprintf_s", &after) == 1);
    CHECK(after.calls == before.calls + 1);
    CHECK(after.ok == before.ok + 1);
    CHECK(after.fail == before.fail);
    CHECK(sc_guards(&b));

    sc_canary(&b);
    CHECK(cplus_safe_stats_get("snprintf_s", &before) == 1);
    errno = 0;
    CHECK(cplus_snprintf_s_checked(0, SC_BODY, (char *)b.body, SC_BODY,
                                   "%d", 7) < 0);
    CHECK(errno == EINVAL);
    CHECK(cplus_safe_stats_get("snprintf_s", &after) == 1);
    CHECK(after.calls == before.calls + 1);
    CHECK(after.ok == before.ok);
    CHECK(after.fail == before.fail + 1);
    CHECK(sc_guards(&b));
    CHECK(sc_body_untouched_from(&b, 0));

    cplus_safe_log_set_level(old);
    CHECK(cplus_safe_log_get_level() == old);
}

/* repeated calls stay consistent; refusals never leak a byte past the canary */
static void sc_08_repeated_calls(void) {
    int i;
    char ref[SC_BODY];
    for (i = 0; i < 200; ++i) {
        sc_buf b;
        int want = snprintf(ref, sizeof(ref), "i=%d", i);
        sc_canary(&b);
        CHECK(cplus_snprintf_s((char *)b.body, SC_BODY, "i=%d", i) == want);
        CHECK(sc_prefix(b.body, ref, (size_t)want));
        CHECK(b.body[want] == '\0');
        CHECK(sc_guards(&b));
        CHECK(sc_body_untouched_from(&b, (size_t)want + 1));
    }
    for (i = 0; i < 50; ++i) {
        sc_buf b;
        sc_canary(&b);
        errno = 0;
        CHECK(cplus_snprintf_s_checked(0, SC_BODY, (char *)b.body, SC_BODY,
                                       "%d", i) < 0);
        CHECK(errno == EINVAL);
        CHECK(sc_guards(&b));
        CHECK(sc_body_untouched_from(&b, 0));
    }
}

/* ------------------------------- driver -------------------------------- */

typedef void (*sc_fn)(void);
static const sc_fn sc_tests[] = {
    sc_08_snprintf_exact_fit,
    sc_08_snprintf_one_short_truncation,
    sc_08_snprintf_zero_size_dest,
    sc_08_snprintf_null_dest,
    sc_08_snprintf_null_format,
    sc_08_snprintf_literal_args,
    sc_08_snprintf_percent_n_rejected,
    sc_08_snprintf_nonliteral_rejected,
    sc_08_snprintf_huge_width,
    sc_08_snprintf_width_overflow,
    sc_08_snprintf_negative_precision,
    sc_08_snprintf_positional_args,
    sc_08_snprintf_percent_escape,
    sc_08_snprintf_embedded_nul,
    sc_08_snprintf_too_many_args,
    sc_08_snprintf_no_args,
    sc_08_sprintf_exact_and_trunc,
    sc_08_sprintf_refusals,
    sc_08_fprintf_literal_and_null_stream,
    sc_08_fprintf_refusals,
    sc_08_sscanf_bounded,
    sc_08_sscanf_unbounded_s_rejected,
    sc_08_sscanf_scanset_and_suppress,
    sc_08_sscanf_refusals,
    sc_08_sscanf_null_args,
    sc_08_fscanf_and_scanf_rejections,
    sc_08_vformat_variants,
    sc_08_wide_ls,
    sc_08_logging_parity,
    sc_08_repeated_calls
};
#define SC_N ((int)(sizeof(sc_tests) / sizeof(sc_tests[0])))

int main(void) {
    int i;
    /* Required: inspect refusal return values instead of exiting on violation. */
    cplus_runtime_set_violation_trap(0);
    cplus_runtime_set_failstop(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    for (i = 0; i < SC_N; ++i) sc_tests[i]();
    if (g_fail != 0) {
        printf("runtime_format: FAILED (%d checks)\n", g_fail);
        return 1;
    }
    printf("runtime_format: %d scenarios ok\n", SC_N);
    return 0;
}
