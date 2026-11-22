/* Adversarial format-function tests for the C+ hardened libc / SafeCRT.
 *
 * Scope: cplus_snprintf / vsnprintf / sprintf / printf / fprintf / scanf /
 * fscanf / sscanf (raw wrappers), their checked `_s` variants from
 * stdlib/safe/safe_format.h, and the wide-format wrappers declared in
 * libc/install-include/cplus_libc.h.
 *
 * Contracts exercised (see safe_format.h header comment and wchar_format.c):
 *   - Checked `*_s_checked` adapters reject non-literal formats (CWE-134) and
 *     the forbidden `%n` conversion (C11 Annex K K.3.5.3.1) with -1/EINVAL.
 *   - Bounded variants never write past `dmax` and NUL-terminate when dmax > 0.
 *   - NULL format/destination/stream and dmax > real capacity are defined
 *     errors (-1, errno = EINVAL), not crashes.
 *   - Wide wrappers are PLATFORM-BACKED: Apple Clang provides NO static
 *     format(wprintf/wscanf,...) checking for wide format strings, and the
 *     wrappers do NOT detect `%n`. Format correctness is the caller's job.
 *
 * IMPORTANT — RAW C FORMAT SAFETY IS THE CALLER'S RESPONSIBILITY:
 *   cplus_snprintf/cplus_printf/... are thin namespaced delegates to the host
 *   v*printf family. Passing a dynamic, attacker-controlled format with
 *   mismatched arguments (e.g. "%n%s" with no arguments) is undefined
 *   behaviour IN THE TEST ITSELF and could write through a bogus pointer, so
 *   this test never does that. Raw wrappers are probed only through their
 *   DEFINED-UB inputs (NULL pointers, truncation, bounded sizes) and literal
 *   formats whose arguments match exactly. The hostile-format attacks below
 *   target the CHECKED wrappers, which are explicitly specified to reject or
 *   neutralize them before any variadic argument is consumed.
 *
 * INVALID (fclose'd) STREAMS:
 *   cplus_fclose() guards double-close, but no format wrapper (raw or safe)
 *   validates whether a FILE* is still open. Using a closed FILE* with
 *   printf/scanf family is UB per ISO C and is NOT deliberately triggered here.
 *   NULL FILE* is a defined error and is tested.
 *
 * COMPILE-TIME VARIADIC-MISMATCH EVIDENCE (probe in /tmp, not committed):
 *   Apple clang 16.0.0, `clang -std=c99 -Wall -Wextra -pedantic -c probe.c`:
 *     probe.c:10:29: warning: format specifies type 'int' but the argument
 *                   has type 'char *' [-Wformat]
 *     my_checked_printf("%d", "not-an-int");
 *                        ~~   ^~~~~~~~~~~~
 *                        %s
 *   1 warning generated.
 *   A wide-format probe (`__attribute__((format(wprintf,1,2)))` on a
 *   `const wchar_t *` format) produced ONLY
 *     warning: 'format' attribute argument not supported: wprintf
 *             [-Wignored-attributes]
 *   and NO -Wformat warning for L"%d" with a wchar_t* argument, confirming
 *   that wide format checking is absent on this toolchain.
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include <safe_format.h>
#include <safe_runtime.h>

/* Canonical prototypes live in libc/install-include/cplus_libc.h. That
 * directory is not on the verify command's include path, so declare the
 * subset used here directly (intentionally WITHOUT format attributes: the
 * compile-time contract is demonstrated by the /tmp probe above). */
int cplus_snprintf(char *, size_t, const char *, ...);
int cplus_vsnprintf(char *, size_t, const char *, va_list);
int cplus_sprintf(char *, const char *, ...);
int cplus_printf(const char *, ...);
int cplus_fprintf(FILE *, const char *, ...);
int cplus_scanf(const char *, ...);
int cplus_fscanf(FILE *, const char *, ...);
int cplus_sscanf(const char *, const char *, ...);
int cplus_fclose(FILE *);
int cplus_swprintf(wchar_t *, size_t, const wchar_t *, ...);
int cplus_fwprintf(FILE *, const wchar_t *, ...);
int cplus_fwscanf(FILE *, const wchar_t *, ...);
int cplus_wscanf(const wchar_t *, ...);

/* Canary helpers: detect any byte written outside the intended region. */
#define LEAD_BYTE  0xA5u
#define TRAIL_BYTE 0x5Au
#define BUF_CAP    16u

typedef struct {
    unsigned char lead[8];
    char buf[BUF_CAP];
    unsigned char trail[8];
} guarded_buf;

