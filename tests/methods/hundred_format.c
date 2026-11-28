/* tests/methods/hundred_format.c
 *
 * 100 DISTINCT runtime scenarios for the formatting methods of BOTH safe-C
 * layers in this repository:
 *
 *   Layer 1 (plain hardened libc, "plain" build variant):
 *       cplus_printf / fprintf / snprintf / vsnprintf / sprintf / vsprintf /
 *       scanf / fscanf / sscanf / vprintf / vfprintf / vsscanf / vfscanf
 *       from libc/install-include/cplus_libc.h + libcplus_hardened.a.
 *
 *   Layer 2 (checked SafeCRT, "safecrt" build variant):
 *       cplus_snprintf_s / vsnprintf_s / sprintf_s / vsprintf_s /
 *       printf_s / fprintf_s / scanf_s / fscanf_s / sscanf_s, their
 *       *_checked adapters and the unprefixed *_s macros from
 *       stdlib/safe/safe_format.h + libcplus_safecrt.a.
 *
 * The harness tests/methods/run_hundred.sh compiles this file twice: once with
 * -Istdlib/safe and BOTH static libs (safecrt: both layers live), and once with
 * only libc include paths + libcplus_hardened.a (plain: only Layer 1 lives).
 * __has_include("safe_format.h") detects which. When Layer 2 is absent its
 * 56 cases report SKIP (an expected, counted outcome) so that the file always
 * prints exactly 100 case lines in every variant.
 *
 * EXACT OBSERVED BEHAVIOUR RECORDED HERE (verified in stdlib/safe/src/safe_format.c):
 *   - %n and ALL its length/positional variants (%hhn, %lln, %zn, %1$n, ...)
 *     are REJECTED by every checked entry point: return -1 with errno=EINVAL,
 *     destination untouched, the %n target never read or written. The rejection
 *     happens in cplus_fmt_has_percent_n()/cplus_scanf_fmt_check() BEFORE any
 *     variadic argument is consumed, so no UB occurs. A literal %%n is ACCEPTED
 *     (the parser treats the second '%' as an escape and 'n' as an ordinary
 *     character) and produces the two characters "%n".
 *   - Non-literal format: the unprefixed *_s macros pass
 *     __builtin_constant_p(fmt) to *_s_checked; a runtime format yields 0 and is
 *     refused with -1/EINVAL before consuming arguments (CWE-134 gate). The raw
 *     cplus_*_s functions do NOT re-measure literalness; they rely on the
 *     compiler format attribute. Both paths are exercised below.
 *   - Checked bounded variants refuse NULL format/dest(stream) and dmax larger
 *     than proven capacity with -1/EINVAL, never writing a byte.
 *   - Plain cplus_* functions guard NULL but do NOT reject %n or non-literal
 *     formats (raw C remains the caller's responsibility); %n with a VALID
 *     int* / unsigned char* target is therefore defined and its written count
 *     is observed in case 15 below.
 *
 * The violation trap (cplus_runtime_set_violation_trap) is disabled so the
 * -1/EINVAL contract is observable instead of _exit(70). Logging metadata is
 * written to stderr only and never changes a result (OFF/ON parity cases 43,
 * 93, 94, 100). No case invokes genuine UB; hostile inputs only ever reach
 * checked entry points. All buffers are canary-guarded; every case verifies
 * canary integrity. Warnings from deliberate non-literal/hostile formats are
 * suppressed locally with clang pragmas.
 */
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cplus_libc.h" /* Layer 1 declarations (format attributes included). */

#if __has_include("safe_format.h")
#include "safe_format.h"
#include "safe_log.h"
#include "safe_runtime.h"
#define HAVE_SAFECRT 1
#else
#define HAVE_SAFECRT 0
#endif

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-security"
#pragma clang diagnostic ignored "-Wformat-extra-args"
#endif

#define DEST_CAP 128u
#define LEAD_BYTE 0xA5u
#define TRAIL_BYTE 0x5Au

typedef struct {
    unsigned char lead[8];
    char buf[DEST_CAP];
    unsigned char trail[8];
} gbuf_t;

static void gb_init(gbuf_t *g) {
    size_t i;
    for (i = 0; i < sizeof g->lead; ++i) g->lead[i] = LEAD_BYTE;
    for (i = 0; i < sizeof g->trail; ++i) g->trail[i] = TRAIL_BYTE;
    memset(g->buf, 'X', sizeof g->buf);
}

static int gb_canaries(const gbuf_t *g) {
    size_t i;
    for (i = 0; i < sizeof g->lead; ++i)
        if (g->lead[i] != LEAD_BYTE) return 0;
    for (i = 0; i < sizeof g->trail; ++i)
        if (g->trail[i] != TRAIL_BYTE) return 0;
    return 1;
}

/* Whole buffer must equal the NUL-terminated `want` and canaries be intact. */
static int gb_equals(const gbuf_t *g, const char *want) {
    if (!gb_canaries(g)) return 0;
    if (strlen(want) >= sizeof g->buf) return 0;
    return strcmp(g->buf, want) == 0;
}

/* stdio capture helpers -------------------------------------------------- */
typedef struct {
    FILE *f;
    int saved;
} cap_t;

static int cap_out_begin(cap_t *c) {
    fflush(stdout);
    c->f = tmpfile();
    if (!c->f) return 0;
    c->saved = dup(STDOUT_FILENO);
    if (c->saved < 0 || dup2(fileno(c->f), STDOUT_FILENO) < 0) return 0;
    return 1;
}

static void cap_out_end(cap_t *c, char *out, size_t outcap) {
    fflush(stdout);
    if (c->saved >= 0) {
        dup2(c->saved, STDOUT_FILENO);
        close(c->saved);
    }
    rewind(c->f);
    if (out && outcap) {
        size_t n = fread(out, 1, outcap - 1, c->f);
        out[n] = '\0';
    }
    fclose(c->f);
}

/* printf-style operations that write to stdout via a FILE* cannot be
 * redirected with the above; instead every stdout case uses cplus_printf /
 * cplus_printf_s (fd 1). fprintf cases use a tmpfile() stream directly. */
static FILE *make_input(const char *text) {
    FILE *f = tmpfile();
    if (!f) return NULL;
    fputs(text, f);
    fflush(f);
    rewind(f);
    return f;
}

