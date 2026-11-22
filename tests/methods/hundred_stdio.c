/*
 * hundred_stdio.c -- 100 DISTINCT runtime scenarios for the stdio surface of
 * both safe-C layers.
 *
 * Layer selection (driven by tests/methods/run_hundred.sh):
 *   safecrt : stdlib/safe/safe_c.h + libcplus_safecrt.a + libcplus_hardened.a
 *             (ordinary C names are remapped to the C+ wrappers; the checked
 *              cplus_fopen_s/fclose_s/fread_s/fwrite_s adapters are available)
 *   plain   : -DPLAIN_LIBC, cplus_libc_safe.h + libcplus_hardened.a
 *             (ordinary C names are remapped to the same C+ wrappers; this
 *              layer has no _s adapters, so the equivalent checked libc entry
 *              points cplus_fopen/cplus_fclose plus a local bmax-clamping
 *              adapter are used for the "checked API" scenarios)
 *
 * Every scenario prints exactly:
 *   hundred_stdio: case <n> <method> <scenario> <ok|FINDING:...>
 * main prints:
 *   hundred_stdio: <N>/100 scenarios ok
 * and always returns 0.  A real library defect is reported with a FINDING.
 *
 * Safety properties honored here:
 *   - NULL arguments are only ever passed to GUARDED wrappers.
 *   - size*count overflow scenarios are stopped by the wrapper before the host
 *     runtime sees the product; no huge buffer is ever touched.
 *   - All named temp files use a PID-tagged path and are removed at case 99;
 *     no temp files survive a run.  tmpfile() streams are unnamed/auto-removed.
 *   - No raw FILE* is used after close; double-close is only attempted through
 *     the checked close API that is designed to refuse it.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The per-method runner's "plain" variant uses a libc-only include set and
 * does not pass -DPLAIN_LIBC, so auto-detect it exactly like sibling suites.
 * The explicit -DPLAIN_LIBC switch remains the documented override. */
#if !defined(PLAIN_LIBC)
#  if defined(__has_include)
#    if !__has_include(<safe_runtime.h>)
#      define PLAIN_LIBC 1
#    endif
#  endif
#endif

#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "safe_runtime.h"
#include "safe_log.h"
#endif

/* ------------------------------------------------------------------------- */
/* Checked-adapter abstraction: each layer exercises its OWN checked API.     */
/* ------------------------------------------------------------------------- */
#ifdef PLAIN_LIBC
/* The hardened libc's cplus_fread/cplus_fwrite do not carry an Annex K bmax,
 * so provide a local bmax-clamping adapter with the same guard semantics as
 * the SafeCRT cplus_fread_s/cplus_fwrite_s (stdlib/safe/src/safe_io.c). */
static size_t chk_fread(void *buf, size_t bmax, size_t size, size_t count, FILE *f)
{
    if (!buf || !f || size == 0 || count == 0) return 0;
    if (count > bmax / size) count = bmax / size;
    return cplus_fread(buf, size, count, f);
}
static size_t chk_fwrite(const void *buf, size_t bmax, size_t size, size_t count, FILE *f)
{
    if (!buf || !f || size == 0 || count == 0) return 0;
    if (count > bmax / size) count = bmax / size;
    return cplus_fwrite(buf, size, count, f);
}
#define CHK_FOPEN(n, m) cplus_fopen((n), (m))
#define CHK_FCLOSE(f)   cplus_fclose((f))
#else
#define chk_fread  cplus_fread_s
#define chk_fwrite cplus_fwrite_s
#define CHK_FOPEN(n, m) cplus_fopen_s((n), (m))
#define CHK_FCLOSE(f)   cplus_fclose_s((f))
#endif

/* Open/close through the hardened libc stream API (available in both layers). */
#define OPN(n, m) cplus_fopen((n), (m))
#define CLS(f)    cplus_fclose((f))

/* ------------------------------------------------------------------------- */
/* Reporting helpers.                                                        */
/* ------------------------------------------------------------------------- */
static int g_pass = 0;
static int g_total = 0;
static char g_paths[160][256];
static int g_npaths = 0;

static const char *mpath(const char *tag)
{
    char *p;
    const char *d;
    if (g_npaths >= (int)(sizeof g_paths / sizeof g_paths[0])) return NULL;
    p = g_paths[g_npaths];
    d = getenv("TMPDIR");
    if (!d || !*d) d = "/tmp";
    snprintf(p, 256, "%s/hundred_stdio_%ld_%s_%d.tmp",
             d, (long)getpid(), tag, g_npaths);
    g_npaths++;
    return p;
}

static void R(int n, const char *method, const char *scen, int ok, const char *why)
{
    g_total++;
    if (ok) {
        g_pass++;
        printf("hundred_stdio: case %d %s %s ok\n", n, method, scen);
    } else {
        printf("hundred_stdio: case %d %s %s FINDING:%s\n", n, method, scen, why);
    }
}

#define CASE(n, m, s) int _c = (n); const char *_m = (m); const char *_s = (s); \
                      (void)_c; (void)_m; (void)_s
#define CK(cond) R(_c, _m, _s, (cond), #cond)