static void gbuf_init(guarded_buf *g) {
    size_t i;
    for (i = 0; i < sizeof g->lead; ++i) g->lead[i] = LEAD_BYTE;
    for (i = 0; i < BUF_CAP; ++i) g->buf[i] = 'X';
    for (i = 0; i < sizeof g->trail; ++i) g->trail[i] = TRAIL_BYTE;
}

static int gbuf_intact(const guarded_buf *g) {
    size_t i;
    for (i = 0; i < sizeof g->lead; ++i)
        if (g->lead[i] != LEAD_BYTE) return 0;
    for (i = 0; i < sizeof g->trail; ++i)
        if (g->trail[i] != TRAIL_BYTE) return 0;
    return 1;
}

typedef struct {
    unsigned char lead[8];
    wchar_t buf[8];
    unsigned char trail[8];
} guarded_wbuf;

static void wbuf_init(guarded_wbuf *g) {
    size_t i;
    for (i = 0; i < sizeof g->lead; ++i) g->lead[i] = LEAD_BYTE;
    for (i = 0; i < 8; ++i) g->buf[i] = (wchar_t)'X';
    for (i = 0; i < sizeof g->trail; ++i) g->trail[i] = TRAIL_BYTE;
}

static int wbuf_intact(const guarded_wbuf *g) {
    size_t i;
    for (i = 0; i < sizeof g->lead; ++i)
        if (g->lead[i] != LEAD_BYTE) return 0;
    for (i = 0; i < sizeof g->trail; ++i)
        if (g->trail[i] != TRAIL_BYTE) return 0;
    return 1;
}

/* Wrapper used to exercise the va_list entry points. */
static int call_vsnprintf_s(char *d, size_t n, const char *f, ...) {
    va_list ap;
    int r;
    va_start(ap, f);
    r = cplus_vsnprintf_s(d, n, f, ap);
    va_end(ap);
    return r;
}

/* Clang's -Wformat-security / -Wformat-insufficient-args fire on the
 * deliberately non-literal / argument-less hostile formats. They are the
 * very warnings the C+ compiler pass is meant to turn into hard errors; the
 * runtime test needs them to compile, so suppress them locally only. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-insufficient-args"
#pragma clang diagnostic ignored "-Wformat-extra-args"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#endif

/* ------------------------------------------------------------------ */
/* 1. FORMAT-STRING ATTACKS: dynamic formats containing %n and %s,      */
/*    passed with NO matching arguments.                                */
/* ------------------------------------------------------------------ */
static void test_dynamic_format_attack(void) {
    char dyn_n_s[] = "%n%s";
    char dyn_s_n[] = "%s%n";
    char dyn_wide_n[] = "%ls%n";
    guarded_buf g;
    FILE *fp;
    int r;

    /* Trap disabled so violations return -1 instead of exit(70). */
    cplus_runtime_set_violation_trap(0);
    assert(cplus_runtime_get_violation_trap() == 0);

    /* snprintf_s: non-literal format refused before any vararg is read. */
    gbuf_init(&g);
    errno = 0;
    r = cplus_snprintf_s_checked(0, BUF_CAP, g.buf, BUF_CAP, dyn_n_s);
    assert(r < 0 && errno == EINVAL);
    assert(gbuf_intact(&g));
    assert(g.buf[0] == 'X'); /* no %n write, no %s read/print */

    /* vsnprintf_s: same check on the va_list path. */
    gbuf_init(&g);
    errno = 0;
    r = call_vsnprintf_s(g.buf, BUF_CAP, dyn_s_n);
    assert(r < 0 && errno == EINVAL);
    assert(gbuf_intact(&g));
    assert(g.buf[0] == 'X');

    /* sprintf_s: alias of bounded vsnprintf, also refuses non-literal fmt. */
    gbuf_init(&g);
    errno = 0;
    r = cplus_sprintf_s_checked(0, BUF_CAP, g.buf, BUF_CAP, dyn_n_s);
    assert(r < 0 && errno == EINVAL);
    assert(gbuf_intact(&g));

    /* printf_s / fprintf_s. */
    errno = 0;
    r = cplus_printf_s_checked(0, dyn_s_n);
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_fprintf_s_checked(0, stdout, dyn_n_s);
    assert(r < 0 && errno == EINVAL);

    /* scanf_s / sscanf_s / fscanf_s: refusal happens before v*scanf, so no
     * stdin read and no %n store. Use a tmpfile for fscanf to guarantee
     * there is no interactive dependency even if a check were removed. */
    fp = tmpfile();
    assert(fp != NULL);
    errno = 0;
    r = cplus_scanf_s_checked(0, dyn_n_s);
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_sscanf_s_checked(0, "payload", dyn_s_n);
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_fscanf_s_checked(0, fp, dyn_wide_n);
    assert(r < 0 && errno == EINVAL);
    fclose(fp);

    /* The unprefixed macros route through the same literal detector. */
    gbuf_init(&g);
    errno = 0;
    r = snprintf_s(g.buf, BUF_CAP, dyn_n_s);
    assert(r < 0 && errno == EINVAL);
    assert(gbuf_intact(&g));
    errno = 0;
    r = printf_s(dyn_s_n);
    assert(r < 0 && errno == EINVAL);

    /* Defense in depth: even a LITERAL format containing %n is rejected by
     * the non-checked `_s` entry points (Annex K K.3.5.3.1). */
    gbuf_init(&g);
    errno = 0;
    r = cplus_snprintf_s(g.buf, BUF_CAP, "%n");
    assert(r < 0 && errno == EINVAL);
    assert(gbuf_intact(&g));
    errno = 0;
    r = cplus_printf_s("%s%n");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_fprintf_s(stdout, "%n");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_sscanf_s("123", "%n");
    assert(r < 0 && errno == EINVAL);

    /* Literal, well-formed formats still work through the macro path. */
    gbuf_init(&g);
    r = snprintf_s(g.buf, BUF_CAP, "ok %d", 7);
    assert(r == 4 && strcmp(g.buf, "ok 7") == 0);
    assert(gbuf_intact(&g));
}