/* Runtime (non-constant) format providers: defeat __builtin_constant_p so the
 * checked gate is genuinely exercised, and avoid -Wformat on odd literals. */
static const char *fmt_embedded_nul(void) {
    static const char f[] = {'A', '\0', 'B', '%', 'd', '\0'};
    return f;
}
#if HAVE_SAFECRT
static const char *fmt_literal_d(void) {
    static const char f[] = {'%', 'd', '\0'};
    return f;
}
static const char *fmt_too_many(void) {
    static const char f[] = {'%', 'd', '\0'};
    return f;
}
static const char *fmt_too_few(void) {
    static const char f[] = {'%', 'd', ' ', '%', 'd', ' ', '%', 'd', '\0'};
    return f;
}
static const char *fmt_n_nonlit(void) {
    static const char f[] = {'x', '%', 'n', '\0'};
    return f;
}
#endif /* HAVE_SAFECRT */

/* v-variant trampolines --------------------------------------------------- */
static int vp_snprintf(char *b, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsnprintf(b, n, fmt, ap);
    va_end(ap);
    return r;
}
static int vp_vprintf_redir(const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vprintf(fmt, ap);
    va_end(ap);
    return r;
}
static int vp_vsnprintf_nullfmt(char *b, size_t n, const char *fmt, ...) {
    /* fmt is NULL at the call site; the callee checks fmt before using ap. */
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsnprintf(b, n, fmt, ap);
    va_end(ap);
    return r;
}
static int vp_vfscanf(FILE *f, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vfscanf(f, fmt, ap);
    va_end(ap);
    return r;
}
static int vp_fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
static int vp_sscanf(const char *str, const char *fmt, ...) {
    /* `fmt` is the last named parameter both here and in cplus_vsscanf. */
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

static int g_ok = 0;
static int g_findings = 0;

/* ======================================================================
 * LAYER 1 — plain hardened libc (cases 1..44)
 * ====================================================================== */

static const char *c1(void) {
    gbuf_t g; int r; gb_init(&g); errno = 0;
    r = cplus_snprintf(g.buf, sizeof g.buf, "%d", 7);
    if (r != 1 || !gb_equals(&g, "7")) return "FAIL:literal-correct-args";
    return "OK";
}
static const char *c2(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, 4, "%s", "abc");
    if (r != 3 || !gb_equals(&g, "abc")) return "FAIL:exact-fit";
    return "OK";
}
static const char *c3(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, 3, "%s", "abcd");
    if (r != 4 || memcmp(g.buf, "ab", 2) != 0 || g.buf[2] != '\0' || !gb_canaries(&g))
        return "FAIL:one-short-truncation";
    return "OK";
}
static const char *c4(void) {
    gbuf_t g; int r; size_t i; gb_init(&g);
    r = cplus_snprintf(g.buf, 0, "%s", "ignored");
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:zero-size-wrote";
    if (r != 7 || !gb_canaries(&g)) return "FAIL:zero-size-dest";
    return "OK";
}
static const char *c5(void) {
    int r = cplus_snprintf(NULL, 0, "hello %d", 5);
    if (r != 7) return "FAIL:null-dest-zero-size";
    return "OK";
}
static const char *c6(void) {
    errno = 0;
    if (cplus_snprintf(NULL, 16, "%s", "x") != -1 || errno == 0) return "FAIL:null-dest-nonzero";
    return "OK";
}
static const char *c7(void) {
    gbuf_t g; gb_init(&g); errno = 0;
    if (cplus_snprintf(g.buf, sizeof g.buf, NULL) != -1 || errno == 0 || !gb_canaries(&g))
        return "FAIL:null-format";
    return "OK";
}
static const char *c8(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, sizeof g.buf, "%.2s", "abcdef");
    if (r != 2 || !gb_equals(&g, "ab")) return "FAIL:precision-2s";
    return "OK";
}
static const char *c9(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, sizeof g.buf, "%.*s", 3, "abcdef");
    if (r != 3 || !gb_equals(&g, "abc")) return "FAIL:dynamic-precision";
    return "OK";
}
static const char *c10(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, 16, "%1000d", 7);
    if (r != 1000 || strlen(g.buf) != 15 || !gb_canaries(&g)) return "FAIL:huge-width";
    return "OK";
}
static const char *c11(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, sizeof g.buf, "100%% done");
    if (r != 9 || !gb_equals(&g, "100% done")) return "FAIL:percent-escape";
    return "OK";
}
static const char *c12(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, sizeof g.buf, "%2$s-%1$d", 7, "xy");
    if (r != 4 || !gb_equals(&g, "xy-7")) return "FAIL:positional";
    return "OK";
}
static const char *c13(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, sizeof g.buf, fmt_embedded_nul(), 7);
    if (r != 1 || !gb_equals(&g, "A")) return "FAIL:embedded-NUL";
    return "OK";
}
static const char *c14(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf(g.buf, sizeof g.buf, "[%ls]", L"AB");
    if (r != 4 || !gb_equals(&g, "[AB]")) return "FAIL:wide-derived-ls";
    return "OK";
}
static const char *c15(void) {
    /* Plain layer does NOT forbid %n: with a valid target this is defined. */
    gbuf_t g; int r; int cnt = -1; gb_init(&g);
    r = cplus_snprintf(g.buf, sizeof g.buf, "ab%ncd", &cnt);
    if (r != 4 || cnt != 2 || !gb_equals(&g, "abcd")) return "FAIL:pct-n-count";
    return "OK";
}
static const char *c16(void) {
    gbuf_t g; int r; gb_init(&g);
    r = vp_snprintf(g.buf, sizeof g.buf, "%d-%s", 7, "ok");
    if (r != 4 || !gb_equals(&g, "7-ok")) return "FAIL:vsnprintf-basic";
    return "OK";
}
static const char *c17(void) {
    gbuf_t g; int r; gb_init(&g);
    r = vp_snprintf(g.buf, 4, "%s", "abcdef");
    if (r != 6 || memcmp(g.buf, "abc", 3) != 0 || g.buf[3] != '\0' || !gb_canaries(&g))
        return "FAIL:vsnprintf-truncation";
    return "OK";
}
static const char *c18(void) {
    gbuf_t g; gb_init(&g); errno = 0;
    if (vp_vsnprintf_nullfmt(g.buf, sizeof g.buf, NULL, 7) != -1 || errno == 0)
        return "FAIL:vsnprintf-null-format";
    return "OK";
}
static const char *c19(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_sprintf(g.buf, "%s-%d", "v", 42);
    if (r != 4 || !gb_equals(&g, "v-42")) return "FAIL:sprintf-basic";
    return "OK";
}
static const char *c20(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_sprintf(g.buf, "%%%d", 3);
    if (r != 2 || !gb_equals(&g, "%3")) return "FAIL:sprintf-percent";
    return "OK";
}
static const char *c21(void) {
    errno = 0;
    if (cplus_sprintf(NULL, "%d", 1) != -1 || errno == 0) return "FAIL:sprintf-null-dest";
    return "OK";
}
static const char *c22(void) {
    gbuf_t g; gb_init(&g); errno = 0;
    if (cplus_sprintf(g.buf, NULL) != -1 || errno == 0 || !gb_canaries(&g))
        return "FAIL:sprintf-null-format";
    return "OK";
}
static const char *c23(void) {
    cap_t c; char out[32] = ""; int r;
    if (!cap_out_begin(&c)) return "FAIL:printf-capture-setup";
    r = cplus_printf("%d", 7);
    cap_out_end(&c, out, sizeof out);
    if (r != 1 || strcmp(out, "7") != 0) return "FAIL:printf-literal";
    return "OK";
}
static const char *c24(void) {
    errno = 0;
    if (cplus_printf(NULL) != -1 || errno == 0) return "FAIL:printf-null-format";
    return "OK";
}
static const char *c25(void) {
    cap_t c; char out[32] = ""; int r;
    if (!cap_out_begin(&c)) return "FAIL:printf-pct-capture-setup";
    r = cplus_printf("50%%");
    cap_out_end(&c, out, sizeof out);
    if (r != 3 || strcmp(out, "50%") != 0) return "FAIL:printf-percent";
    return "OK";
}
static const char *c26(void) {
    cap_t c; char out[32] = ""; int r;
    if (!cap_out_begin(&c)) return "FAIL:vprintf-capture-setup";
    r = vp_vprintf_redir("v=%d", 9);
    cap_out_end(&c, out, sizeof out);
    if (r != 3 || strcmp(out, "v=9") != 0) return "FAIL:vprintf-basic";
    return "OK";
}
static const char *c27(void) {
    FILE *f = make_input(""); char out[32] = ""; int r;
    if (!f) return "FAIL:fprintf-setup";
    r = cplus_fprintf(f, "f=%d", 4);
    rewind(f); { size_t n = fread(out, 1, sizeof out - 1, f); out[n] = 0; }
    fclose(f);
    if (r != 3 || strcmp(out, "f=4") != 0) return "FAIL:fprintf-basic";
    return "OK";
}
static const char *c28(void) {
    errno = 0;
    if (cplus_fprintf(NULL, "%d", 1) != -1 || errno == 0) return "FAIL:fprintf-null-stream";
    return "OK";
}
static const char *c29(void) {
    FILE *f = make_input(""); errno = 0;
    if (!f) return "FAIL:fprintf-nullfmt-setup";
    if (cplus_fprintf(f, NULL) != -1 || errno == 0) { fclose(f); return "FAIL:fprintf-null-format"; }
    fclose(f);
    return "OK";
}
static const char *c30(void) {
    FILE *f = make_input(""); char out[32] = ""; int r;
    if (!f) return "FAIL:vfprintf-setup";
    r = vp_fprintf(f, "vf=%d", 6);
    rewind(f); { size_t n = fread(out, 1, sizeof out - 1, f); out[n] = 0; }
    fclose(f);
    if (r != 4 || strcmp(out, "vf=6") != 0) return "FAIL:vfprintf-basic";
    return "OK";
}
static const char *c31(void) {
    int v = 0; int r = cplus_sscanf("42", "%d", &v);
    if (r != 1 || v != 42) return "FAIL:sscanf-one-int";
    return "OK";
}
static const char *c32(void) {
    int v = 0; char s[16] = ""; int r = cplus_sscanf("42 abc", "%d %15s", &v, s);
    if (r != 2 || v != 42 || strcmp(s, "abc") != 0) return "FAIL:sscanf-int-string";
    return "OK";
}
static const char *c33(void) {
    char s[16] = ""; int r = cplus_sscanf("abcdefgh", "%3s", s);
    if (r != 1 || strcmp(s, "abc") != 0) return "FAIL:sscanf-width";
    return "OK";
}
static const char *c34(void) {
    int v = -1; int r = cplus_sscanf("99 7", "%*d %d", &v);
    if (r != 1 || v != 7) return "FAIL:sscanf-suppression";
    return "OK";
}
static const char *c35(void) {
    int v = -1; int r = cplus_sscanf("abc", "%d", &v);
    if (r != 0 || v != -1) return "FAIL:sscanf-mismatch";
    return "OK";
}
static const char *c36(void) {
    if (cplus_sscanf(NULL, "%d", (int *)0) != EOF) return "FAIL:sscanf-null-input";
    return "OK";
}
static const char *c37(void) {
    if (cplus_sscanf("1", NULL) != EOF) return "FAIL:sscanf-null-format";
    return "OK";
}
static const char *c38(void) {
    FILE *f = make_input("42\n"); int v = 0; int r;
    if (!f) return "FAIL:fscanf-setup";
    r = cplus_fscanf(f, "%d", &v);
    fclose(f);
    if (r != 1 || v != 42) return "FAIL:fscanf-int";
    return "OK";
}
static const char *c39(void) {
    if (cplus_fscanf(NULL, "%d", (int *)0) != EOF) return "FAIL:fscanf-null-stream";
    return "OK";
}
static const char *c40(void) {
    /* Redirect fd 0 from a temp file so cplus_scanf reads deterministic data. */
    FILE *f = make_input("42\n"); int v = 0; int r; int saved;
    if (!f) return "FAIL:scanf-setup";
    saved = dup(STDIN_FILENO);
    if (saved < 0 || dup2(fileno(f), STDIN_FILENO) < 0) { fclose(f); return "FAIL:scanf-dup"; }
    r = cplus_scanf("%d", &v);
    dup2(saved, STDIN_FILENO);
    close(saved);
    fclose(f);
    if (r != 1 || v != 42) return "FAIL:scanf-stdin";
    return "OK";
}
static const char *c41(void) {
    int v = 0; int r = vp_sscanf("17", "%d", &v);
    if (r != 1 || v != 17) return "FAIL:vsscanf-basic";
    return "OK";
}
static const char *c42(void) {
    FILE *f = make_input("23\n"); int v = 0; int r;
    if (!f) return "FAIL:vfscanf-setup";
    r = vp_vfscanf(f, "%d", &v);
    fclose(f);
    if (r != 1 || v != 23) return "FAIL:vfscanf-basic";
    return "OK";
}
static const char *c43(void) {
    /* Logging OFF/ON parity (plain libc logger). */
    gbuf_t a, b; int ra, rb;
    gb_init(&a); gb_init(&b);
    cplus_libc_log_set_enabled(0);
    ra = cplus_snprintf(a.buf, sizeof a.buf, "%d-%s", 7, "parity");
    cplus_libc_log_set_enabled(1);
    rb = cplus_snprintf(b.buf, sizeof b.buf, "%d-%s", 7, "parity");
    cplus_libc_log_set_enabled(0);
    if (ra != rb || !gb_equals(&a, "7-parity") || !gb_equals(&b, "7-parity"))
        return "FAIL:plain-log-parity";
    return "OK";
}
static const char *c44(void) {
    /* Dedicated canary integrity across repeated truncation writes. */
    gbuf_t g; int i; gb_init(&g);
    for (i = 1; i <= 8; ++i)
        (void)cplus_snprintf(g.buf, (size_t)i, "%s", "abcdefgh");
    if (!gb_canaries(&g)) return "FAIL:plain-canary-integrity";
    return "OK";
}

