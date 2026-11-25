/*
 * methods_format.c -- per-method unit + logging tests for the formatting APIs.
 *
 * Standalone C99. main() always returns 0 and prints one summary line:
 *   methods_format: <passed>/<total> checks ok
 * Every failed CHECK also emits a "FINDING line N: ..." record.
 *
 * TWO LAYERS, HONESTLY SEPARATED
 * ------------------------------
 *   default : SafeCRT headers (safe_c.h / safe_format.h / safe_runtime.h /
 *             safe_log.h / safe_stats.h). Both the narrow hardened-libc
 *             cplus_* entry points and the checked cplus_*_s entry points are
 *             exercised. Checked violations are inspected (not trapped) via
 *             cplus_runtime_set_violation_trap(0).
 *   -DPLAIN_LIBC : only "cplus_libc_safe.h" is included. The checked cplus_*_s
 *             methods are NOT available there, so they are skipped and a note
 *             is printed; checks are counted only for methods actually run.
 *
 * SCENARIOS (applied across the methods): normal formatting/parsing, exact fit,
 * truncation, zero-size destination, NULL format/destination/stream, literal vs
 * non-literal format, %n rejection, huge width/precision, va_list v-variants,
 * destination canary integrity, and logging OFF/ON result parity. Every body is
 * executed twice -- once with logging OFF and once with logging ON -- and the
 * two result signatures must be byte-for-byte equal.
 *
 * NO GENUINE UB: wrong-type arguments are NEVER executed. They are documented
 * and tested only as a compile-time probe (see note at the end of this file).
 * Unbounded %s is only ever exercised with short input into a large canary
 * buffer. %n is used only with a real int* (standard C writes the count for the
 * narrow layer; the checked layer refuses it before calling libc).
 *
 * Logging is metadata-only in both layers, so toggling it must not change any
 * return value, errno, output byte, or canary byte. Diagnostics are sunk to
 * /dev/null while bodies run (POSIX) and findings are replayed afterwards.
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#define CHECKED_AVAILABLE 0
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_format.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_stats.h"
#define CHECKED_AVAILABLE 1
#endif

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* check / finding accounting                                          */
/* ------------------------------------------------------------------ */
static int g_total = 0;
static int g_fail = 0;
#define FIND_MAX 96
#define FIND_LEN 256
static char g_findings[FIND_MAX][FIND_LEN];
static int g_nfind = 0;

static void add_finding(int line, const char *expr)
{
    if (g_nfind < FIND_MAX) {
        (void)snprintf(g_findings[g_nfind], FIND_LEN,
                       "FINDING line %d: CHECK(%s) failed", line, expr);
        ++g_nfind;
    }
}
#define CHECK(x)                                                               \
    do {                                                                       \
        ++g_total;                                                             \
        if (!(x)) {                                                            \
            ++g_fail;                                                          \
            add_finding(__LINE__, #x);                                         \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------ */
/* result signature used for logging OFF/ON parity                     */
/* ------------------------------------------------------------------ */
#define SIG_R 8
typedef struct {
    int r[SIG_R];
    int er[SIG_R];
    unsigned char h[2];
    int guards;
    int sink;
    int extra[4];
} Sig;

static void sig_zero(Sig *s)
{
    memset(s, 0, sizeof *s);
    s->guards = 1;
    s->sink = -31415;
}
static int sig_equal(const Sig *a, const Sig *b)
{
    int i;
    for (i = 0; i < SIG_R; ++i)
        if (a->r[i] != b->r[i] || a->er[i] != b->er[i]) return 0;
    for (i = 0; i < 2; ++i)
        if (a->h[i] != b->h[i]) return 0;
    for (i = 0; i < 4; ++i)
        if (a->extra[i] != b->extra[i]) return 0;
    return a->guards == b->guards && a->sink == b->sink;
}

static void logging_set(int on)
{
#ifdef PLAIN_LIBC
    cplus_libc_log_set_enabled(on);
#else
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
    cplus_libc_log_set_enabled(on);
#endif
}

typedef void (*body_fn)(Sig *);
static void run_parity(body_fn fn)
{
    Sig off, on;
    sig_zero(&off);
    sig_zero(&on);
    logging_set(0);
    fn(&off);
#if CHECKED_AVAILABLE
    CHECK(cplus_safe_log_get_level() == CPLUS_SAFE_LOG_OFF);
#endif
    logging_set(1);
    fn(&on);
#if CHECKED_AVAILABLE
    CHECK(cplus_safe_log_get_level() == CPLUS_SAFE_LOG_TRACE);
#endif
    logging_set(0);
    CHECK(sig_equal(&off, &on));
}

/* ------------------------------------------------------------------ */
/* canary buffers                                                      */
/* ------------------------------------------------------------------ */
#define CB_PRE 8
#define CB_BODY 32
#define CB_SUF 8
#define CB_BIG 2100
typedef struct {
    unsigned char pre[CB_PRE];
    unsigned char body[CB_BODY];
    unsigned char suf[CB_SUF];
} cbuf;
typedef struct {
    unsigned char pre[CB_PRE];
    unsigned char body[CB_BIG];
    unsigned char suf[CB_SUF];
} cbigbuf;

static unsigned char cb_pat(size_t i)
{
    return (unsigned char)(0xA5u ^ ((i * 37u) & 0xFFu));
}
static void cb_fill(unsigned char *p, size_t n, size_t base)
{
    size_t i;
    for (i = 0; i < n; ++i) p[i] = cb_pat(base + i);
}
static int cb_ok(const unsigned char *p, size_t n, size_t base)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != cb_pat(base + i)) return 0;
    return 1;
}
static void cb_init(cbuf *b)
{
    cb_fill((unsigned char *)b, sizeof *b, 0);
}
static void cb_big_init(cbigbuf *b)
{
    cb_fill((unsigned char *)b, sizeof *b, 0);
}
static int cb_guards(const cbuf *b)
{
    return cb_ok(b->pre, CB_PRE, 0) &&
           cb_ok(b->suf, CB_SUF, CB_PRE + CB_BODY);
}
static int cb_big_guards(const cbigbuf *b)
{
    return cb_ok(b->pre, CB_PRE, 0) &&
           cb_ok(b->suf, CB_SUF, CB_PRE + CB_BIG);
}
static int cb_untouched(const cbuf *b, size_t from)
{
    return from >= CB_BODY || cb_ok(b->body + from, CB_BODY - from, CB_PRE + from);
}
static int cb_prefix(const unsigned char *p, const char *lit, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != (unsigned char)lit[i]) return 0;
    return 1;
}
static int cb_cstr(const unsigned char *p, const char *lit, size_t n)
{
    return cb_prefix(p, lit, n) && p[n] == '\0';
}
static unsigned char cb_hash(const unsigned char *p, size_t n)
{
    unsigned char h = 0;
    size_t i;
    for (i = 0; i < n; ++i) h = (unsigned char)(h * 31u + p[i]);
    return h;
}