/* Setup helpers use host fopen/fclose directly (they are not remapped). */
static int wfile(const char *p, const char *data, size_t n)
{
    FILE *f = fopen(p, "wb");
    size_t w;
    if (!f) return 0;
    w = fwrite(data, 1, n, f);
    fclose(f);
    return w == n;
}
static int req(const char *p, const char *expect, size_t n)
{
    FILE *f = fopen(p, "rb");
    char b[256];
    size_t r;
    if (!f) return 0;
    r = fread(b, 1, sizeof b, f);
    fclose(f);
    return r == n && memcmp(b, expect, n) == 0;
}
static int exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int main(void)
{
#ifndef PLAIN_LIBC
    /* Observe refusals as return values instead of a process trap. */
    cplus_runtime_set_violation_trap(0);
#endif

    /* ---- fopen (1-7) --------------------------------------------------- */
    {
        CASE(1, "fopen", "wb_create_new");
        const char *p = mpath("c1");
        remove(p);
        FILE *f = OPN(p, "wb");
        CK(f != NULL);
        if (f) CLS(f);
    }
    {
        CASE(2, "fopen", "rb_existing");
        const char *p = mpath("c2");
        FILE *f;
        wfile(p, "abc", 3);
        f = OPN(p, "rb");
        CK(f != NULL);
        if (f) CLS(f);
    }
    {
        CASE(3, "fopen", "rb_missing_returns_null");
        const char *p = mpath("c3");
        FILE *f;
        remove(p);
        f = OPN(p, "rb");
        CK(f == NULL);
    }
    {
        CASE(4, "fopen", "null_name_guarded");
        CK(cplus_fopen(NULL, "rb") == NULL);
    }
    {
        CASE(5, "fopen", "null_mode_guarded");
        const char *p = mpath("c5");
        CK(cplus_fopen(p, NULL) == NULL);
    }
    {
        CASE(6, "fopen", "text_w_plus_roundtrip");
        const char *p = mpath("c6");
        FILE *f = OPN(p, "w+");
        char b[16] = {0};
        int ok = (f != NULL);
        if (f) { fputs("hi\n", f); rewind(f); if (fgets(b, sizeof b, f) == NULL) ok = 0; CLS(f); }
        CK(ok && strcmp(b, "hi\n") == 0);
    }
    {
        CASE(7, "fopen", "binary_embedded_nul_roundtrip");
        const char *p = mpath("c7");
        const char src[5] = {'a', '\0', '\n', (char)0xFF, 'b'};
        char b[8] = {0};
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            size_t w = fwrite(src, 1, 5, f);
            rewind(f);
            size_t r = fread(b, 1, 5, f);
            CLS(f);
            ok = ok && w == 5 && r == 5 && memcmp(b, src, 5) == 0;
        }
        CK(ok);
    }

    /* ---- fclose (8-11) ------------------------------------------------- */
    {
        CASE(8, "fclose", "valid_stream_returns_zero");
        const char *p = mpath("c8");
        FILE *f = OPN(p, "wb");
        int ok = (f != NULL);
        if (f) ok = ok && (CLS(f) == 0);
        CK(ok);
    }
    {
        CASE(9, "fclose", "null_stream_guarded");
        CK(cplus_fclose(NULL) == EOF);
    }
    {
        CASE(10, "fclose_s", "double_close_refused_checked");
        const char *p = mpath("c10");
        FILE *f = CHK_FOPEN(p, "wb");
        int a = -1, b = -1;
        if (f) { a = CHK_FCLOSE(f); b = CHK_FCLOSE(f); }
        CK(f != NULL && a == 0 && b == EOF);
    }
    {
        CASE(11, "fclose", "close_flushes_to_disk");
        const char *p = mpath("c11");
        FILE *f = OPN(p, "wb");
        int ok = (f != NULL);
        if (f) { fwrite("flush", 1, 5, f); CLS(f); }
        CK(ok && req(p, "flush", 5));
    }

    /* ---- fread (12-21) ------------------------------------------------- */
    {
        CASE(12, "fread", "exact_item_count");
        const char *p = mpath("c12");
        char b[16] = {0};
        FILE *f;
        size_t n;
        wfile(p, "ABCDE", 5);
        f = OPN(p, "rb");
        n = f ? fread(b, 1, 5, f) : 0;
        CK(f != NULL && n == 5 && memcmp(b, "ABCDE", 5) == 0 && !feof(f));
        if (f) CLS(f);
    }
    {
        CASE(13, "fread", "short_read_sets_feof");
        const char *p = mpath("c13");
        char b[16] = {0};
        FILE *f;
        size_t n;
        wfile(p, "ABCDE", 5);
        f = OPN(p, "rb");
        n = f ? fread(b, 1, 10, f) : 0;
        CK(f != NULL && n == 5 && feof(f));
        if (f) CLS(f);
    }
    {
        CASE(14, "fread", "read_at_eof_returns_zero");
        const char *p = mpath("c14");
        char b[16];
        FILE *f;
        size_t n1, n2;
        wfile(p, "xy", 2);
        f = OPN(p, "rb");
        n1 = f ? fread(b, 1, 8, f) : 0;
        n2 = f ? fread(b, 1, 8, f) : 0;
        CK(f != NULL && n1 == 2 && n2 == 0 && feof(f));
        if (f) CLS(f);
    }
    {
        CASE(15, "fread", "clearerr_then_feof_again");
        const char *p = mpath("c15");
        char b[8];
        FILE *f;
        size_t n;
        wfile(p, "z", 1);
        f = OPN(p, "rb");
        n = f ? fread(b, 1, 4, f) : 0;
        if (f) clearerr(f);
        n = f ? fread(b, 1, 4, f) : 0;
        CK(f != NULL && n == 0 && feof(f));
        if (f) CLS(f);
    }
    {
        CASE(16, "fread", "size2_count3_exact");
        const char *p = mpath("c16");
        char b[8] = {0};
        FILE *f;
        size_t n;
        wfile(p, "A1B2C3", 6);
        f = OPN(p, "rb");
        n = f ? fread(b, 2, 3, f) : 0;
        CK(f != NULL && n == 3 && memcmp(b, "A1B2C3", 6) == 0);
        if (f) CLS(f);
    }
    {
        CASE(17, "fread", "partial_item_only_whole_items");
        const char *p = mpath("c17");
        char b[8] = {0};
        FILE *f;
        size_t n;
        wfile(p, "123456", 6);
        f = OPN(p, "rb");
        n = f ? fread(b, 4, 3, f) : 0;
        CK(f != NULL && n == 1 && memcmp(b, "1234", 4) == 0 && feof(f));
        if (f) CLS(f);
    }
    {
        CASE(18, "fread", "size_zero");
        const char *p = mpath("c18");
        char b[4] = {0};
        FILE *f = OPN(p, "wb+");
        size_t n = f ? fread(b, 0, 3, f) : 99;
        CK(f != NULL && n == 0);
        if (f) CLS(f);
    }
    {
        CASE(19, "fread", "count_zero");
        const char *p = mpath("c19");
        char b[4] = {0};
        FILE *f = OPN(p, "wb+");
        size_t n = f ? fread(b, 1, 0, f) : 99;
        CK(f != NULL && n == 0);
        if (f) CLS(f);
    }
    {
        CASE(20, "fread", "null_buffer_guarded");
        const char *p = mpath("c20");
        FILE *f = OPN(p, "wb+");
        size_t n = f ? fread(NULL, 1, 4, f) : 99;
        CK(f != NULL && n == 0);
        if (f) CLS(f);
    }
    {
        CASE(21, "fread", "size_count_overflow_guarded");
        const char *p = mpath("c21");
        char b[4] = {0};
        FILE *f = OPN(p, "wb+");
        size_t n;
        errno = 0;
        n = f ? fread(b, 4096, (size_t)-1, f) : 99;
        CK(f != NULL && n == 0 && errno == EOVERFLOW);
        if (f) CLS(f);
    }

    /* ---- fwrite (22-28) ------------------------------------------------ */
    {
        CASE(22, "fwrite", "exact_item_count");
        const char *p = mpath("c22");
        FILE *f = OPN(p, "wb");
        size_t n = f ? fwrite("HELLO", 1, 5, f) : 0;
        if (f) CLS(f);
        CK(n == 5 && req(p, "HELLO", 5));
    }
    {
        CASE(23, "fwrite", "write_to_readonly_ferror");
        const char *p = mpath("c23");
        FILE *f;
        size_t n;
        wfile(p, "R", 1);
        f = OPN(p, "rb");
        errno = 0;
        n = f ? fwrite("x", 1, 1, f) : 99;
        CK(f != NULL && n == 0 && ferror(f));
        if (f) CLS(f);
    }
    {
        CASE(24, "fwrite", "size4_count2_exact");
        const char *p = mpath("c24");
        FILE *f = OPN(p, "wb");
        size_t n = f ? fwrite("ABCDEFGH", 4, 2, f) : 0;
        if (f) CLS(f);
        CK(n == 2 && req(p, "ABCDEFGH", 8));
    }
    {
        CASE(25, "fwrite", "count_zero");
        const char *p = mpath("c25");
        FILE *f = OPN(p, "wb");
        size_t n = f ? fwrite("x", 4, 0, f) : 99;
        CK(f != NULL && n == 0);
        if (f) CLS(f);
    }
    {
        CASE(26, "fwrite", "size_zero");
        const char *p = mpath("c26");
        FILE *f = OPN(p, "wb");
        size_t n = f ? fwrite("x", 0, 4, f) : 99;
        CK(f != NULL && n == 0);
        if (f) CLS(f);
    }
    {
        CASE(27, "fwrite", "null_buffer_guarded");
        const char *p = mpath("c27");
        FILE *f = OPN(p, "wb");
        size_t n = f ? fwrite(NULL, 1, 4, f) : 99;
        CK(f != NULL && n == 0);
        if (f) CLS(f);
    }
    {
        CASE(28, "fwrite", "size_count_overflow_guarded");
        const char *p = mpath("c28");
        char b[4] = {0};
        FILE *f = OPN(p, "wb");
        size_t n;
        errno = 0;
        n = f ? fwrite(b, 4096, (size_t)-1, f) : 99;
        CK(f != NULL && n == 0 && errno == EOVERFLOW);
        if (f) CLS(f);
    }

    /* ---- fseek / ftell / rewind (29-38) -------------------------------- */
    {
        CASE(29, "fseek", "set_zero_and_ftell");
        const char *p = mpath("c29");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            fwrite("0123456789", 1, 10, f);
            ok = ok && fseek(f, 0, SEEK_SET) == 0 && ftell(f) == 0;
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(30, "fseek", "set_middle");
        const char *p = mpath("c30");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) { fwrite("0123456789", 1, 10, f); fseek(f, 4, SEEK_SET); ok = ftell(f) == 4; CLS(f); }
        CK(ok);
    }
    {
        CASE(31, "fseek", "end_zero_reports_size");
        const char *p = mpath("c31");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) { fwrite("0123456789", 1, 10, f); fseek(f, 0, SEEK_END); ok = ftell(f) == 10; CLS(f); }
        CK(ok);
    }
    {
        CASE(32, "fseek", "cur_forward");
        const char *p = mpath("c32");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            fwrite("0123456789", 1, 10, f);
            fseek(f, 2, SEEK_SET);
            fseek(f, 3, SEEK_CUR);
            ok = ftell(f) == 5;
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(33, "fseek", "cur_backward");
        const char *p = mpath("c33");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            fwrite("0123456789", 1, 10, f);
            fseek(f, 0, SEEK_END);
            fseek(f, -4, SEEK_CUR);
            ok = ftell(f) == 6;
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(34, "fseek", "past_eof_then_write");
        const char *p = mpath("c34");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            fseek(f, 20, SEEK_SET);
            fputc('X', f);
            ok = ftell(f) == 21;
            fflush(f);
            fseek(f, 20, SEEK_SET);
            ok = ok && fgetc(f) == 'X';
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(35, "ftell", "fresh_stream_zero");
        const char *p = mpath("c35");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) { ok = ftell(f) == 0; CLS(f); }
        CK(ok);
    }
    {
        CASE(36, "rewind", "clears_eof_and_position");
        const char *p = mpath("c36");
        FILE *f;
        int ok;
        wfile(p, "ab", 2);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) {
            while (getc(f) != EOF) { }
            ok = ok && feof(f);
            rewind(f);
            ok = ok && !feof(f) && ftell(f) == 0;
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(37, "fseek", "text_ftell_roundtrip");
        const char *p = mpath("c37");
        FILE *f = OPN(p, "w+");
        int ok = (f != NULL);
        if (f) {
            char b[8] = {0};
            long pos;
            fputs("abc", f);
            pos = ftell(f);
            fseek(f, pos, SEEK_SET);
            ok = ok && ftell(f) == pos;
            fseek(f, 0, SEEK_SET);
            ok = ok && fgets(b, sizeof b, f) != NULL && strcmp(b, "abc") == 0;
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(38, "fseek", "before_begin_returns_error");
        const char *p = mpath("c38");
        FILE *f;
        int ok;
        wfile(p, "abc", 3);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) { ok = ok && fseek(f, -1, SEEK_SET) != 0; CLS(f); }
        CK(ok);
    }

    /* ---- fgetpos / fsetpos (39-42) ------------------------------------- */
    {
        CASE(39, "fgetpos", "fsetpos_restores_position");
        const char *p = mpath("c39");
        FILE *f;
        fpos_t pos;
        int ok;
        wfile(p, "abcdef", 6);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) {
            fseek(f, 2, SEEK_SET);
            ok = ok && fgetpos(f, &pos) == 0;
            fseek(f, 0, SEEK_SET);
            ok = ok && fsetpos(f, &pos) == 0 && ftell(f) == 2;
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(40, "fsetpos", "overwrite_earlier_bytes");
        const char *p = mpath("c40");
        FILE *f;
        fpos_t pos;
        wfile(p, "AAAA", 4);
        f = OPN(p, "r+b");
        if (f) {
            fgetpos(f, &pos);
            fwrite("BB", 1, 2, f);
            fsetpos(f, &pos);
            fwrite("CC", 1, 2, f);
            CLS(f);
        }
        CK(f != NULL && req(p, "CCAA", 4));
    }
    {
        CASE(41, "fsetpos", "after_eof_then_clearerr");
        const char *p = mpath("c41");
        FILE *f;
        fpos_t pos;
        int ok;
        wfile(p, "abc", 3);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) {
            fgetpos(f, &pos);
            while (getc(f) != EOF) { }
            ok = ok && feof(f);
            fsetpos(f, &pos);
            clearerr(f);
            ok = ok && getc(f) == 'a';
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(42, "fgetpos", "binary_end_position");
        const char *p = mpath("c42");
        FILE *f;
        fpos_t pos;
        int ok;
        wfile(p, "xyz", 3);
        f = OPN(p, "rb+");
        ok = (f != NULL);
        if (f) {
            fseek(f, 0, SEEK_END);
            ok = ok && fgetpos(f, &pos) == 0 && ftell(f) == 3;
            ok = ok && fsetpos(f, &pos) == 0 && ftell(f) == 3;
            CLS(f);
        }
        CK(ok);
    }

    /* ---- fgets / fputs (43-50) ----------------------------------------- */
    {
        CASE(43, "fputs", "writes_string");
        const char *p = mpath("c43");
        FILE *f = OPN(p, "wb");
        int r = f ? fputs("hello", f) : -1;
        if (f) CLS(f);
        CK(r >= 0 && req(p, "hello", 5));
    }
    {
        CASE(44, "fgets", "line_with_newline");
        const char *p = mpath("c44");
        FILE *f;
        char b[16] = {0};
        int ok;
        wfile(p, "hi\n", 3);
        f = OPN(p, "rt");
        ok = (f != NULL);
        if (f) { ok = ok && fgets(b, sizeof b, f) == b && strcmp(b, "hi\n") == 0; CLS(f); }
        CK(ok);
    }
    {
        CASE(45, "fgets", "buffer_smaller_than_line");
        const char *p = mpath("c45");
        FILE *f;
        char b[4] = {0};
        int ok;
        wfile(p, "abcdefghij", 10);
        f = OPN(p, "rt");
        ok = (f != NULL);
        if (f) { ok = ok && fgets(b, sizeof b, f) == b && strcmp(b, "abc") == 0; CLS(f); }
        CK(ok);
    }
    {
        CASE(46, "fgets", "at_eof_returns_null");
        const char *p = mpath("c46");
        FILE *f;
        char b[16];
        int ok;
        wfile(p, "q", 1);
        f = OPN(p, "rt");
        ok = (f != NULL);
        if (f) { while (fgets(b, sizeof b, f) != NULL) { } ok = ok && feof(f); CLS(f); }
        CK(ok);
    }
    {
        CASE(47, "fgets", "n_one_empty_string");
        const char *p = mpath("c47");
        FILE *f;
        char b[4] = {'?'};
        int ok;
        wfile(p, "xyz", 3);
        f = OPN(p, "rt");
        ok = (f != NULL);
        if (f) { ok = ok && fgets(b, 1, f) == b && b[0] == '\0'; CLS(f); }
        CK(ok);
    }
    {
        CASE(48, "fputs", "empty_string_writes_nothing");
        const char *p = mpath("c48");
        FILE *f = OPN(p, "wb");
        int r = f ? fputs("", f) : -1;
        if (f) CLS(f);
        CK(r >= 0 && req(p, "", 0));
    }
    {
        CASE(49, "fputs", "write_to_readonly_ferror");
        const char *p = mpath("c49");
        FILE *f;
        int r;
        wfile(p, "R", 1);
        f = OPN(p, "rb");
        r = f ? fputs("x", f) : 0;
        CK(f != NULL && r == EOF && ferror(f));
        if (f) CLS(f);
    }
    {
        CASE(50, "fgets", "after_clearerr_at_eof");
        const char *p = mpath("c50");
        FILE *f;
        char b[8];
        int ok;
        wfile(p, "a", 1);
        f = OPN(p, "rt");
        ok = (f != NULL);
        if (f) {
            while (fgets(b, sizeof b, f) != NULL) { }
            clearerr(f);
            ok = ok && fgets(b, sizeof b, f) == NULL;
            CLS(f);
        }
        CK(ok);
    }

    /* ---- getc / putc / ungetc (51-58) ---------------------------------- */
    {
        CASE(51, "putc", "getc_roundtrip");
        const char *p = mpath("c51");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            ok = ok && putc('Q', f) == 'Q';
            fflush(f); rewind(f);
            ok = ok && getc(f) == 'Q';
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(52, "getc", "eof_set");
        const char *p = mpath("c52");
        FILE *f;
        int ok;
        wfile(p, "a", 1);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) { ok = ok && getc(f) == 'a' && getc(f) == EOF && feof(f); CLS(f); }
        CK(ok);
    }
    {
        CASE(53, "ungetc", "pushback_one_char");
        const char *p = mpath("c53");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            fputc('A', f); fflush(f); rewind(f);
            ok = ok && getc(f) == 'A';
            ok = ok && ungetc('B', f) == 'B' && getc(f) == 'B';
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(54, "ungetc", "eof_argument_is_noop");
        const char *p = mpath("c54");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            fputc('Z', f); fflush(f); rewind(f);
            ok = ok && getc(f) == 'Z';
            ok = ok && ungetc(EOF, f) == EOF;
            ok = ok && getc(f) == EOF;
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(55, "ungetc", "twice_separated_by_read");
        const char *p = mpath("c55");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            ok = ok && ungetc('a', f) == 'a' && getc(f) == 'a';
            ok = ok && ungetc('b', f) == 'b' && getc(f) == 'b';
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(56, "putc", "write_to_readonly_ferror");
        const char *p = mpath("c56");
        FILE *f;
        int r;
        wfile(p, "R", 1);
        f = OPN(p, "rb");
        r = f ? putc('x', f) : 0;
        CK(f != NULL && r == EOF && ferror(f));
        if (f) CLS(f);
    }
    {
        CASE(57, "getc", "read_from_writeonly_ferror");
        const char *p = mpath("c57");
        FILE *f = OPN(p, "wb");
        int r = f ? getc(f) : 0;
        CK(f != NULL && r == EOF && ferror(f));
        if (f) CLS(f);
    }
    {
        CASE(58, "ungetc", "after_eof_and_clearerr");
        const char *p = mpath("c58");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            fputc('M', f); fflush(f); rewind(f);
            ok = ok && getc(f) == 'M' && getc(f) == EOF && feof(f);
            clearerr(f);
            ok = ok && ungetc('X', f) == 'X' && getc(f) == 'X';
            CLS(f);
        }
        CK(ok);
    }

    /* ---- feof / ferror / clearerr (59-66) ------------------------------- */
    {
        CASE(59, "feof", "initially_zero");
        const char *p = mpath("c59");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) { ok = ok && !feof(f); CLS(f); }
        CK(ok);
    }
    {
        CASE(60, "feof", "after_read_past_end");
        const char *p = mpath("c60");
        FILE *f;
        char b[4];
        int ok;
        wfile(p, "a", 1);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) { fread(b, 1, 4, f); ok = ok && feof(f); CLS(f); }
        CK(ok);
    }
    {
        CASE(61, "ferror", "initially_zero");
        const char *p = mpath("c61");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) { ok = ok && !ferror(f); CLS(f); }
        CK(ok);
    }
    {
        CASE(62, "ferror", "after_invalid_write");
        const char *p = mpath("c62");
        FILE *f;
        int ok;
        wfile(p, "R", 1);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) { fwrite("x", 1, 1, f); ok = ok && ferror(f); CLS(f); }
        CK(ok);
    }
    {
        CASE(63, "clearerr", "clears_feof");
        const char *p = mpath("c63");
        FILE *f;
        char b[4];
        int ok;
        wfile(p, "a", 1);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) {
            fread(b, 1, 4, f);
            ok = ok && feof(f);
            clearerr(f);
            ok = ok && !feof(f);
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(64, "clearerr", "clears_ferror");
        const char *p = mpath("c64");
        FILE *f;
        int ok;
        wfile(p, "R", 1);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) {
            fwrite("x", 1, 1, f);
            ok = ok && ferror(f);
            clearerr(f);
            ok = ok && !ferror(f);
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(65, "feof", "set_again_after_clearerr_read");
        const char *p = mpath("c65");
        FILE *f;
        char b[4];
        int ok;
        wfile(p, "a", 1);
        f = OPN(p, "rb");
        ok = (f != NULL);
        if (f) {
            fread(b, 1, 4, f);
            clearerr(f);
            ok = ok && !feof(f);
            fread(b, 1, 4, f);
            ok = ok && feof(f);
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(66, "ferror", "fresh_open_indicators_clean");
        const char *p = mpath("c66");
        wfile(p, "clean", 5);
        FILE *f = OPN(p, "rb");
        int ok = (f != NULL);
        if (f) { ok = ok && !ferror(f) && !feof(f); CLS(f); }
        CK(ok);
    }

    /* ---- setvbuf (67-70) ----------------------------------------------- */
    {
        CASE(67, "setvbuf", "ionbf_stream_usable");
        const char *p = mpath("c67");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            ok = ok && setvbuf(f, NULL, _IONBF, 0) == 0;
            fputs("n", f); fflush(f); rewind(f);
            ok = ok && getc(f) == 'n';
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(68, "setvbuf", "iofbf_with_buffer");
        const char *p = mpath("c68");
        char buf[64];
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            ok = ok && setvbuf(f, buf, _IOFBF, sizeof buf) == 0;
            fputs("fb", f); fflush(f); rewind(f);
            ok = ok && getc(f) == 'f' && getc(f) == 'b';
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(69, "setvbuf", "iolbf_stream_usable");
        const char *p = mpath("c69");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            ok = ok && setvbuf(f, NULL, _IOLBF, 0) == 0;
            fputs("lb\n", f); fflush(f); rewind(f);
            ok = ok && getc(f) == 'l';
            CLS(f);
        }
        CK(ok);
    }
    {
        CASE(70, "setvbuf", "null_buffer_ionbf");
        const char *p = mpath("c70");
        FILE *f = OPN(p, "wb+");
        int ok = (f != NULL);
        if (f) {
            ok = ok && setvbuf(f, NULL, _IONBF, 123) == 0;
            fputs("z", f); fflush(f); rewind(f);
            ok = ok && getc(f) == 'z';
            CLS(f);
        }
        CK(ok);
    }

    /* ---- tmpfile (71-74) ----------------------------------------------- */
    {
        CASE(71, "tmpfile", "write_read_roundtrip");
        FILE *f = tmpfile();
        int ok = (f != NULL);
        if (f) {
            fputs("t", f); fflush(f); rewind(f);
            ok = ok && getc(f) == 't';
            fclose(f);
        }
        CK(ok);
    }
    {
        CASE(72, "tmpfile", "close_returns_zero");
        FILE *f = tmpfile();
        int ok = (f != NULL);
        if (f) ok = ok && fclose(f) == 0;
        CK(ok);
    }
    {
        CASE(73, "tmpfile", "two_independent_streams");
        FILE *f1 = tmpfile(), *f2 = tmpfile();
        int ok = (f1 != NULL && f2 != NULL);
        if (f1 && f2) {
            fputc('A', f1); fputc('B', f2);
            fflush(f1); fflush(f2);
            rewind(f1); rewind(f2);
            ok = ok && getc(f1) == 'A' && getc(f2) == 'B';
            fclose(f1); fclose(f2);
        }
        CK(ok);
    }
    {
        CASE(74, "tmpfile", "seek_tell_end");
        FILE *f = tmpfile();
        int ok = (f != NULL);
        if (f) {
            fwrite("12345", 1, 5, f);
            fseek(f, 0, SEEK_END);
            ok = ok && ftell(f) == 5;
            fclose(f);
        }
        CK(ok);
    }

    /* ---- freopen (75-77) ----------------------------------------------- */
    {
        CASE(75, "freopen", "redirect_to_new_file");
        const char *p1 = mpath("c75a"), *p2 = mpath("c75b");
        FILE *f, *g;
        wfile(p1, "old", 3);
        f = fopen(p1, "rb");
        g = f ? freopen(p2, "wb", f) : NULL;
        if (g) { fputs("new", g); fclose(g); }
        CK(g != NULL && req(p2, "new", 3));
    }
    {
        CASE(76, "freopen", "same_file_mode_change");
        const char *p = mpath("c76");
        FILE *f, *g;
        wfile(p, "txt", 3);
        f = fopen(p, "r+");
        g = f ? freopen(p, "rb", f) : NULL;
        CK(g != NULL && getc(g) == 't');
        if (g) fclose(g);
    }
    {
        CASE(77, "freopen", "write_after_redirect");
        const char *p = mpath("c77");
        FILE *f, *g;
        wfile(p, "old", 3);
        f = fopen(p, "rb");
        g = f ? freopen(p, "wb", f) : NULL;
        if (g) { fputs("w", g); fclose(g); }
        CK(g != NULL && req(p, "w", 1));
    }

    /* ---- remove / rename (78-82) --------------------------------------- */
    {
        CASE(78, "remove", "existing_file");
        const char *p = mpath("c78");
        wfile(p, "x", 1);
        CK(remove(p) == 0 && !exists(p));
    }
    {
        CASE(79, "remove", "nonexistent_file");
        const char *p = mpath("c79");
        remove(p);
        CK(remove(p) != 0);
    }
    {
        CASE(80, "rename", "existing_file");
        const char *p1 = mpath("c80a"), *p2 = mpath("c80b");
        wfile(p1, "data", 4);
        remove(p2);
        CK(rename(p1, p2) == 0 && !exists(p1) && req(p2, "data", 4));
    }
    {
        CASE(81, "rename", "overwrite_destination");
        const char *p1 = mpath("c81a"), *p2 = mpath("c81b");
        wfile(p1, "NEW", 3);
        wfile(p2, "OLD", 3);
        CK(rename(p1, p2) == 0 && req(p2, "NEW", 3));
    }
    {
        CASE(82, "rename", "nonexistent_source");
        const char *p1 = mpath("c82a"), *p2 = mpath("c82b");
        remove(p1);
        CK(rename(p1, p2) != 0);
    }

    /* ---- printf / fprintf / snprintf (83-91) ---------------------------- */
    {
        CASE(83, "printf", "redirected_literal_and_args");
        const char *p = mpath("c83");
        const char msg[] = "hundred_stdio: printf-probe 42\n";
        int sv = dup(STDOUT_FILENO);
        FILE *cap = fopen(p, "wb");
        int r;
        fflush(stdout); /* do not redirect already-buffered case output */
        if (cap) dup2(fileno(cap), STDOUT_FILENO);
        r = printf("hundred_stdio: printf-probe %d\n", 42);
        fflush(stdout);
        if (cap) { dup2(sv, STDOUT_FILENO); fclose(cap); }
        close(sv);
        CK(cap != NULL && r == (int)strlen(msg) && req(p, msg, strlen(msg)));
    }
    {
        CASE(84, "printf", "redirected_empty_format");
        const char *p = mpath("c84");
        int sv = dup(STDOUT_FILENO);
        FILE *cap = fopen(p, "wb");
        int r;
        fflush(stdout); /* do not redirect already-buffered case output */
        if (cap) dup2(fileno(cap), STDOUT_FILENO);
        r = printf("");
        fflush(stdout);
        if (cap) { dup2(sv, STDOUT_FILENO); fclose(cap); }
        close(sv);
        CK(cap != NULL && r == 0 && req(p, "", 0));
    }
    {
        CASE(85, "fprintf", "formatted_to_file");
        const char *p = mpath("c85");
        FILE *f = OPN(p, "wb");
        int r = f ? fprintf(f, "%s=%d", "k", 7) : -1;
        if (f) CLS(f);
        CK(r == 3 && req(p, "k=7", 3));
    }
    {
        CASE(86, "fprintf", "null_stream_guarded");
        CK(fprintf(NULL, "x") == -1);
    }
    {
        CASE(87, "fprintf", "null_format_guarded");
        const char *p = mpath("c87");
        const char * volatile vf = NULL;
        FILE *f = OPN(p, "wb");
        int r;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
        r = f ? fprintf(f, vf) : 0;
#pragma clang diagnostic pop
        CK(f != NULL && r == -1);
        if (f) CLS(f);
    }
    {
        CASE(88, "snprintf", "bounded_format");
        char b[16] = {0};
        int r = snprintf(b, sizeof b, "%d-%s", 12, "ok");
        CK(r == 5 && strcmp(b, "12-ok") == 0);
    }
    {
        CASE(89, "snprintf", "truncation_null_terminated");
        char b[4] = {'x', 'x', 'x', 'x'};
        int r = snprintf(b, sizeof b, "ABCDEFGH");
        CK(r == 8 && strcmp(b, "ABC") == 0);
    }
    {
        CASE(90, "snprintf", "size_zero_buffer_untouched");
        char b[4] = {'q', 'q', 'q', 'q'};
        int r = snprintf(b, 0, "HI");
        CK(r == 2 && b[0] == 'q');
    }
    {
        CASE(91, "snprintf", "size_zero_null_buffer");
        int r = snprintf(NULL, 0, "LONG-%d", 9);
        CK(r == 6);
    }

    /* ---- checked adapters fopen_s / fread_s / fwrite_s (92-97) ---------- */
    {
        CASE(92, "fopen_s", "valid_open");
        const char *p = mpath("c92");
        FILE *f = CHK_FOPEN(p, "wb");
        int ok = (f != NULL);
        if (f) ok = ok && CHK_FCLOSE(f) == 0;
        CK(ok);
    }
    {
        CASE(93, "fopen_s", "null_name_and_mode_guarded");
        const char *p = mpath("c93");
        CK(CHK_FOPEN(NULL, "wb") == NULL && CHK_FOPEN(p, NULL) == NULL);
    }
    {
        CASE(94, "fread_s", "exact_within_bmax");
        const char *p = mpath("c94");
        char b[16] = {0};
        FILE *f;
        size_t n;
        wfile(p, "ABCDE", 5);
        f = CHK_FOPEN(p, "rb");
        n = f ? chk_fread(b, sizeof b, 1, 5, f) : 0;
        CK(f != NULL && n == 5 && memcmp(b, "ABCDE", 5) == 0);
        if (f) CHK_FCLOSE(f);
    }
    {
        CASE(95, "fread_s", "bmax_clamps_count");
        const char *p = mpath("c95");
        char b[16] = {0};
        FILE *f;
        size_t n;
        wfile(p, "ABCDEFGHIJ", 10);
        f = CHK_FOPEN(p, "rb");
        n = f ? chk_fread(b, 4, 1, 10, f) : 0;
        CK(f != NULL && n == 4 && memcmp(b, "ABCD", 4) == 0);
        if (f) CHK_FCLOSE(f);
    }
    {
        CASE(96, "fwrite_s", "exact_within_bmax");
        const char *p = mpath("c96");
        FILE *f = CHK_FOPEN(p, "wb");
        size_t n = f ? chk_fwrite("WXYZ", 4, 1, 4, f) : 0;
        if (f) CHK_FCLOSE(f);
        CK(n == 4 && req(p, "WXYZ", 4));
    }
    {
        CASE(97, "fwrite_s", "bmax_clamps_count");
        const char *p = mpath("c97");
        FILE *f = CHK_FOPEN(p, "wb");
        size_t n = f ? chk_fwrite("WXYZ", 2, 1, 4, f) : 0;
        if (f) CHK_FCLOSE(f);
        CK(n == 2 && req(p, "WX", 2));
    }

    /* ---- logging parity + temp-file cleanup (98-100) ------------------- */
    {
        CASE(98, "logging", "off_on_parity");
        const char *p1 = mpath("c98a"), *p2 = mpath("c98b");
        int r1, r2;
#ifdef PLAIN_LIBC
        cplus_libc_log_set_enabled(0);
#else
        cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
#endif
        r1 = wfile(p1, "parity", 6) && req(p1, "parity", 6);
#ifdef PLAIN_LIBC
        cplus_libc_log_set_enabled(1);
#else
        cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
#endif
        r2 = wfile(p2, "parity", 6) && req(p2, "parity", 6);
#ifdef PLAIN_LIBC
        cplus_libc_log_set_enabled(0);
#else
        cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
#endif
        CK(r1 && r2 && r1 == r2);
    }
    {
        CASE(99, "tmpfile", "all_named_temps_removed");
        int i;
        int all = 1;
        for (i = 0; i < g_npaths; ++i) remove(g_paths[i]);
        for (i = 0; i < g_npaths; ++i) if (exists(g_paths[i])) all = 0;
        CK(all && g_npaths > 0);
    }
    {
        CASE(100, "fclose", "final_stream_state_clean");
        const char *p = mpath("c100");
        FILE *f = OPN(p, "wb");
        int ok = (f != NULL);
        if (f) ok = ok && (CLS(f) == 0);
        remove(p);
        CK(ok);
    }

    printf("hundred_stdio: %d/100 scenarios ok\n", g_pass);
    if (g_total != 100)
        printf("hundred_stdio: FINDING: scenario count is %d, expected 100\n", g_total);
    return 0;
}