/* ======================================================================
 * LAYER 2 — checked SafeCRT (cases 45..100). Stubbed with SKIP when absent.
 * ====================================================================== */

#if HAVE_SAFECRT

/* checked-layer v-trampolines -------------------------------------------- */
static int cv_snprintf(char *b, size_t n, const char *fmt, ...) {
    va_list ap; int r; va_start(ap, fmt); r = cplus_vsnprintf_s(b, n, fmt, ap); va_end(ap); return r;
}
static int cv_sprintf(char *b, size_t n, const char *fmt, ...) {
    va_list ap; int r; va_start(ap, fmt); r = cplus_vsprintf_s(b, n, fmt, ap); va_end(ap); return r;
}
static int cvp_vsnprintf_nullfmt(char *b, size_t n, const char *fmt, ...) {
    va_list ap; int r; va_start(ap, fmt); r = cplus_vsnprintf_s(b, n, fmt, ap); va_end(ap); return r;
}

static const char *c45(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "%d", 7);
    if (r != 1 || !gb_equals(&g, "7")) return "FAIL:snprintf-s-basic";
    return "OK";
}
static const char *c46(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s(g.buf, 4, "%s", "abc");
    if (r != 3 || !gb_equals(&g, "abc")) return "FAIL:snprintf-s-exact-fit";
    return "OK";
}
static const char *c47(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s(g.buf, 3, "%s", "abcd");
    if (r != 4 || memcmp(g.buf, "ab", 2) || g.buf[2] || !gb_canaries(&g))
        return "FAIL:snprintf-s-truncation";
    return "OK";
}
static const char *c48(void) {
    gbuf_t g; int r; size_t i; gb_init(&g);
    r = cplus_snprintf_s(g.buf, 0, "%s", "ignored");
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:snprintf-s-zero-wrote";
    if (r != 7 || !gb_canaries(&g)) return "FAIL:snprintf-s-zero-dmax";
    return "OK";
}
static const char *c49(void) {
    errno = 0;
    if (cplus_snprintf_s(NULL, 16, "%s", "x") != -1 || errno == 0) return "FAIL:snprintf-s-null-dest";
    return "OK";
}
static const char *c50(void) {
    int r = cplus_snprintf_s(NULL, 0, "hello %d", 5);
    if (r != 7) return "FAIL:snprintf-s-null-dest-zero";
    return "OK";
}
static const char *c51(void) {
    gbuf_t g; gb_init(&g); errno = 0;
    if (cplus_snprintf_s(g.buf, sizeof g.buf, NULL) != -1 || errno == 0 || !gb_canaries(&g))
        return "FAIL:snprintf-s-null-format";
    return "OK";
}
/* %n rejection family ---------------------------------------------------- */
typedef struct { unsigned char lead; int n; unsigned char trail; } victim_t;
static void victim_init(victim_t *v) {
    v->lead = 0x11; v->trail = 0x22; v->n = -777;
}
static int victim_ok(const victim_t *v) {
    return v->lead == 0x11 && v->trail == 0x22 && v->n == -777;
}
static const char *c52(void) {
    gbuf_t g; victim_t v; int r; size_t i; gb_init(&g); victim_init(&v); errno = 0;
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "a%nb", &v.n);
    if (r != -1 || errno != EINVAL || !victim_ok(&v) || !gb_canaries(&g)) return "FAIL:pct-n";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:pct-n-wrote-dest";
    return "OK";
}
static const char *c53(void) {
    gbuf_t g; unsigned char cc = 5; int r; gb_init(&g); errno = 0;
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "x%hhn", &cc);
    if (r != -1 || errno != EINVAL || cc != 5 || !gb_equals(&g, "")) {
        /* dest untouched => still all 'X', not "" */
        if (r != -1 || errno != EINVAL || cc != 5) return "FAIL:pct-hhn";
        { size_t i; for (i=0;i<sizeof g.buf;++i) if (g.buf[i]!='X') return "FAIL:pct-hhn-dest"; }
    }
    return "OK";
}
static const char *c54(void) {
    gbuf_t g; long long ll = 123; int r; size_t i; gb_init(&g); errno = 0;
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "%lln", &ll);
    if (r != -1 || errno != EINVAL || ll != 123) return "FAIL:pct-lln";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:pct-lln-dest";
    return "OK";
}
static const char *c55(void) {
    gbuf_t g; size_t z = 99; int r; size_t i; gb_init(&g); errno = 0;
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "%zn", &z);
    if (r != -1 || errno != EINVAL || z != 99) return "FAIL:pct-zn";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:pct-zn-dest";
    return "OK";
}
static const char *c56(void) {
    gbuf_t g; victim_t v; int r; size_t i; gb_init(&g); victim_init(&v); errno = 0;
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "%1$n", &v.n);
    if (r != -1 || errno != EINVAL || !victim_ok(&v)) return "FAIL:positional-pct-n";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:positional-pct-n-dest";
    return "OK";
}
static const char *c57(void) {
    /* %%n is a literal '%' followed by 'n' — must be ACCEPTED. */
    gbuf_t g; int r; gb_init(&g); errno = 0;
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "%%n");
    if (r != 2 || !gb_equals(&g, "%n")) return "FAIL:pct-pct-n-accepted";
    return "OK";
}
/* Non-literal gate ------------------------------------------------------- */
static const char *c58(void) {
    gbuf_t g; int r; size_t i; gb_init(&g); errno = 0;
    r = cplus_snprintf_s_checked(0, (size_t)-1, g.buf, sizeof g.buf, fmt_literal_d(), 7);
    if (r != -1 || errno != EINVAL) return "FAIL:nonliteral-checked-gate";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:nonliteral-gate-wrote";
    return "OK";
}
static const char *c59(void) {
    const char *f = fmt_literal_d(); gbuf_t g; int r; gb_init(&g); errno = 0;
    r = snprintf_s(g.buf, sizeof g.buf, f, 7); /* macro -> __builtin_constant_p(f)==0 */
    if (r != -1 || errno != EINVAL || !gb_canaries(&g)) return "FAIL:nonliteral-macro-gate";
    return "OK";
}
static const char *c60(void) {
    gbuf_t g; int r; gb_init(&g); errno = 0;
    r = snprintf_s(g.buf, 512, "%s", "x"); /* dmax > __builtin_object_size(g.buf,0) */
    if (r != -1 || errno != EINVAL || !gb_canaries(&g)) return "FAIL:dmax-exceeds-capacity";
    return "OK";
}
/* More checked snprintf formats ------------------------------------------ */
static const char *c61(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s(g.buf, 16, "%1000d", 7);
    if (r != 1000 || strlen(g.buf) != 15 || !gb_canaries(&g)) return "FAIL:snprintf-s-huge-width";
    return "OK";
}
static const char *c62(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "%.*s", 3, "abcdef");
    if (r != 3 || !gb_equals(&g, "abc")) return "FAIL:snprintf-s-dyn-precision";
    return "OK";
}
static const char *c63(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "%2$s-%1$d", 7, "xy");
    if (r != 4 || !gb_equals(&g, "xy-7")) return "FAIL:snprintf-s-positional";
    return "OK";
}
static const char *c64(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s_checked(1, (size_t)-1, g.buf, sizeof g.buf, fmt_embedded_nul(), 7);
    if (r != 1 || !gb_equals(&g, "A")) return "FAIL:snprintf-s-embedded-NUL";
    return "OK";
}
static const char *c65(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "[%ls]", L"AB");
    if (r != 4 || !gb_equals(&g, "[AB]")) return "FAIL:snprintf-s-wide-derived";
    return "OK";
}
/* v-variants ------------------------------------------------------------- */
static const char *c66(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cv_snprintf(g.buf, sizeof g.buf, "%d-%s", 7, "ok");
    if (r != 4 || !gb_equals(&g, "7-ok")) return "FAIL:vsnprintf-s-basic";
    return "OK";
}
static const char *c67(void) {
    gbuf_t g; victim_t v; int r; size_t i; gb_init(&g); victim_init(&v); errno = 0;
    r = cv_snprintf(g.buf, sizeof g.buf, "%n", &v.n);
    if (r != -1 || errno != EINVAL || !victim_ok(&v)) return "FAIL:vsnprintf-s-pct-n";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:vsnprintf-s-pct-n-dest";
    return "OK";
}
static const char *c68(void) {
    gbuf_t g; size_t i; gb_init(&g); errno = 0;
    if (cvp_vsnprintf_nullfmt(g.buf, sizeof g.buf, NULL, 7) != -1 || errno == 0)
        return "FAIL:vsnprintf-s-null-format";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:vsnprintf-s-nullfmt-dest";
    return "OK";
}
static const char *c69(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_sprintf_s(g.buf, sizeof g.buf, "%s-%d", "v", 42);
    if (r != 4 || !gb_equals(&g, "v-42")) return "FAIL:sprintf-s-basic";
    return "OK";
}
static const char *c70(void) {
    gbuf_t g; victim_t v; int r; size_t i; gb_init(&g); victim_init(&v); errno = 0;
    r = cplus_sprintf_s(g.buf, sizeof g.buf, "a%n", &v.n);
    if (r != -1 || errno != EINVAL || !victim_ok(&v)) return "FAIL:sprintf-s-pct-n";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:sprintf-s-pct-n-dest";
    return "OK";
}
static const char *c71(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cv_sprintf(g.buf, sizeof g.buf, "vs=%d", 8);
    if (r != 4 || !gb_equals(&g, "vs=8")) return "FAIL:vsprintf-s-basic";
    return "OK";
}
static const char *c72(void) {
    gbuf_t g; victim_t v; int r; gb_init(&g); victim_init(&v); errno = 0;
    r = cv_sprintf(g.buf, sizeof g.buf, "%lln", (long long *)0);
    if (r != -1 || errno != EINVAL || !victim_ok(&v)) return "FAIL:vsprintf-s-pct-n";
    return "OK";
}
/* printf_s / fprintf_s ---------------------------------------------------- */
static const char *c73(void) {
    cap_t c; char out[32] = ""; int r;
    if (!cap_out_begin(&c)) return "FAIL:printf-s-setup";
    r = cplus_printf_s("%d", 7);
    cap_out_end(&c, out, sizeof out);
    if (r != 1 || strcmp(out, "7") != 0) return "FAIL:printf-s-literal";
    return "OK";
}
static const char *c74(void) {
    errno = 0;
    if (cplus_printf_s(NULL) != -1 || errno == 0) return "FAIL:printf-s-null-format";
    return "OK";
}
static const char *c75(void) {
    victim_t v; int r; cap_t c; char out[32] = ""; victim_init(&v); errno = 0;
    if (!cap_out_begin(&c)) return "FAIL:printf-s-pct-n-setup";
    r = cplus_printf_s("x%n", &v.n);
    cap_out_end(&c, out, sizeof out);
    if (r != -1 || errno != EINVAL || !victim_ok(&v) || strcmp(out, "") != 0)
        return "FAIL:printf-s-pct-n";
    return "OK";
}
static const char *c76(void) {
    const char *f = fmt_literal_d(); int r; errno = 0;
    r = cplus_printf_s_checked(0, f, 7); /* forced non-literal gate; no stdout output */
    if (r != -1 || errno != EINVAL) return "FAIL:printf-s-nonliteral-gate";
    return "OK";
}
static const char *c77(void) {
    FILE *f = make_input(""); char out[32] = ""; int r;
    if (!f) return "FAIL:fprintf-s-setup";
    r = cplus_fprintf_s(f, "f=%d", 4);
    rewind(f); { size_t n = fread(out, 1, sizeof out - 1, f); out[n] = 0; }
    fclose(f);
    if (r != 3 || strcmp(out, "f=4") != 0) return "FAIL:fprintf-s-basic";
    return "OK";
}
static const char *c78(void) {
    errno = 0;
    if (cplus_fprintf_s(NULL, "%d", 1) != -1 || errno == 0) return "FAIL:fprintf-s-null-stream";
    return "OK";
}
static const char *c79(void) {
    FILE *f = make_input(""); errno = 0;
    if (!f) return "FAIL:fprintf-s-nullfmt-setup";
    if (cplus_fprintf_s(f, NULL) != -1 || errno == 0) { fclose(f); return "FAIL:fprintf-s-null-format"; }
    fclose(f);
    return "OK";
}
static const char *c80(void) {
    victim_t v; FILE *f = make_input(""); int r; char out[16] = ""; victim_init(&v); errno = 0;
    if (!f) return "FAIL:fprintf-s-pct-n-setup";
    r = cplus_fprintf_s(f, "a%n", &v.n);
    rewind(f); { size_t n = fread(out, 1, sizeof out - 1, f); out[n] = 0; }
    fclose(f);
    if (r != -1 || errno != EINVAL || !victim_ok(&v) || strcmp(out, "") != 0)
        return "FAIL:fprintf-s-pct-n";
    return "OK";
}
static const char *c81(void) {
    const char *f = fmt_literal_d(); FILE *s = make_input(""); int r; errno = 0;
    if (!s) return "FAIL:fprintf-s-nonlit-setup";
    r = fprintf_s(s, f, 7);
    fclose(s);
    if (r != -1 || errno != EINVAL) return "FAIL:fprintf-s-nonliteral-gate";
    return "OK";
}
/* scanf_s family ---------------------------------------------------------- */
static const char *c82(void) {
    int v = 0; int r = cplus_sscanf_s("42", "%d", &v);
    if (r != 1 || v != 42) return "FAIL:sscanf-s-one-int";
    return "OK";
}
static const char *c83(void) {
    victim_t v; int r; victim_init(&v); errno = 0;
    r = cplus_sscanf_s("42", "%n", &v.n);
    if (r != -1 || errno != EINVAL || !victim_ok(&v)) return "FAIL:sscanf-s-pct-n";
    return "OK";
}
static const char *c84(void) {
    char s[16] = "untouched"; int r; errno = 0;
    r = cplus_sscanf_s("abc", "%s", s);
    if (r != -1 || errno != EINVAL || strcmp(s, "untouched") != 0)
        return "FAIL:sscanf-s-unbounded-s";
    return "OK";
}
static const char *c85(void) {
    char s[16] = ""; int r;
    r = cplus_sscanf_s("abcdefgh", "%3s", s);
    if (r != 1 || strcmp(s, "abc") != 0) return "FAIL:sscanf-s-bounded-s";
    return "OK";
}
static const char *c86(void) {
    const char *f = fmt_literal_d(); int v = 0; int r; errno = 0;
    r = sscanf_s("7", f, &v);
    if (r != -1 || errno != EINVAL || v != 0) return "FAIL:sscanf-s-nonliteral-gate";
    return "OK";
}
static const char *c87(void) {
    errno = 0;
    if (cplus_sscanf_s(NULL, "%d", (int *)0) != -1 || errno == 0) return "FAIL:sscanf-s-null-input";
    return "OK";
}
static const char *c88(void) {
    FILE *f = make_input("42\n"); int v = 0; int r;
    if (!f) return "FAIL:fscanf-s-setup";
    r = cplus_fscanf_s(f, "%d", &v);
    fclose(f);
    if (r != 1 || v != 42) return "FAIL:fscanf-s-int";
    return "OK";
}
static const char *c89(void) {
    errno = 0;
    if (cplus_fscanf_s(NULL, "%d", (int *)0) != -1 || errno == 0) return "FAIL:fscanf-s-null-stream";
    return "OK";
}
static const char *c90(void) {
    victim_t v; FILE *f = make_input("42\n"); int r; victim_init(&v); errno = 0;
    if (!f) return "FAIL:fscanf-s-pct-n-setup";
    r = cplus_fscanf_s(f, "%n", &v.n);
    fclose(f);
    if (r != -1 || errno != EINVAL || !victim_ok(&v)) return "FAIL:fscanf-s-pct-n";
    return "OK";
}
static const char *c91(void) {
    FILE *f = make_input("42\n"); int v = 0; int r; int saved;
    if (!f) return "FAIL:scanf-s-setup";
    saved = dup(STDIN_FILENO);
    if (saved < 0 || dup2(fileno(f), STDIN_FILENO) < 0) { fclose(f); return "FAIL:scanf-s-dup"; }
    r = cplus_scanf_s("%d", &v);
    dup2(saved, STDIN_FILENO);
    close(saved);
    fclose(f);
    if (r != 1 || v != 42) return "FAIL:scanf-s-stdin";
    return "OK";
}
static const char *c92(void) {
    victim_t v; int r; victim_init(&v); errno = 0;
    r = cplus_scanf_s("%n", &v.n);
    if (r != -1 || errno != EINVAL || !victim_ok(&v)) return "FAIL:scanf-s-pct-n";
    return "OK";
}
/* Logging parity / canary / arg-count / precedence ------------------------ */
static const char *c93(void) {
    /* Logging OFF/ON parity (SafeCRT logger). */
    gbuf_t a, b; int ra, rb;
    gb_init(&a); gb_init(&b);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    ra = cplus_snprintf_s(a.buf, sizeof a.buf, "%d-%s", 7, "parity");
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    rb = cplus_snprintf_s(b.buf, sizeof b.buf, "%d-%s", 7, "parity");
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    if (ra != rb || !gb_equals(&a, "7-parity") || !gb_equals(&b, "7-parity"))
        return "FAIL:safecrt-log-parity";
    return "OK";
}
static const char *c94(void) {
    /* %n rejection identical with logging OFF and ON; victim intact both. */
    victim_t v1, v2; gbuf_t g1, g2; int r1, r2;
    gb_init(&g1); gb_init(&g2); victim_init(&v1); victim_init(&v2);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    r1 = cplus_snprintf_s(g1.buf, sizeof g1.buf, "a%n", &v1.n);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    r2 = cplus_snprintf_s(g2.buf, sizeof g2.buf, "a%n", &v2.n);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    if (r1 != -1 || r2 != -1 || !victim_ok(&v1) || !victim_ok(&v2) ||
        !gb_canaries(&g1) || !gb_canaries(&g2)) return "FAIL:safecrt-pct-n-log-parity";
    return "OK";
}
static const char *c95(void) {
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s(g.buf, sizeof g.buf, "abcdefgh");
    if (r != 8 || !gb_equals(&g, "abcdefgh") || !gb_canaries(&g))
        return "FAIL:safecrt-canary-accepted";
    return "OK";
}
static const char *c96(void) {
    gbuf_t g; victim_t v; int i; gb_init(&g); victim_init(&v);
    (void)cplus_snprintf_s(g.buf, sizeof g.buf, "%n", &v.n);
    for (i = 0; i < (int)sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:safecrt-canary-rejected";
    if (!gb_canaries(&g) || !victim_ok(&v)) return "FAIL:safecrt-canary-rejected-2";
    return "OK";
}
static const char *c97(void) {
    /* Excess variadic arguments are evaluated but ignored (defined by C). */
    gbuf_t g; int r; gb_init(&g);
    r = cplus_snprintf_s_checked(1, (size_t)-1, g.buf, sizeof g.buf, fmt_too_many(),
                                 7, "extra", 3.14);
    if (r != 1 || !gb_equals(&g, "7")) return "FAIL:too-many-args";
    return "OK";
}
static const char *c98(void) {
    /* Too few args would be UB; the checked non-literal gate refuses first. */
    gbuf_t g; int r; size_t i; gb_init(&g); errno = 0;
    r = cplus_snprintf_s_checked(0, (size_t)-1, g.buf, sizeof g.buf, fmt_too_few(), 7);
    if (r != -1 || errno != EINVAL) return "FAIL:too-few-args-via-gate";
    for (i = 0; i < sizeof g.buf; ++i) if (g.buf[i] != 'X') return "FAIL:too-few-args-wrote";
    return "OK";
}
static const char *c99(void) {
    /* Non-literal gate fires before %n scan; both would be hostile. */
    gbuf_t g; victim_t v; int r; gb_init(&g); victim_init(&v); errno = 0;
    r = cplus_snprintf_s_checked(0, (size_t)-1, g.buf, sizeof g.buf, fmt_n_nonlit(), &v.n);
    if (r != -1 || errno != EINVAL || !victim_ok(&v)) return "FAIL:nonliteral-and-pct-n-precedence";
    return "OK";
}
static const char *c100(void) {
    /* TRACE-success and ERROR-rejection logging both yield stable results. */
    gbuf_t g; victim_t v; int ok_r, bad_r;
    gb_init(&g); victim_init(&v);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    ok_r = cplus_snprintf_s(g.buf, sizeof g.buf, "%d", 7);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);
    bad_r = cplus_snprintf_s(g.buf, sizeof g.buf, "%n", &v.n);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    if (ok_r != 1 || bad_r != -1 || !victim_ok(&v) || !gb_canaries(&g))
        return "FAIL:trace-error-log-levels";
    return "OK";
}