/* Runtime-derived (non-constant) format: __builtin_constant_p == 0. */
static const char *rt_fmt_d(void)
{
    static char f[4];
    f[0] = '%';
    f[1] = 'd';
    f[2] = '\0';
    return f;
}

/* ------------------------------------------------------------------ */
/* POSIX stream redirection helpers                                    */
/* ------------------------------------------------------------------ */
#ifndef _WIN32
static FILE *g_outcap = NULL;
static int g_saved_out = -1;
static int outcap_begin(void)
{
    fflush(stdout);
    g_outcap = tmpfile();
    if (!g_outcap) return -1;
    g_saved_out = dup(fileno(stdout));
    if (g_saved_out < 0 || dup2(fileno(g_outcap), fileno(stdout)) < 0) {
        if (g_saved_out >= 0) {
            (void)dup2(g_saved_out, fileno(stdout));
            close(g_saved_out);
            g_saved_out = -1;
        }
        fclose(g_outcap);
        g_outcap = NULL;
        return -1;
    }
    return 0;
}
static size_t outcap_end(char *buf, size_t cap)
{
    size_t n = 0;
    fflush(stdout);
    if (g_saved_out >= 0) {
        (void)dup2(g_saved_out, fileno(stdout));
        close(g_saved_out);
        g_saved_out = -1;
    }
    if (g_outcap) {
        rewind(g_outcap);
        if (cap) {
            n = fread(buf, 1, cap - 1, g_outcap);
            buf[n] = '\0';
        }
        fclose(g_outcap);
        g_outcap = NULL;
    }
    return n;
}
static FILE *g_incap = NULL;
static int g_saved_in = -1;
static int incap_begin(const char *data)
{
    g_incap = tmpfile();
    if (!g_incap) return -1;
    if (fputs(data, g_incap) == EOF) {
        fclose(g_incap);
        g_incap = NULL;
        return -1;
    }
    rewind(g_incap);
    g_saved_in = dup(fileno(stdin));
    if (g_saved_in < 0 || dup2(fileno(g_incap), fileno(stdin)) < 0) {
        if (g_saved_in >= 0) {
            (void)dup2(g_saved_in, fileno(stdin));
            close(g_saved_in);
            g_saved_in = -1;
        }
        fclose(g_incap);
        g_incap = NULL;
        return -1;
    }
    return 0;
}
static void incap_end(void)
{
    if (g_saved_in >= 0) {
        (void)dup2(g_saved_in, fileno(stdin));
        close(g_saved_in);
        g_saved_in = -1;
    }
    if (g_incap) {
        fclose(g_incap);
        g_incap = NULL;
    }
}
static int g_saved_err = -1;
static void err_sink_begin(void)
{
    FILE *nul;
    fflush(stderr);
    g_saved_err = dup(fileno(stderr));
    if (g_saved_err < 0) return;
    nul = fopen("/dev/null", "w");
    if (nul) {
        (void)dup2(fileno(nul), fileno(stderr));
        fclose(nul);
    }
}
static void err_sink_end(void)
{
    fflush(stderr);
    if (g_saved_err >= 0) {
        (void)dup2(g_saved_err, fileno(stderr));
        close(g_saved_err);
        g_saved_err = -1;
    }
}
#endif /* !_WIN32 */

/* ================================================================== */
/* METHOD 1 -- cplus_printf / cplus_fprintf (narrow)                    */
/* ================================================================== */
static void b_printf_literal(Sig *s)
{
    char cap[64];
    size_t n = 0;
    int capok = 0;
    const char *nf = NULL;
    cap[0] = '\0';
#ifndef _WIN32
    capok = (outcap_begin() == 0);
#endif
    errno = 0;
    s->r[0] = cplus_printf("<%d>", 42);
    s->er[0] = errno;
    s->r[1] = cplus_printf("");
    s->er[1] = errno;
#ifndef _WIN32
    if (capok) n = outcap_end(cap, sizeof cap);
#endif
    CHECK(s->r[0] == 4 && s->er[0] == 0);
    CHECK(s->r[1] == 0);
#ifndef _WIN32
    CHECK(n == 4 && memcmp(cap, "<42>", 4) == 0);
#endif
    s->h[0] = cb_hash((const unsigned char *)cap, sizeof cap);
    errno = 0;
    /* dummy arg documents "no conversions expected"; the NULL fmt is refused
     * before libc is reached, so the extra variadic value is never read. */
    s->r[2] = cplus_printf(nf, 0);
    s->er[2] = errno;
    CHECK(s->r[2] == -1 && s->er[2] == EINVAL);
}
static void b_fprintf_stream(Sig *s)
{
    FILE *f = tmpfile();
    char rd[32];
    size_t n;
    const char *nf = NULL;
    CHECK(f != NULL);
    if (!f) return;
    errno = 0;
    s->r[0] = cplus_fprintf(f, "%s:%d", "hi", 3);
    s->er[0] = errno;
    CHECK(s->r[0] == 4 && s->er[0] == 0); /* "hi:3" */
    rewind(f);
    n = fread(rd, 1, sizeof rd - 1, f);
    rd[n] = '\0';
    CHECK(n == 4 && memcmp(rd, "hi:3", 4) == 0);
    s->h[0] = cb_hash((const unsigned char *)rd, n);
    errno = 0;
    s->r[1] = cplus_fprintf(f, nf, 1);
    s->er[1] = errno;
    CHECK(s->r[1] == -1 && s->er[1] == EINVAL);
    errno = 0;
    s->r[2] = cplus_fprintf(NULL, "%d", 1);
    s->er[2] = errno;
    CHECK(s->r[2] == -1 && s->er[2] == EINVAL);
    /* %n is defined for the narrow layer: a valid int* receives the count. */
    s->sink = -1;
    s->r[3] = cplus_fprintf(f, "ab%n", &s->sink);
    CHECK(s->r[3] == 2 && s->sink == 2);
    s->extra[0] = (int)ftell(f);
    fclose(f);
}
/* METHOD: cplus_printf / cplus_fprintf (narrow hardened libc) */
static void m_fmt_01_printf_fprintf(void)
{
    run_parity(b_printf_literal);
    run_parity(b_fprintf_stream);
}