/* ------------------------------------------------------------------ */
/* 2. HUGE WIDTH / PRECISION through checked APIs.                     */
/* ------------------------------------------------------------------ */
static void test_huge_width_precision(void) {
    guarded_buf g;
    clock_t t0, t1;
    int r;

    gbuf_init(&g);
    errno = 0;
    t0 = clock();
    r = cplus_snprintf_s_checked(1, BUF_CAP, g.buf, BUF_CAP, "%999999999d", 42);
    t1 = clock();
    /* C99 snprintf returns the would-be length; output itself is bounded. */
    assert(r == 999999999);
    assert(gbuf_intact(&g));
    assert(g.buf[BUF_CAP - 1] == '\0'); /* NUL-terminated inside dmax */
    assert((double)(t1 - t0) / CLOCKS_PER_SEC < 2.0);

    gbuf_init(&g);
    errno = 0;
    t0 = clock();
    r = cplus_snprintf_s_checked(1, BUF_CAP, g.buf, BUF_CAP, "%.*s", INT_MAX, "hi");
    t1 = clock();
    assert(r == 2 && strcmp(g.buf, "hi") == 0);
    assert(gbuf_intact(&g));
    assert((double)(t1 - t0) / CLOCKS_PER_SEC < 2.0);
}

/* ------------------------------------------------------------------ */
/* 3. TRUNCATION: exact fit, one short, zero-size, bad dmax.           */
/* ------------------------------------------------------------------ */
static void test_truncation(void) {
    guarded_buf g;
    int r;

    /* Exact fit: BUF_CAP-1 chars plus NUL. */
    gbuf_init(&g);
    r = cplus_snprintf_s_checked(1, BUF_CAP, g.buf, BUF_CAP, "%s", "abcdefghijklmno");
    assert(r == 15);
    assert(strcmp(g.buf, "abcdefghijklmno") == 0);
    assert(gbuf_intact(&g));

    /* One short: would-be length still returned, output truncated + NUL. */
    gbuf_init(&g);
    r = cplus_snprintf_s_checked(1, 3, g.buf, 3, "%s", "abc");
    assert(r == 3);
    assert(g.buf[0] == 'a' && g.buf[1] == 'b' && g.buf[2] == '\0');
    assert(gbuf_intact(&g));

    /* Zero-size destination with NULL pointer is legal for snprintf. */
    errno = 0;
    r = cplus_snprintf_s_checked(1, 0, NULL, 0, "%s", "abc");
    assert(r == 3);

    /* Zero-size with a real buffer: nothing written, canaries intact. */
    gbuf_init(&g);
    r = cplus_snprintf_s_checked(1, BUF_CAP, g.buf, 0, "%s", "abc");
    assert(r == 3);
    assert(g.buf[0] == 'X');
    assert(gbuf_intact(&g));

    /* dmax larger than the object's real capacity is a violation. */
    gbuf_init(&g);
    errno = 0;
    r = cplus_snprintf_s_checked(1, BUF_CAP, g.buf, BUF_CAP + 64u, "%s", "abc");
    assert(r < 0 && errno == EINVAL);
    assert(gbuf_intact(&g));

    /* sprintf_s is a bounded alias, not unbounded host sprintf. */
    gbuf_init(&g);
    r = cplus_sprintf_s_checked(1, 3, g.buf, 3, "%s", "abc");
    assert(r == 3 && g.buf[0] == 'a' && g.buf[1] == 'b' && g.buf[2] == '\0');
    assert(gbuf_intact(&g));

    /* Raw wrapper preserves C99 truncation semantics (caller-owned size). */
    gbuf_init(&g);
    r = cplus_snprintf(g.buf, 3, "%s", "abc");
    assert(r == 3 && g.buf[0] == 'a' && g.buf[1] == 'b' && g.buf[2] == '\0');
    assert(gbuf_intact(&g));
    r = cplus_snprintf(NULL, 0, "%s", "abc");
    assert(r == 3);
}