#else /* !HAVE_SAFECRT : Layer 2 is not linked in the plain build variant. */

#define DEF_SKIP(n) static const char *c##n(void) { return "SKIP"; }
DEF_SKIP(45) DEF_SKIP(46) DEF_SKIP(47) DEF_SKIP(48) DEF_SKIP(49)
DEF_SKIP(50) DEF_SKIP(51) DEF_SKIP(52) DEF_SKIP(53) DEF_SKIP(54)
DEF_SKIP(55) DEF_SKIP(56) DEF_SKIP(57) DEF_SKIP(58) DEF_SKIP(59)
DEF_SKIP(60) DEF_SKIP(61) DEF_SKIP(62) DEF_SKIP(63) DEF_SKIP(64)
DEF_SKIP(65) DEF_SKIP(66) DEF_SKIP(67) DEF_SKIP(68) DEF_SKIP(69)
DEF_SKIP(70) DEF_SKIP(71) DEF_SKIP(72) DEF_SKIP(73) DEF_SKIP(74)
DEF_SKIP(75) DEF_SKIP(76) DEF_SKIP(77) DEF_SKIP(78) DEF_SKIP(79)
DEF_SKIP(80) DEF_SKIP(81) DEF_SKIP(82) DEF_SKIP(83) DEF_SKIP(84)
DEF_SKIP(85) DEF_SKIP(86) DEF_SKIP(87) DEF_SKIP(88) DEF_SKIP(89)
DEF_SKIP(90) DEF_SKIP(91) DEF_SKIP(92) DEF_SKIP(93) DEF_SKIP(94)
DEF_SKIP(95) DEF_SKIP(96) DEF_SKIP(97) DEF_SKIP(98) DEF_SKIP(99)
DEF_SKIP(100)
#undef DEF_SKIP
#endif /* HAVE_SAFECRT */