/* ================================================================== */
/* METHOD 2 -- cplus_snprintf / cplus_vsnprintf (narrow)                */
/* ================================================================== */
static int v_snprintf_helper(char *d, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsnprintf(d, n, fmt, ap);
    va_end(ap);
    return r;
}
static void b_snprintf_normal(Sig *s)
{
    cbuf b;
    cb_init(&b);
    errno = 0;
    s->r[0] = cplus_snprintf((char *)b.body, CB_BODY, "%s/%d/%.2f",
                              "ab", -7, 3.14159);
    s->er[0] = errno;
    CHECK(s->r[0] == 10 && s->er[0] == 0);
    CHECK(cb_cstr(b.body, "ab/-7/3.14", 10));
    CHECK(cb_guards(&b) && cb_untouched(&b, 11));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_snprintf_bounds(Sig *s)
{
    cbuf b;
    const char *nf = NULL;
    const char *dyn = rt_fmt_d();
    cb_init(&b);
    s->r[0] = cplus_snprintf((char *)b.body, 3, "%d", 42);
    CHECK(s->r[0] == 2 && cb_cstr(b.body, "42", 2));
    CHECK(cb_guards(&b) && cb_untouched(&b, 3));
    cb_init(&b);
    s->r[1] = cplus_snprintf((char *)b.body, 6, "abcdef");
    CHECK(s->r[1] == 6 && cb_prefix(b.body, "abcde", 5) && b.body[5] == '\0');
    CHECK(cb_guards(&b) && cb_untouched(&b, 6));
    errno = 0;
    s->r[2] = cplus_snprintf(NULL, 0, "abc%d", 7);
    s->er[2] = errno;
    CHECK(s->r[2] == 4);
    errno = 0;
    s->r[3] = cplus_snprintf(NULL, 8, "%d", 1);
    s->er[3] = errno;
    CHECK(s->r[3] == -1 && s->er[3] == EINVAL);
    errno = 0;
    s->r[4] = cplus_snprintf((char *)b.body, CB_BODY, nf, 1);
    s->er[4] = errno;
    CHECK(s->r[4] == -1 && s->er[4] == EINVAL);
    /* narrow layer accepts a runtime-derived format with correct arguments */
    cb_init(&b);
    s->r[5] = cplus_snprintf((char *)b.body, CB_BODY, dyn, 9);
    CHECK(s->r[5] == 1 && cb_cstr(b.body, "9", 1));
    CHECK(cb_guards(&b));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_snprintf_huge_and_n(Sig *s)
{
    cbigbuf big;
    cbuf b;
    cb_big_init(&big);
    s->r[0] = cplus_snprintf((char *)big.body, CB_BIG, "%2000d", 7);
    CHECK(s->r[0] == 2000);
    CHECK(big.body[1999] == '7' && big.body[2000] == '\0');
    CHECK(cb_big_guards(&big));
    cb_big_init(&big);
    s->r[1] = cplus_snprintf((char *)big.body, CB_BIG, "%.1000d", 5);
    CHECK(s->r[1] == 1000 && big.body[999] == '5' && big.body[1000] == '\0');
    CHECK(cb_big_guards(&big));
    s->h[0] = cb_hash(big.body, CB_BIG);
    /* narrow %n writes through a valid int* (defined C behaviour) */
    cb_init(&b);
    s->sink = -1;
    s->r[2] = cplus_snprintf((char *)b.body, CB_BODY, "ab%n", &s->sink);
    CHECK(s->r[2] == 2 && s->sink == 2);
    CHECK(cb_guards(&b));
    s->h[1] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_vsnprintf(Sig *s)
{
    cbuf b;
    cb_init(&b);
    s->r[0] = v_snprintf_helper((char *)b.body, CB_BODY, "%s=%d", "x", 9);
    CHECK(s->r[0] == 3 && cb_cstr(b.body, "x=9", 3));
    CHECK(cb_guards(&b) && cb_untouched(&b, 4));
    cb_init(&b);
    s->r[1] = v_snprintf_helper((char *)b.body, 4, "abcdef");
    CHECK(s->r[1] == 6 && cb_cstr(b.body, "abc", 3));
    CHECK(cb_guards(&b) && cb_untouched(&b, 4));
    cb_init(&b);
    s->sink = -1;
    s->r[2] = v_snprintf_helper((char *)b.body, CB_BODY, "xy%n", &s->sink);
    CHECK(s->r[2] == 2 && s->sink == 2);
    CHECK(cb_guards(&b));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
/* METHOD: cplus_snprintf / cplus_vsnprintf (narrow hardened libc) */
static void m_fmt_02_snprintf_vsnprintf(void)
{
    run_parity(b_snprintf_normal);
    run_parity(b_snprintf_bounds);
    run_parity(b_snprintf_huge_and_n);
    run_parity(b_vsnprintf);
}

/* ================================================================== */
/* METHOD 3 -- cplus_sprintf / cplus_vsprintf (narrow)                  */
/* ================================================================== */
static int v_sprintf_helper(char *d, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsprintf(d, fmt, ap);
    va_end(ap);
    return r;
}
static void b_sprintf_normal(Sig *s)
{
    cbuf b;
    cb_init(&b);
    s->r[0] = cplus_sprintf((char *)b.body, "%d-%s", 12, "xy");
    CHECK(s->r[0] == 5 && cb_cstr(b.body, "12-xy", 5));
    CHECK(cb_guards(&b) && cb_untouched(&b, 6));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_sprintf_null_v(Sig *s)
{
    cbuf b;
    const char *nf = NULL;
    errno = 0;
    s->r[0] = cplus_sprintf(NULL, "%d", 1);
    s->er[0] = errno;
    CHECK(s->r[0] == -1 && s->er[0] == EINVAL);
    errno = 0;
    s->r[1] = cplus_sprintf((char *)b.body, nf, 1);
    s->er[1] = errno;
    CHECK(s->r[1] == -1 && s->er[1] == EINVAL);
    cb_init(&b);
    s->r[2] = v_sprintf_helper((char *)b.body, "%d/%d", 3, 4);
    CHECK(s->r[2] == 3 && cb_cstr(b.body, "3/4", 3));
    CHECK(cb_guards(&b) && cb_untouched(&b, 4));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
/* METHOD: cplus_sprintf / cplus_vsprintf (narrow hardened libc) */
static void m_fmt_03_sprintf_vsprintf(void)
{
    run_parity(b_sprintf_normal);
    run_parity(b_sprintf_null_v);
}

/* ================================================================== */
/* METHOD 4 -- cplus_scanf / cplus_fscanf / cplus_sscanf (narrow)       */
/* ================================================================== */
static void b_sscanf_normal(Sig *s)
{
    int a = 0, b = 0;
    char t[8] = {0};
    const char *nf = NULL;
    cbuf guard;
    s->r[0] = cplus_sscanf("12 34", "%d %d", &a, &b);
    CHECK(s->r[0] == 2 && a == 12 && b == 34);
    s->extra[0] = a;
    s->extra[1] = b;
    s->r[1] = cplus_sscanf("hello world", "%7s", t);
    CHECK(s->r[1] == 1 && memcmp(t, "hello", 5) == 0 && t[5] == '\0');
    s->h[0] = cb_hash((const unsigned char *)t, sizeof t);
    errno = 0;
    s->r[2] = cplus_sscanf(NULL, "%d", &a);
    s->er[2] = errno;
    CHECK(s->r[2] == EOF && s->er[2] == EINVAL);
    errno = 0;
    s->r[3] = cplus_sscanf("1", nf, &a);
    s->er[3] = errno;
    CHECK(s->r[3] == EOF && s->er[3] == EINVAL);
    /* narrow layer has no width enforcement: short input, large canary */
    cb_init(&guard);
    s->r[4] = cplus_sscanf("abc", "%31s", (char *)guard.body);
    CHECK(s->r[4] == 1 && cb_guards(&guard));
    s->guards = cb_guards(&guard);
    s->h[1] = cb_hash(guard.body, CB_BODY);
}
static void b_fscanf_normal(Sig *s)
{
    FILE *f = tmpfile();
    int v = 0;
    char t[8] = {0};
    CHECK(f != NULL);
    if (!f) return;
    CHECK(fputs("42 hello\n", f) >= 0);
    rewind(f);
    s->r[0] = cplus_fscanf(f, "%d %7s", &v, t);
    CHECK(s->r[0] == 2 && v == 42 && memcmp(t, "hello", 5) == 0);
    s->extra[0] = v;
    s->h[0] = cb_hash((const unsigned char *)t, sizeof t);
    errno = 0;
    s->r[1] = cplus_fscanf(NULL, "%d", &v);
    s->er[1] = errno;
    CHECK(s->r[1] == EOF && s->er[1] == EINVAL);
    fclose(f);
}
static void b_scanf_stdin(Sig *s)
{
    int a = 0, b = 0;
    const char *nf = NULL;
#ifndef _WIN32
    if (incap_begin("7 8\n") == 0) {
#endif
        errno = 0;
        s->r[0] = cplus_scanf("%d %d", &a, &b);
        s->er[0] = errno;
        CHECK(s->r[0] == 2 && a == 7 && b == 8);
        s->extra[0] = a;
        s->extra[1] = b;
#ifndef _WIN32
        incap_end();
    }
#endif
    errno = 0;
    s->r[1] = cplus_scanf(nf, &a);
    s->er[1] = errno;
    CHECK(s->r[1] == EOF && s->er[1] == EINVAL);
}
/* METHOD: cplus_scanf / cplus_fscanf / cplus_sscanf (narrow) */
static void m_fmt_04_scanf_fscanf_sscanf(void)
{
    run_parity(b_sscanf_normal);
    run_parity(b_fscanf_normal);
    run_parity(b_scanf_stdin);
}

#if CHECKED_AVAILABLE
/* ================================================================== */
/* METHOD 5 -- checked cplus_snprintf_s                                */
/* ================================================================== */
static void b_snprintf_s_exact_trunc(Sig *s)
{
    cbuf b;
    cb_init(&b);
    errno = 0;
    s->r[0] = cplus_snprintf_s((char *)b.body, 3, "%d", 42);
    s->er[0] = errno;
    CHECK(s->r[0] == 2 && s->er[0] == 0 && cb_cstr(b.body, "42", 2));
    CHECK(cb_guards(&b) && cb_untouched(&b, 3));
    cb_init(&b);
    errno = 0;
    s->r[1] = cplus_snprintf_s((char *)b.body, 6, "abcdef");
    s->er[1] = errno;
    CHECK(s->r[1] == 6 && s->er[1] == 0);
    CHECK(cb_prefix(b.body, "abcde", 5) && b.body[5] == '\0');
    CHECK(cb_guards(&b) && cb_untouched(&b, 6));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_snprintf_s_zero_null(Sig *s)
{
    cbuf b;
    const char *nf = NULL;
    cb_init(&b);
    s->r[0] = cplus_snprintf_s((char *)b.body, 0, "abc%d", 7);
    CHECK(s->r[0] == 4 && cb_guards(&b) && cb_untouched(&b, 0));
    errno = 0;
    s->r[1] = cplus_snprintf_s(NULL, 8, "%d", 1);
    s->er[1] = errno;
    CHECK(s->r[1] == -1 && s->er[1] == EINVAL);
    errno = 0;
    s->r[2] = cplus_snprintf_s(NULL, 0, "xyz");
    s->er[2] = errno;
    CHECK(s->r[2] == 3);
    errno = 0;
    s->r[3] = cplus_snprintf_s((char *)b.body, CB_BODY, nf, 1);
    s->er[3] = errno;
    CHECK(s->r[3] == -1 && s->er[3] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_snprintf_s_literal(Sig *s)
{
    cbuf b;
    const char *dyn = rt_fmt_d();
    cb_init(&b);
    s->r[0] = snprintf_s((char *)b.body, CB_BODY, "%d", 7);
    CHECK(s->r[0] == 1 && cb_cstr(b.body, "7", 1));
    CHECK(cb_guards(&b));
    cb_init(&b);
    errno = 0;
    s->r[1] = snprintf_s((char *)b.body, CB_BODY, dyn, 7);
    s->er[1] = errno;
    CHECK(s->r[1] == -1 && s->er[1] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    cb_init(&b);
    errno = 0;
    s->r[2] = cplus_snprintf_s_checked(0, CB_BODY, (char *)b.body, CB_BODY,
                                       "%d", 7);
    s->er[2] = errno;
    CHECK(s->r[2] == -1 && s->er[2] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    /* direct function does not enforce literalness */
    cb_init(&b);
    s->r[3] = cplus_snprintf_s((char *)b.body, CB_BODY, dyn, 7);
    CHECK(s->r[3] == 1 && cb_cstr(b.body, "7", 1));
    /* oversized dmax refused with explicit known capacity (no overflow) */
    cb_init(&b);
    errno = 0;
    s->r[4] = cplus_snprintf_s_checked(1, CB_BODY, (char *)b.body, CB_BODY + 1,
                                       "%d", 7);
    s->er[4] = errno;
    CHECK(s->r[4] == -1 && s->er[4] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_snprintf_s_percent_n(Sig *s)
{
    const char *fmts[] = {"%n", "%hhn", "%lln", "%1$n", "before%n"};
    size_t i;
    int sink = -1;
    cbuf b;
    for (i = 0; i < sizeof fmts / sizeof fmts[0]; ++i) {
        cb_init(&b);
        errno = 0;
        s->r[i] = cplus_snprintf_s_checked(1, CB_BODY, (char *)b.body, CB_BODY,
                                           fmts[i], &sink);
        s->er[i] = errno;
        CHECK(s->r[i] < 0 && s->er[i] == EINVAL && sink == -1);
        CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    }
    cb_init(&b);
    errno = 0;
    s->r[6] = snprintf_s((char *)b.body, CB_BODY, "%n", &sink);
    s->er[6] = errno;
    CHECK(s->r[6] < 0 && s->er[6] == EINVAL && sink == -1);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
    s->sink = sink;
}
static void b_snprintf_s_huge(Sig *s)
{
    cbigbuf big;
    cb_big_init(&big);
    s->r[0] = cplus_snprintf_s((char *)big.body, CB_BIG, "%2000d", 7);
    CHECK(s->r[0] == 2000 && big.body[1999] == '7' && big.body[2000] == '\0');
    CHECK(cb_big_guards(&big));
    cb_big_init(&big);
    s->r[1] = cplus_snprintf_s((char *)big.body, CB_BIG, "%.1000d", 5);
    CHECK(s->r[1] == 1000 && big.body[999] == '5' && big.body[1000] == '\0');
    CHECK(cb_big_guards(&big));
    s->h[0] = cb_hash(big.body, CB_BIG);
}
static int v_snprintf_s_helper(char *d, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsnprintf_s(d, n, fmt, ap);
    va_end(ap);
    return r;
}
static void b_vsnprintf_s(Sig *s)
{
    cbuf b;
    int sink = -1;
    cb_init(&b);
    s->r[0] = v_snprintf_s_helper((char *)b.body, CB_BODY, "%s=%d", "x", 9);
    CHECK(s->r[0] == 3 && cb_cstr(b.body, "x=9", 3));
    CHECK(cb_guards(&b) && cb_untouched(&b, 4));
    cb_init(&b);
    s->r[1] = v_snprintf_s_helper((char *)b.body, 4, "abcdef");
    CHECK(s->r[1] == 6 && cb_cstr(b.body, "abc", 3));
    CHECK(cb_guards(&b) && cb_untouched(&b, 4));
    cb_init(&b);
    errno = 0;
    s->r[2] = v_snprintf_s_helper((char *)b.body, CB_BODY, "%n", &sink);
    s->er[2] = errno;
    CHECK(s->r[2] < 0 && s->er[2] == EINVAL && sink == -1);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
    s->sink = sink;
}
static void b_snprintf_s_stats(Sig *s)
{
    cplus_safe_stat before, after;
    cbuf b;
    cb_init(&b);
    CHECK(cplus_safe_stats_get("snprintf_s", &before) == 1);
    s->r[0] = cplus_snprintf_s((char *)b.body, CB_BODY, "%d", 7);
    CHECK(cplus_safe_stats_get("snprintf_s", &after) == 1);
    s->extra[0] = (int)(after.calls - before.calls);
    s->extra[1] = (int)(after.ok - before.ok);
    s->extra[2] = (int)(after.fail - before.fail);
    CHECK(s->extra[0] == 1 && s->extra[1] == 1 && s->extra[2] == 0);
    cb_init(&b);
    CHECK(cplus_safe_stats_get("snprintf_s", &before) == 1);
    errno = 0;
    s->r[1] = cplus_snprintf_s_checked(0, CB_BODY, (char *)b.body, CB_BODY,
                                       "%d", 7);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL);
    CHECK(cplus_safe_stats_get("snprintf_s", &after) == 1);
    s->extra[3] = (int)(after.fail - before.fail);
    CHECK(s->extra[3] == 1);
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
/* METHOD: checked cplus_snprintf_s (bounds, literalness, %n, va_list, stats) */
static void m_fmt_05_snprintf_s(void)
{
    run_parity(b_snprintf_s_exact_trunc);
    run_parity(b_snprintf_s_zero_null);
    run_parity(b_snprintf_s_literal);
    run_parity(b_snprintf_s_percent_n);
    run_parity(b_snprintf_s_huge);
    run_parity(b_vsnprintf_s);
    run_parity(b_snprintf_s_stats);
}

/* ================================================================== */
/* METHOD 6 -- checked cplus_sprintf_s                                 */
/* ================================================================== */
static void b_sprintf_s_exact(Sig *s)
{
    cbuf b;
    cb_init(&b);
    s->r[0] = cplus_sprintf_s((char *)b.body, 4, "%d", 123);
    CHECK(s->r[0] == 3 && cb_cstr(b.body, "123", 3));
    CHECK(cb_guards(&b) && cb_untouched(&b, 4));
    cb_init(&b);
    s->r[1] = sprintf_s((char *)b.body, 3, "%d", 12345);
    CHECK(s->r[1] == 5 && cb_cstr(b.body, "12", 2));
    CHECK(cb_guards(&b) && cb_untouched(&b, 3));
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_sprintf_s_refusals(Sig *s)
{
    cbuf b;
    const char *nf = NULL;
    const char *dyn = rt_fmt_d();
    int sink = -1;
    cb_init(&b);
    errno = 0;
    s->r[0] = sprintf_s((char *)b.body, CB_BODY, "%n", &sink);
    s->er[0] = errno;
    CHECK(s->r[0] < 0 && s->er[0] == EINVAL && sink == -1);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    cb_init(&b);
    errno = 0;
    s->r[1] = sprintf_s((char *)b.body, CB_BODY, dyn, 1);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    cb_init(&b);
    errno = 0;
    s->r[2] = cplus_sprintf_s_checked(1, CB_BODY, (char *)b.body, CB_BODY + 1,
                                      "%d", 1);
    s->er[2] = errno;
    CHECK(s->r[2] < 0 && s->er[2] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    errno = 0;
    s->r[3] = cplus_sprintf_s((char *)b.body, CB_BODY, nf, 1);
    s->er[3] = errno;
    CHECK(s->r[3] < 0 && s->er[3] == EINVAL);
    errno = 0;
    s->r[4] = cplus_sprintf_s(NULL, CB_BODY, "%d", 1);
    s->er[4] = errno;
    CHECK(s->r[4] < 0 && s->er[4] == EINVAL);
    s->sink = sink;
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static int v_sprintf_s_helper(char *d, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsprintf_s(d, n, fmt, ap);
    va_end(ap);
    return r;
}
static void b_vsprintf_s(Sig *s)
{
    cbuf b;
    int sink = -1;
    cb_init(&b);
    s->r[0] = v_sprintf_s_helper((char *)b.body, CB_BODY, "%d-%d", 1, 2);
    CHECK(s->r[0] == 3 && cb_cstr(b.body, "1-2", 3));
    CHECK(cb_guards(&b) && cb_untouched(&b, 4));
    cb_init(&b);
    errno = 0;
    s->r[1] = v_sprintf_s_helper((char *)b.body, CB_BODY, "%n", &sink);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL && sink == -1);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    s->sink = sink;
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
/* METHOD: checked cplus_sprintf_s (bounded alias, refusals, va_list) */
static void m_fmt_06_sprintf_s(void)
{
    run_parity(b_sprintf_s_exact);
    run_parity(b_sprintf_s_refusals);
    run_parity(b_vsprintf_s);
}

/* ================================================================== */
/* METHOD 7 -- checked cplus_fprintf_s                                 */
/* ================================================================== */
static void b_fprintf_s_ok(Sig *s)
{
    FILE *f = tmpfile();
    char rd[16];
    size_t n;
    const char *nf = NULL;
    CHECK(f != NULL);
    if (!f) return;
    errno = 0;
    s->r[0] = cplus_fprintf_s(f, "%s %d", "hi", 3);
    s->er[0] = errno;
    CHECK(s->r[0] == 4 && s->er[0] == 0);
    rewind(f);
    n = fread(rd, 1, sizeof rd - 1, f);
    rd[n] = '\0';
    CHECK(n == 4 && memcmp(rd, "hi 3", 4) == 0);
    s->h[0] = cb_hash((const unsigned char *)rd, n);
    errno = 0;
    s->r[1] = cplus_fprintf_s(f, nf, 1);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL);
    errno = 0;
    s->r[2] = cplus_fprintf_s(NULL, "%d", 1);
    s->er[2] = errno;
    CHECK(s->r[2] < 0 && s->er[2] == EINVAL);
    s->extra[0] = (int)ftell(f);
    fclose(f);
}
static void b_fprintf_s_refusals(Sig *s)
{
    FILE *f = tmpfile();
    int sink = -1;
    const char *dyn = rt_fmt_d();
    CHECK(f != NULL);
    if (!f) return;
    errno = 0;
    s->r[0] = fprintf_s(f, "%n", &sink);
    s->er[0] = errno;
    CHECK(s->r[0] < 0 && s->er[0] == EINVAL && sink == -1);
    errno = 0;
    s->r[1] = fprintf_s(f, dyn, 42);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL);
    errno = 0;
    s->r[2] = cplus_fprintf_s_checked(0, f, "%d", 7);
    s->er[2] = errno;
    CHECK(s->r[2] < 0 && s->er[2] == EINVAL);
    s->extra[0] = (int)ftell(f);
    CHECK(s->extra[0] == 0);
    s->sink = sink;
    fclose(f);
}
/* METHOD: checked cplus_fprintf_s (stream, NULL, literalness, %n) */
static void m_fmt_07_fprintf_s(void)
{
    run_parity(b_fprintf_s_ok);
    run_parity(b_fprintf_s_refusals);
}

/* ================================================================== */
/* METHOD 8 -- checked cplus_printf_s                                  */
/* ================================================================== */
static void b_printf_s_ok(Sig *s)
{
    char cap[64];
    size_t n = 0;
    int capok = 0;
    cap[0] = '\0';
#ifndef _WIN32
    capok = (outcap_begin() == 0);
#endif
    errno = 0;
    s->r[0] = cplus_printf_s("<%d>", 42);
    s->er[0] = errno;
#ifndef _WIN32
    if (capok) n = outcap_end(cap, sizeof cap);
#endif
    CHECK(s->r[0] == 4 && s->er[0] == 0);
#ifndef _WIN32
    CHECK(n == 4 && memcmp(cap, "<42>", 4) == 0);
#endif
    s->h[0] = cb_hash((const unsigned char *)cap, sizeof cap);
}
static void b_printf_s_refusals(Sig *s)
{
    const char *nf = NULL;
    const char *dyn = rt_fmt_d();
    int sink = -1;
    char cap[64];
    size_t n = 0;
    int capok = 0;
    cap[0] = '\0';
#ifndef _WIN32
    capok = (outcap_begin() == 0);
#endif
    errno = 0;
    s->r[0] = printf_s("%n", &sink);
    s->er[0] = errno;
    CHECK(s->r[0] < 0 && s->er[0] == EINVAL && sink == -1);
    errno = 0;
    s->r[1] = printf_s(dyn, 42);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL);
    errno = 0;
    s->r[2] = cplus_printf_s_checked(0, "%d", 7);
    s->er[2] = errno;
    CHECK(s->r[2] < 0 && s->er[2] == EINVAL);
    errno = 0;
    /* dummy arg as above: non-literal (NULL) format is refused before use. */
    s->r[3] = cplus_printf_s(nf, 0);
    s->er[3] = errno;
    CHECK(s->r[3] < 0 && s->er[3] == EINVAL);
    /* direct function does not enforce literalness */
    s->r[4] = cplus_printf_s(dyn, 9);
    CHECK(s->r[4] == 1);
#ifndef _WIN32
    if (capok) n = outcap_end(cap, sizeof cap);
#endif
    s->h[0] = cb_hash((const unsigned char *)cap, sizeof cap);
    s->extra[0] = (int)n;
    s->sink = sink;
}
/* METHOD: checked cplus_printf_s (literal, %n, NULL, direct vs macro) */
static void m_fmt_08_printf_s(void)
{
    run_parity(b_printf_s_ok);
    run_parity(b_printf_s_refusals);
}

/* ================================================================== */
/* METHOD 9 -- checked cplus_scanf_s (+ cplus_sscanf_s)                 */
/* ================================================================== */
static void b_sscanf_s_ok(Sig *s)
{
    int a = 0, b = 0;
    char t[8] = {0};
    s->r[0] = cplus_sscanf_s("12 34", "%d %d", &a, &b);
    CHECK(s->r[0] == 2 && a == 12 && b == 34);
    s->extra[0] = a;
    s->extra[1] = b;
    s->r[1] = cplus_sscanf_s("hello world", "%7s", t);
    CHECK(s->r[1] == 1 && memcmp(t, "hello", 5) == 0 && t[5] == '\0');
    s->h[0] = cb_hash((const unsigned char *)t, sizeof t);
}
static void b_sscanf_s_refusals(Sig *s)
{
    cbuf b;
    int sink = -1;
    const char *dyn = rt_fmt_d();
    cb_init(&b);
    errno = 0;
    s->r[0] = sscanf_s("abc", "%s", (char *)b.body);
    s->er[0] = errno;
    CHECK(s->r[0] < 0 && s->er[0] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    cb_init(&b);
    errno = 0;
    s->r[1] = cplus_sscanf_s("abc", "%n", &sink);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL && sink == -1);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    cb_init(&b);
    errno = 0;
    s->r[2] = sscanf_s("42", dyn, &sink);
    s->er[2] = errno;
    CHECK(s->r[2] < 0 && s->er[2] == EINVAL && sink == -1);
    errno = 0;
    s->r[3] = cplus_sscanf_s(NULL, "%d", &sink);
    s->er[3] = errno;
    CHECK(s->r[3] < 0 && s->er[3] == EINVAL);
    errno = 0;
    s->r[4] = cplus_sscanf_s("1", NULL, &sink);
    s->er[4] = errno;
    CHECK(s->r[4] < 0 && s->er[4] == EINVAL);
    s->sink = sink;
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
static void b_scanf_s_ok(Sig *s)
{
    int a = 0, b = 0;
#ifndef _WIN32
    if (incap_begin("5 6\n") == 0) {
#endif
        errno = 0;
        s->r[0] = scanf_s("%d %d", &a, &b);
        s->er[0] = errno;
        CHECK(s->r[0] == 2 && a == 5 && b == 6);
        s->extra[0] = a;
        s->extra[1] = b;
#ifndef _WIN32
        incap_end();
    }
#endif
}
static void b_scanf_s_refusals(Sig *s)
{
    cbuf b;
    int sink = -1;
    cb_init(&b);
    errno = 0;
    s->r[0] = scanf_s("%s", (char *)b.body);
    s->er[0] = errno;
    CHECK(s->r[0] < 0 && s->er[0] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    errno = 0;
    s->r[1] = scanf_s("%n", &sink);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL && sink == -1);
    errno = 0;
    s->r[2] = scanf_s(rt_fmt_d(), &sink);
    s->er[2] = errno;
    CHECK(s->r[2] < 0 && s->er[2] == EINVAL && sink == -1);
    s->sink = sink;
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
}
/* METHOD: checked cplus_scanf_s (+ cplus_sscanf_s): width, %n, literalness */
static void m_fmt_09_scanf_s_sscanf_s(void)
{
    run_parity(b_sscanf_s_ok);
    run_parity(b_sscanf_s_refusals);
    run_parity(b_scanf_s_ok);
    run_parity(b_scanf_s_refusals);
}

/* ================================================================== */
/* METHOD 10 -- checked cplus_fscanf_s                                 */
/* ================================================================== */
static void b_fscanf_s_ok(Sig *s)
{
    FILE *f = tmpfile();
    int v = 0;
    char t[8] = {0};
    CHECK(f != NULL);
    if (!f) return;
    CHECK(fputs("42 hello\n", f) >= 0);
    rewind(f);
    s->r[0] = cplus_fscanf_s(f, "%d %7s", &v, t);
    CHECK(s->r[0] == 2 && v == 42 && memcmp(t, "hello", 5) == 0);
    s->extra[0] = v;
    s->h[0] = cb_hash((const unsigned char *)t, sizeof t);
    fclose(f);
}
static void b_fscanf_s_refusals(Sig *s)
{
    FILE *f = tmpfile();
    cbuf b;
    int sink = -1;
    const char *dyn = rt_fmt_d();
    CHECK(f != NULL);
    if (!f) return;
    cb_init(&b);
    errno = 0;
    s->r[0] = fscanf_s(f, "%s", (char *)b.body);
    s->er[0] = errno;
    CHECK(s->r[0] < 0 && s->er[0] == EINVAL);
    CHECK(cb_guards(&b) && cb_untouched(&b, 0));
    cb_init(&b);
    errno = 0;
    s->r[1] = fscanf_s(f, "%n", &sink);
    s->er[1] = errno;
    CHECK(s->r[1] < 0 && s->er[1] == EINVAL && sink == -1);
    errno = 0;
    s->r[2] = fscanf_s(f, dyn, &sink);
    s->er[2] = errno;
    CHECK(s->r[2] < 0 && s->er[2] == EINVAL && sink == -1);
    errno = 0;
    s->r[3] = cplus_fscanf_s(NULL, "%d", &sink);
    s->er[3] = errno;
    CHECK(s->r[3] < 0 && s->er[3] == EINVAL);
    s->extra[0] = (int)ftell(f);
    s->sink = sink;
    s->h[0] = cb_hash(b.body, CB_BODY);
    s->guards = cb_guards(&b);
    fclose(f);
}
/* METHOD: checked cplus_fscanf_s (stream parse, width, %n, literalness) */
static void m_fmt_10_fscanf_s(void)
{
    run_parity(b_fscanf_s_ok);
    run_parity(b_fscanf_s_refusals);
}
#endif /* CHECKED_AVAILABLE */

/* ------------------------------------------------------------------ */
/* driver                                                              */
/* ------------------------------------------------------------------ */
typedef void (*method_fn)(void);
typedef struct {
    const char *name;
    method_fn fn;
} Method;
static const Method METHODS[] = {
    {"m_fmt_01_printf_fprintf", m_fmt_01_printf_fprintf},
    {"m_fmt_02_snprintf_vsnprintf", m_fmt_02_snprintf_vsnprintf},
    {"m_fmt_03_sprintf_vsprintf", m_fmt_03_sprintf_vsprintf},
    {"m_fmt_04_scanf_fscanf_sscanf", m_fmt_04_scanf_fscanf_sscanf},
#if CHECKED_AVAILABLE
    {"m_fmt_05_snprintf_s", m_fmt_05_snprintf_s},
    {"m_fmt_06_sprintf_s", m_fmt_06_sprintf_s},
    {"m_fmt_07_fprintf_s", m_fmt_07_fprintf_s},
    {"m_fmt_08_printf_s", m_fmt_08_printf_s},
    {"m_fmt_09_scanf_s_sscanf_s", m_fmt_09_scanf_s_sscanf_s},
    {"m_fmt_10_fscanf_s", m_fmt_10_fscanf_s},
#endif
};
#define NMETHODS ((int)(sizeof METHODS / sizeof METHODS[0]))

int main(void)
{
    int i;
    char line[FIND_LEN + 32];
#if CHECKED_AVAILABLE
    cplus_runtime_set_violation_trap(0);
    cplus_runtime_set_failstop(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
#else
    fputs("methods_format: note - checked cplus_*_s methods skipped "
          "(not present in the plain libc layer)\n", stdout);
#endif
    logging_set(0);
#ifndef _WIN32
    err_sink_begin();
#endif
    for (i = 0; i < NMETHODS; ++i) METHODS[i].fn();
#ifndef _WIN32
    err_sink_end();
#endif
    logging_set(0);
    for (i = 0; i < g_nfind; ++i) {
        (void)snprintf(line, sizeof line, "%s\n", g_findings[i]);
        fputs(line, stdout);
    }
    (void)snprintf(line, sizeof line, "methods_format: %d/%d checks ok\n",
                   g_total - g_fail, g_total);
    fputs(line, stdout);
    return 0;
}

/*
 * WRONG-TYPE ARGUMENTS -- COMPILE-TIME PROBE ONLY (NEVER EXECUTED).
 * The Clang/GCC `format` attributes on the wrappers reject the two classic
 * mismatches at compile time. The probe lives in /tmp and is compiled with
 * -Werror=format; it is not part of this translation unit because executing
 * it would be genuine undefined behaviour. See the verification note returned
 * with this file for the exact compiler diagnostics.
 *   char b[32];
 *   cplus_snprintf_s(b, sizeof b, "%d", (void *)0);   // pointer for %d
 *   cplus_printf_s("%ls", "narrow char* not wchar_t*"); // wrong type for %ls
 */