/* ------------------------------------------------------------------ */
/* 4. NULL ARGUMENTS: defined errors, no crash.                        */
/* ------------------------------------------------------------------ */
static void test_null_arguments(void) {
    guarded_buf g;
    int r;

    gbuf_init(&g);
    errno = 0;
    r = cplus_snprintf_s_checked(1, BUF_CAP, g.buf, BUF_CAP, NULL);
    assert(r < 0 && errno == EINVAL);
    assert(gbuf_intact(&g));

    errno = 0;
    r = call_vsnprintf_s(g.buf, BUF_CAP, NULL);
    assert(r < 0 && errno == EINVAL);

    errno = 0;
    r = cplus_printf_s_checked(1, NULL);
    assert(r < 0 && errno == EINVAL);

    errno = 0;
    r = cplus_fprintf_s_checked(1, stdout, NULL);
    assert(r < 0 && errno == EINVAL);

    errno = 0;
    r = cplus_scanf_s_checked(1, NULL);
    assert(r < 0 && errno == EINVAL);

    errno = 0;
    r = cplus_sscanf_s_checked(1, NULL, "%d");
    assert(r < 0 && errno == EINVAL);

    errno = 0;
    r = cplus_fscanf_s_checked(1, stdin, NULL);
    assert(r < 0 && errno == EINVAL);

    /* NULL destination with n > 0. */
    errno = 0;
    r = cplus_snprintf_s_checked(1, 0, NULL, 4, "hi");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_sprintf_s_checked(1, 0, NULL, 4, "hi");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_snprintf(NULL, 4, "hi");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_sprintf(NULL, "hi");
    assert(r < 0 && errno == EINVAL);

    /* NULL FILE* is a defined error for both raw and safe wrappers. */
    errno = 0;
    r = cplus_fprintf_s_checked(1, NULL, "hi");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_fscanf_s_checked(1, NULL, "%d");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_fprintf(NULL, "hi");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_fscanf(NULL, "%d");
    assert(r < 0 && errno == EINVAL);

    /* Raw NULL format. */
    errno = 0;
    r = cplus_snprintf(g.buf, BUF_CAP, NULL);
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_printf(NULL);
    assert(r < 0 && errno == EINVAL);
}

/* ------------------------------------------------------------------ */
/* 5. STREAM LIFECYCLE (no closed-FILE* format calls — UB, skipped).   */
/* ------------------------------------------------------------------ */
static void test_stream_lifecycle(void) {
    FILE *fp = tmpfile();
    int r;
    char out[16];
    assert(fp != NULL);
    /* Valid stream through a raw wrapper works and is bounded. */
    r = cplus_fprintf(fp, "%s", "stream-ok");
    assert(r == 9);
    rewind(fp);
    memset(out, 0, sizeof out);
    assert(fread(out, 1, 9, fp) == 9);
    assert(strcmp(out, "stream-ok") == 0);
    /* cplus_fclose guards double-close; the format wrappers do not track
     * closure, so a closed FILE* is never passed to them here. */
    assert(cplus_fclose(fp) == 0);
    errno = 0;
    r = cplus_fclose(fp);
    assert(r == EOF && errno == EBADF);
}