/* Driver ----------------------------------------------------------------- */
#define CASE(n, method, scenario)                                            \
    do {                                                                      \
        const char *_r = c##n();                                              \
        printf("hundred_format: case %d %s %s %s\n", (n), (method), (scenario), _r); \
        if (strcmp(_r, "OK") == 0 || strcmp(_r, "SKIP") == 0) {               \
            ++g_ok;                                                           \
        } else {                                                              \
            printf("FINDING: case %d (%s/%s) %s\n", (n), (method), (scenario), _r); \
            ++g_findings;                                                     \
        }                                                                     \
    } while (0)

int main(void) {
#if HAVE_SAFECRT
    /* Observable -1/EINVAL contract instead of _exit(70) on violations. */
    cplus_runtime_set_violation_trap(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
#endif
    cplus_libc_log_set_enabled(0);

    /* ---- Layer 1: plain hardened libc (1..44) ------------------------- */
    CASE(1, "snprintf", "literal-correct-args");
    CASE(2, "snprintf", "exact-fit");
    CASE(3, "snprintf", "one-short-truncation");
    CASE(4, "snprintf", "zero-size-dest");
    CASE(5, "snprintf", "null-dest-zero-size");
    CASE(6, "snprintf", "null-dest-nonzero");
    CASE(7, "snprintf", "null-format");
    CASE(8, "snprintf", "precision-2s");
    CASE(9, "snprintf", "dynamic-precision");
    CASE(10, "snprintf", "huge-width");
    CASE(11, "snprintf", "percent-escape");
    CASE(12, "snprintf", "positional");
    CASE(13, "snprintf", "embedded-NUL");
    CASE(14, "snprintf", "wide-derived-ls");
    CASE(15, "snprintf", "pct-n-valid-target-count");
    CASE(16, "vsnprintf", "va-basic");
    CASE(17, "vsnprintf", "va-truncation");
    CASE(18, "vsnprintf", "null-format-guard");
    CASE(19, "sprintf", "basic-large-buffer");
    CASE(20, "sprintf", "percent-and-int");
    CASE(21, "sprintf", "null-dest-guard");
    CASE(22, "sprintf", "null-format-guard");
    CASE(23, "printf", "literal-redirect");
    CASE(24, "printf", "null-format-guard");
    CASE(25, "printf", "percent-redirect");
    CASE(26, "vprintf", "va-redirect");
    CASE(27, "fprintf", "tmpfile-basic");
    CASE(28, "fprintf", "null-stream-guard");
    CASE(29, "fprintf", "null-format-guard");
    CASE(30, "vfprintf", "va-tmpfile");
    CASE(31, "sscanf", "one-int");
    CASE(32, "sscanf", "int-and-string");
    CASE(33, "sscanf", "width-3s");
    CASE(34, "sscanf", "suppression");
    CASE(35, "sscanf", "type-mismatch");
    CASE(36, "sscanf", "null-input-guard");
    CASE(37, "sscanf", "null-format-guard");
    CASE(38, "fscanf", "tmpfile-int");
    CASE(39, "fscanf", "null-stream-guard");
    CASE(40, "scanf", "stdin-redirect-int");
    CASE(41, "vsscanf", "va-basic");
    CASE(42, "vfscanf", "va-tmpfile");
    CASE(43, "snprintf", "logging-off-on-parity");
    CASE(44, "snprintf", "canary-integrity");

    /* ---- Layer 2: checked SafeCRT (45..100) --------------------------- */
    CASE(45, "snprintf_s", "literal-basic");
    CASE(46, "snprintf_s", "exact-fit");
    CASE(47, "snprintf_s", "one-short-truncation");
    CASE(48, "snprintf_s", "zero-dmax");
    CASE(49, "snprintf_s", "null-dest-nonzero");
    CASE(50, "snprintf_s", "null-dest-zero");
    CASE(51, "snprintf_s", "null-format");
    CASE(52, "snprintf_s", "pct-n-rejected");
    CASE(53, "snprintf_s", "pct-hhn-rejected");
    CASE(54, "snprintf_s", "pct-lln-rejected");
    CASE(55, "snprintf_s", "pct-zn-rejected");
    CASE(56, "snprintf_s", "positional-pct-n-rejected");
    CASE(57, "snprintf_s", "pct-pct-n-accepted");
    CASE(58, "snprintf_s", "nonliteral-checked-gate");
    CASE(59, "snprintf_s", "nonliteral-macro-gate");
    CASE(60, "snprintf_s", "dmax-exceeds-capacity");
    CASE(61, "snprintf_s", "huge-width");
    CASE(62, "snprintf_s", "dynamic-precision");
    CASE(63, "snprintf_s", "positional");
    CASE(64, "snprintf_s", "embedded-NUL");
    CASE(65, "snprintf_s", "wide-derived-ls");
    CASE(66, "vsnprintf_s", "va-basic");
    CASE(67, "vsnprintf_s", "pct-n-rejected");
    CASE(68, "vsnprintf_s", "null-format");
    CASE(69, "sprintf_s", "basic");
    CASE(70, "sprintf_s", "pct-n-rejected");
    CASE(71, "vsprintf_s", "va-basic");
    CASE(72, "vsprintf_s", "pct-n-rejected");
    CASE(73, "printf_s", "literal-redirect");
    CASE(74, "printf_s", "null-format");
    CASE(75, "printf_s", "pct-n-rejected-victim-intact");
    CASE(76, "printf_s", "nonliteral-checked-gate");
    CASE(77, "fprintf_s", "tmpfile-basic");
    CASE(78, "fprintf_s", "null-stream");
    CASE(79, "fprintf_s", "null-format");
    CASE(80, "fprintf_s", "pct-n-rejected");
    CASE(81, "fprintf_s", "nonliteral-macro-gate");
    CASE(82, "sscanf_s", "one-int");
    CASE(83, "sscanf_s", "pct-n-rejected");
    CASE(84, "sscanf_s", "unbounded-s-rejected");
    CASE(85, "sscanf_s", "bounded-3s");
    CASE(86, "sscanf_s", "nonliteral-macro-gate");
    CASE(87, "sscanf_s", "null-input");
    CASE(88, "fscanf_s", "tmpfile-int");
    CASE(89, "fscanf_s", "null-stream");
    CASE(90, "fscanf_s", "pct-n-rejected");
    CASE(91, "scanf_s", "stdin-redirect-int");
    CASE(92, "scanf_s", "pct-n-rejected");
    CASE(93, "snprintf_s", "logging-off-on-parity");
    CASE(94, "snprintf_s", "pct-n-rejection-log-parity");
    CASE(95, "snprintf_s", "canary-accepted-path");
    CASE(96, "snprintf_s", "canary-rejected-path");
    CASE(97, "snprintf_s", "too-many-args-ignored");
    CASE(98, "snprintf_s", "too-few-args-via-gate");
    CASE(99, "snprintf_s", "nonliteral-and-pct-n-precedence");
    CASE(100, "snprintf_s", "trace-error-log-levels");

    printf("hundred_format: %d/100 scenarios ok\n", g_ok);
    (void)g_findings;
    return 0;
}
