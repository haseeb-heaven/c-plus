#ifndef F100_IO_COMMON_H
#define F100_IO_COMMON_H
/*
 * Shared helpers for deterministic I/O / numeric-parser fuzz configs
 * F-051..F-060.  All fixtures are generated in-process from a fixed-seed LCG
 * or from a fixed byte template; nothing depends on the host environment
 * except the temp-file directory (TMPDIR, default /tmp) and the PID in the
 * temp-file name.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_string.h"
#include "safe_log.h"
#include "safe_runtime.h"
#include "cplus_libc.h"

#define F100_LCG_MUL UINT32_C(1664525)
#define F100_LCG_ADD UINT32_C(1013904223)
#define F100_LCG_SEED UINT32_C(0xF10010DE)

static uint32_t g_f100_state = F100_LCG_SEED;
static unsigned long g_f100_ops;
static unsigned long g_f100_fail;

static void f100_seed(uint32_t s) { g_f100_state = s; }
static uint32_t f100_next(void) {
    g_f100_state = g_f100_state * F100_LCG_MUL + F100_LCG_ADD;
    return g_f100_state;
}
static unsigned f100_below(unsigned n) {
    return n ? (unsigned)(f100_next() % (uint32_t)n) : 0u;
}
static void f100_init(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    /* Let contract-violation cases return -1/EINVAL instead of _exit(70). */
    cplus_runtime_set_violation_trap(0);
    cplus_libc_log_set_enabled(0);
    f100_seed(F100_LCG_SEED);
    g_f100_ops = 0;
    g_f100_fail = 0;
}
#define F100_OP() ((void)++g_f100_ops)
#define F100_FAIL(msg) do { \
    ++g_f100_fail; \
    fprintf(stderr, "F100 FAIL %s:%d %s\n", __FILE__, __LINE__, (msg)); \
} while (0)
static int f100_report(const char *cfg) {
    printf("CONFIG %s ops=%lu fail=%lu\n", cfg, g_f100_ops, g_f100_fail);
    return g_f100_fail == 0 ? 0 : 1;
}

/* Deterministic temp path; caller unlinks when done.  No user-specific or
 * game/retail path is hard-coded (see AGENTS.md). */
static void f100_tmp_path(char *out, size_t n, const char *tag) {
    const char *d = getenv("TMPDIR");
    if (d == NULL || *d == '\0') d = "/tmp";
    (void)snprintf(out, n, "%s/f100_io_%s_%ld.tmp", d, tag, (long)getpid());
}

/* ---- read_line / token fixtures --------------------------------------- */
#define F100_LINE_BUF 32

/* One combined fixture, six framed cases in this fixed order:
 *  1 empty line        "\n"
 *  2 whitespace line   " \t  \r\n"
 *  3 exact-fit line    (F100_LINE_BUF-1) * 'e' + "\n"
 *  4 overlong line     400 * 'o' (with occasional spaces) + "\n"
 *  5 CRLF line         "abc\r\n"
 *  6 EOF final line    "tail-no-newline" (no terminator)
 */
static void f100_write_line_fixture(const char *path) {
    FILE *f = fopen(path, "wb");
    int i;
    if (f == NULL) { F100_FAIL("cannot create line fixture"); return; }
    fputc('\n', f);                                   /* case 1 */
    fputs(" \t  \r\n", f);                            /* case 2 */
    for (i = 0; i < F100_LINE_BUF - 1; ++i) fputc('e', f); /* case 3 */
    fputc('\n', f);
    for (i = 0; i < 400; ++i)                         /* case 4 */
        fputc((i % 17 == 0) ? ' ' : 'o', f);
    fputc('\n', f);
    fputs("abc\r\n", f);                              /* case 5 */
    fputs("tail-no-newline", f);                      /* case 6 (EOF) */
    if (fclose(f) != 0) F100_FAIL("cannot close line fixture");
}

/* Bounded line reader with overlong-line drain; mirrors the read_line helper
 * used by examples/apps/cgame.c but never overflows n. */
static size_t f100_read_line(char *buf, size_t n) {
    size_t len;
    if (n == 0) return 0;
    buf[0] = '\0';
    if (fgets(buf, (int)n, stdin) == NULL) return 0;
    len = strlen(buf);
    if (len != 0 && buf[len - 1] == '\n') return len;
    if (len + 1 >= n) {                 /* buffer filled: drain physical line */
        int c;
        while ((c = fgetc(stdin)) != '\n' && c != EOF) { }
    }
    return len;
}

/* Token fixture: leading/repeated delimiters, CRLF, and an EOF final token. */
static void f100_write_token_fixture(const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) { F100_FAIL("cannot create token fixture"); return; }
    fputs(",,alpha,beta;;gamma: delta\tepsilon\r\n", f);
    fputs("one two  three\r\n", f);
    fputs("last-token-no-newline", f);
    if (fclose(f) != 0) F100_FAIL("cannot close token fixture");
}

/* ---- numeric token generation ----------------------------------------- */
/* Generates one deterministic token into out (NUL-terminated).  valid==1
 * emits a syntactically valid integer in [base]; valid==0 emits an invalid
 * token (letters only, bad digit for base, empty, sign-only). */
static void f100_int_token(char *out, size_t n, int base, int valid) {
    static const char digits[] = "0123456789abcdefABCDEF";
    size_t k = 0;
    unsigned i, len;
    if (n == 0) return;
    if (!valid) {
        static const char *bad[] = { "", "+", "-", "zzz", "0x", "12w", "--3", "9a" };
        (void)snprintf(out, n, "%s", bad[f100_below(8)]);
        return;
    }
    if (f100_below(4) == 0) out[k++] = (f100_below(2) ? '+' : '-');
    if (base == 16 && f100_below(2) == 0 && k + 2 < n) {
        out[k++] = '0'; out[k++] = 'x';
    }
    len = 1 + f100_below(12);
    for (i = 0; i < len && k + 1 < n; ++i) {
        unsigned d = f100_below((unsigned)base);
        out[k++] = digits[d];
    }
    out[k] = '\0';
}

/* Valid/invalid floating tokens. */
static void f100_float_token(char *out, size_t n, int valid) {
    if (!valid) {
        static const char *bad[] = { "", "abc", "1e", "1.2.3", "--1", "infx" };
        (void)snprintf(out, n, "%s", bad[f100_below(6)]);
        return;
    }
    (void)snprintf(out, n, "%s%u.%02ue%s%d",
                   f100_below(2) ? "-" : "",
                   f100_below(1000), f100_below(100),
                   f100_below(2) ? "+" : "-",
                   (int)f100_below(30));
}
#endif /* F100_IO_COMMON_H */