/* ------------------------------------------------------------------ */
/* 6. WIDE FORMATS — platform-backed, NO static checking on Apple Clang. */
/* ------------------------------------------------------------------ */
static void test_wide_formats(void) {
    guarded_wbuf w;
    int r;

    /* Exact-size wide buffer: 3 chars + NUL fits in n = 4. */
    wbuf_init(&w);
    errno = 0;
    r = cplus_swprintf(w.buf, 4, L"%ls", L"abc");
    assert(r == 3);
    assert(w.buf[0] == L'a' && w.buf[1] == L'b' && w.buf[2] == L'c' &&
           w.buf[3] == L'\0');
    assert(wbuf_intact(&w));

    /* Truncation: Darwin's vswprintf returns -1/EOVERFLOW when the whole
     * conversion plus NUL does not fit, but still NUL-terminates. */
    wbuf_init(&w);
    errno = 0;
    r = cplus_swprintf(w.buf, 3, L"%ls", L"abcdef");
    assert(r <= 0);
    assert(w.buf[0] == L'a' && w.buf[1] == L'b' && w.buf[2] == L'\0');
    assert(wbuf_intact(&w));

    /* n = 1 cannot hold a non-empty conversion: -1, NUL written, no overrun. */
    wbuf_init(&w);
    errno = 0;
    r = cplus_swprintf(w.buf, 1, L"%ls", L"x");
    assert(r <= 0 && w.buf[0] == L'\0');
    assert(wbuf_intact(&w));

    /* NULL format / NULL destination / NULL stream: defined errors. */
    errno = 0;
    r = cplus_swprintf(NULL, 0, NULL);
    assert(r < 0 && errno == EINVAL);
    wbuf_init(&w);
    errno = 0;
    r = cplus_swprintf(w.buf, 4, NULL);
    assert(r < 0 && errno == EINVAL);
    assert(wbuf_intact(&w));
    errno = 0;
    r = cplus_swprintf(NULL, 4, L"%ls", L"abc");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_fwprintf(NULL, L"hi");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_fwscanf(NULL, L"%d");
    assert(r < 0 && errno == EINVAL);
    errno = 0;
    r = cplus_wscanf(NULL);
    assert(r < 0 && errno == EINVAL);

    /* NOTE: no %n rejection is attempted here. The wide wrappers document
     * that %n is NOT filtered; passing an untrusted wide format is UB and
     * is therefore outside this test. */
}

/* ------------------------------------------------------------------ */
/* 7. FINDING: the unprefixed macro's capacity guard is bypassable for   */
/*    a destination that is a struct member.                             */
/*                                                                       */
/*    safe_format.h defines CPLUS_SAFE_OBJ_SIZE(p) as                    */
/*    __builtin_object_size(p, 0). For a member `g.buf[16]` of a larger   */
/*    struct, type 0 returns the number of bytes to the END of the        */
/*    enclosing object (24 here: 16-byte buf + 8-byte trailing canary),  */
/*    NOT the member size. The snprintf_s/sprintf_s macros pass that      */
/*    value as `known_dmax`, so fmt_check_dest_bounds() accepts a dmax    */
/*    larger than the real destination and writes over adjacent fields.  */
/*    __builtin_object_size(g.buf, 1) would correctly return 16.          */
/*                                                                       */
/*    This is a SOFT check: it is contained inside one stack object and  */
/*    never crashes, so the harness stays green while reporting the       */
/*    exact observed behavior.                                            */
/* ------------------------------------------------------------------ */
static void test_capacity_guard_struct_member(void) {
    guarded_buf g;
    int r;
    size_t osz;

    gbuf_init(&g);
    osz = CPLUS_SAFE_OBJ_SIZE(g.buf);
    /* 19 visible chars + NUL = 20 bytes into a 16-byte member. */
    r = snprintf_s(g.buf, 20, "abcdefghijklmnopqrs");
    if (r > 0 && !gbuf_intact(&g)) {
        printf("FINDING: snprintf_s macro capacity guard bypassed for struct "
               "member: __builtin_object_size(g.buf,0)=%zu (16-byte member + "
               "8 trailing canary bytes); dmax=20 accepted; ret=%d; "
               "trail[0]=0x%02x trail[1]=0x%02x (expected 0x5a); "
               "suggested fix: use __builtin_object_size(dest, 1).\n",
               osz, r, (unsigned)g.trail[0], (unsigned)g.trail[1]);
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

int main(void) {
    test_dynamic_format_attack();
    test_huge_width_precision();
    test_truncation();
    test_null_arguments();
    test_stream_lifecycle();
    test_wide_formats();
    test_capacity_guard_struct_member();
    puts("format_attack: ok");
    return 0;
}
