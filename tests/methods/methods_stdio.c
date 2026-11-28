/*
 * methods_stdio.c -- per-method unit + logging-parity tests for the stdio
 * methods of both ordinary-C layers and of the checked I/O API.
 *
 * Build (default / SafeCRT layer):
 *   clang -std=c99 -Wall -Wextra -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     tests/methods/methods_stdio.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o methods_stdio
 *
 * Build (plain libc layer): add -DPLAIN_LIBC and link only
 *   build/libc/libcplus_hardened.a (checked `_s` APIs are guarded out).
 *
 * Every scenario is executed twice, once with all C+ logging disabled and
 * once with it enabled; the complete observable result (return values, errno,
 * stream flags, file bytes) is compared byte-for-byte. A mismatch, or any
 * wrong result, prints a `FINDING:` line. The process always exits 0 and ends
 * with `methods_stdio: <N>/<N> checks ok`. All named temporary files are
 * removed before exit. Only guarded NULL arguments are exercised; no genuine
 * UB (double fclose on a raw FILE*, NULL passed to an unguarded API, etc.)
 * is ever performed.
 */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#ifndef METHODS_STDIO_NO_CHECKED_IO
#include "stdlib/safe/safe_io.h"
#include "stdlib/safe/safe_log.h"
#define HAVE_CHECKED_IO 1
#endif
#endif

/* ------------------------------------------------------------------ */
/* Result record: every scenario writes only into this POD, so the OFF  */
/* and ON runs can be compared with a plain byte walk (no padding uses  */
/* uninitialised data: every record is zeroed first).                   */
/* ------------------------------------------------------------------ */
#define RES_I 20
#define RES_L 8
#define RES_Z 16
#define RES_E 16
#define RES_S 8
#define RES_SLEN 64
#define RES_B 128

typedef struct {
    int i[RES_I];
    long l[RES_L];
    size_t z[RES_Z];
    int e[RES_E];
    char s[RES_S][RES_SLEN];
    unsigned char b[RES_B];
    size_t nb;
} Res;

static int g_checks;
static int g_fails;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_fails;                                                        \
            printf("FINDING: %s\n", (msg));                                   \
        }                                                                     \
    } while (0)

#define CHECKF(cond, fmt, ...)                                                \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_fails;                                                        \
            printf("FINDING: " fmt "\n", __VA_ARGS__);                        \
        }                                                                     \
    } while (0)

static void bzero_local(void *p, size_t n)
{
    unsigned char *q = (unsigned char *)p;
    size_t k;
    for (k = 0; k < n; ++k) q[k] = 0;
}

static int bequal(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t k;
    for (k = 0; k < n; ++k)
        if (x[k] != y[k]) return 0;
    return 1;
}

static int ceq(const char *a, const char *b)
{
    size_t k = 0;
    while (a[k] && b[k]) {
        if (a[k] != b[k]) return 0;
        ++k;
    }
    return a[k] == b[k];
}

static void res_zero(Res *r)
{
    bzero_local(r, sizeof *r);
}

static void res_sets(Res *r, int idx, const char *src)
{
    size_t k = 0;
    if (idx < 0 || idx >= RES_S) return;
    while (src[k] && k + 1u < RES_SLEN) {
        r->s[idx][k] = src[k];
        ++k;
    }
    r->s[idx][k] = '\0';
}

static void res_setb(Res *r, const void *p, size_t n)
{
    const unsigned char *q = (const unsigned char *)p;
    size_t k;
    if (n > RES_B) n = RES_B;
    for (k = 0; k < n; ++k) r->b[k] = q[k];
    r->nb = n;
}

static size_t first_diff(const Res *a, const Res *b)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t k;
    for (k = 0; k < sizeof(Res); ++k)
        if (x[k] != y[k]) return k;
    return (size_t)-1;
}

/* ------------------------------------------------------------------ */
/* Logging control and scenario driver.                                */
/* ------------------------------------------------------------------ */
static void logging_set(int on)
{
    /* Both layers link the hardened libc (its wrappers log via this flag). */
    cplus_libc_log_set_enabled(on);
#ifdef HAVE_CHECKED_IO
    /* SafeCRT checked I/O logs through its own level-based logger. */
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
#endif
}

typedef void (*scenario_fn)(Res *);

static void run_scenario(const char *mid, const char *label,
                         scenario_fn fn, Res *out)
{
    Res off, on;
    size_t d;
    res_zero(&off);
    res_zero(&on);

    logging_set(0);
    errno = 0;
    fn(&off);

    logging_set(1);
    errno = 0;
    fn(&on);

    logging_set(0);
    *out = off;
    d = first_diff(&off, &on);
    CHECKF(d == (size_t)-1,
           "%s/%s logging parity differs at byte %lu",
           mid, label, (unsigned long)d);
}

/* ------------------------------------------------------------------ */
/* Temp-file registry: every generated name is removed at the end.      */
/* ------------------------------------------------------------------ */
#define MAX_TEMP 512
#define PATH_CAP 256
static char g_paths[MAX_TEMP][PATH_CAP];
static int g_npaths;
static int g_path_seq;

static void path_make(char *buf, size_t cap, const char *tag, int reg)
{
    const char *dir = getenv("TMPDIR");
    long stamp;
    if (!dir || !*dir) dir = "/tmp";
#ifdef _WIN32
    stamp = (long)time(NULL);
#else
    stamp = (long)getpid();
#endif
    snprintf(buf, cap, "%s/methods_stdio_%s_%ld_%d.tmp",
             dir, tag, stamp, ++g_path_seq);
    if (reg && g_npaths < MAX_TEMP) {
        size_t k = 0;
        while (buf[k] && k + 1u < PATH_CAP) {
            g_paths[g_npaths][k] = buf[k];
            ++k;
        }
        g_paths[g_npaths][k] = '\0';
        ++g_npaths;
    }
}

static void temp_cleanup(void)
{
    int k;
    for (k = 0; k < g_npaths; ++k) (void)remove(g_paths[k]);
    g_npaths = 0;
}

static void put_file(const char *path, const char *data, size_t n)
{
    FILE *fp = cplus_fopen(path, "wb");
    if (!fp) return;
    if (n) (void)cplus_fwrite(data, 1, n, fp);
    (void)cplus_fclose(fp);
}

static size_t get_file(const char *path, char *buf, size_t cap)
{
    size_t n;
    FILE *fp = cplus_fopen(path, "rb");
    if (!fp) {
        if (cap) buf[0] = '\0';
        return 0;
    }
    n = cplus_fread(buf, 1, cap, fp);
    (void)cplus_fclose(fp);
    if (n < cap) buf[n] = '\0';
    else if (cap) buf[cap - 1u] = '\0';
    return n;
}

#ifndef _WIN32
/* stdout redirection so printf() formatting can be checked without adding
 * stray bytes to this test's own stdout. POSIX-guarded, never on Windows. */
static int stdout_redirect(const char *path, FILE **outfp, int *saved)
{
    FILE *fp = cplus_fopen(path, "wb");
    int fd;
    if (!fp) return -1;
    *saved = dup(STDOUT_FILENO);
    if (*saved < 0) {
        (void)cplus_fclose(fp);
        return -1;
    }
    fd = fileno(fp);
    if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0) {
        (void)close(*saved);
        (void)cplus_fclose(fp);
        return -1;
    }
    *outfp = fp;
    return 0;
}

static void stdout_restore(FILE *fp, int saved)
{
    (void)fflush(stdout);
    (void)dup2(saved, STDOUT_FILENO);
    (void)close(saved);
    (void)cplus_fclose(fp);
}
#endif /* !_WIN32 */

/* Call the attributed wrappers through un-attributed function pointers when
 * deliberately passing a NULL format: this exercises the runtime guard
 * without tripping -Wformat-security at compile time. */
typedef int (*plain_printf_fn)(const char *, ...);
typedef int (*plain_fprintf_fn)(FILE *, const char *, ...);
typedef int (*plain_snprintf_fn)(char *, size_t, const char *, ...);

/* ================================================================== */
/* 01. fopen                                                            */
/* ================================================================== */

/* Scenarios: valid open, truncation on "wb", missing path, NULL args */
static void sc_fopen_valid(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "fopen_valid", 1);
    fp = cplus_fopen(p, "wb");
    r->i[0] = (fp != NULL);
    if (fp) r->i[1] = cplus_fclose(fp);
    fp = cplus_fopen(p, "rb");
    r->i[2] = (fp != NULL);
    if (fp) r->i[3] = cplus_fclose(fp);
}

static void sc_fopen_trunc(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fopen_trunc", 1);
    put_file(p, "ABCD", 4);
    fp = cplus_fopen(p, "wb");
    r->i[0] = (fp != NULL);
    if (fp) r->i[1] = cplus_fclose(fp);
    r->z[0] = get_file(p, buf, sizeof buf);
    res_sets(r, 0, buf);
}

static void sc_fopen_missing(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "fopen_missing", 0); /* never created */
    errno = 0;
    fp = cplus_fopen(p, "rb");
    r->i[0] = (fp == NULL);
    r->e[0] = errno;
    if (fp) (void)cplus_fclose(fp);
}

static void sc_fopen_null(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "fopen_null", 0);
    errno = 0;
    fp = cplus_fopen(NULL, "rb");
    r->i[0] = (fp == NULL);
    r->e[0] = errno;
    errno = 0;
    fp = cplus_fopen(p, NULL);
    r->i[1] = (fp == NULL);
    r->e[1] = errno;
}

/* METHOD: fopen - valid open, truncation on "wb", missing path, NULL args */
static void m_io_01_fopen(void)
{
    Res r;
    run_scenario("fopen", "valid", sc_fopen_valid, &r);
    CHECK(r.i[0] == 1 && r.i[1] == 0, "fopen: valid open/close");
    CHECK(r.i[2] == 1 && r.i[3] == 0, "fopen: reopen for read");
    run_scenario("fopen", "truncation", sc_fopen_trunc, &r);
    CHECK(r.i[0] == 1 && r.i[1] == 0, "fopen: truncate open/close");
    CHECK(r.z[0] == 0, "fopen: \"wb\" truncated existing content");
    run_scenario("fopen", "missing", sc_fopen_missing, &r);
    CHECK(r.i[0] == 1 && r.e[0] != 0, "fopen: missing path fails with errno");
    run_scenario("fopen", "null-args", sc_fopen_null, &r);
    CHECK(r.i[0] == 1 && r.e[0] == EINVAL, "fopen: NULL name guarded");
    CHECK(r.i[1] == 1 && r.e[1] == EINVAL, "fopen: NULL mode guarded");
}

/* ================================================================== */
/* 02. fclose (double-close refusal)                                    */
/* ================================================================== */

static void sc_fclose_valid(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "fclose_valid", 1);
    fp = cplus_fopen(p, "wb");
    r->i[0] = (fp != NULL);
    if (fp) r->i[1] = cplus_fclose(fp);
}

static void sc_fclose_double(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "fclose_double", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_fclose(fp);
    errno = 0;
    r->i[1] = cplus_fclose(fp); /* guarded: must NOT hand stale FILE* to libc */
    r->e[0] = errno;
}

static void sc_fclose_null(Res *r)
{
    errno = 0;
    r->i[0] = cplus_fclose(NULL);
    r->e[0] = errno;
}

/* METHOD: fclose - close, guarded double-close refusal, guarded NULL */
static void m_io_02_fclose(void)
{
    Res r;
    run_scenario("fclose", "valid", sc_fclose_valid, &r);
    CHECK(r.i[0] == 1 && r.i[1] == 0, "fclose: first close succeeds");
    run_scenario("fclose", "double", sc_fclose_double, &r);
    CHECK(r.i[0] == 0, "fclose: first close of double test succeeds");
    CHECK(r.i[1] == EOF && r.e[0] == EBADF,
          "fclose: second close refused (no raw double close)");
    run_scenario("fclose", "null", sc_fclose_null, &r);
    CHECK(r.i[0] == EOF && r.e[0] == EINVAL, "fclose: NULL guarded");
}

/* ================================================================== */
/* 03. fread                                                            */
/* ================================================================== */

static void sc_fread_valid(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fread_valid", 1);
    put_file(p, "ABCDEFGH", 8);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    r->z[0] = fread(buf, 1, 4, fp);
    res_setb(r, buf, r->z[0]);
    r->i[0] = cplus_fclose(fp);
}

static void sc_fread_zero(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fread_zero", 1);
    put_file(p, "data", 4);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    buf[0] = 'Z';
    r->z[0] = fread(buf, 0, 10, fp);
    r->b[0] = (unsigned char)buf[0];
    r->z[1] = fread(buf, 10, 0, fp);
    r->b[1] = (unsigned char)buf[0];
    r->i[0] = cplus_ftell(fp) == 0;
    r->i[1] = cplus_fclose(fp);
}

static void sc_fread_short_eof(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fread_short", 1);
    put_file(p, "xy", 2);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    bzero_local(buf, sizeof buf);
    r->z[0] = fread(buf, 1, 8, fp);
    res_setb(r, buf, r->z[0]);
    r->i[0] = cplus_feof(fp);
    r->i[1] = cplus_ferror(fp);
    r->i[2] = cplus_fclose(fp);
}

static void sc_fread_overflow(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fread_overflow", 1);
    put_file(p, "abcd", 4);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    errno = 0;
    r->z[0] = fread(buf, (size_t)-1, 2, fp);
    r->e[0] = errno;
    r->i[0] = cplus_fclose(fp);
}

static void sc_fread_null(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fread_null", 1);
    put_file(p, "abcd", 4);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    errno = 0;
    r->z[0] = fread(NULL, 1, 1, fp);
    r->e[0] = errno;
    errno = 0;
    r->z[1] = fread(buf, 1, 1, NULL);
    r->e[1] = errno;
    r->i[0] = cplus_fclose(fp);
}

/* METHOD: fread - valid, zero-size, short read + EOF, overflow, NULL guard */
static void m_io_03_fread(void)
{
    Res r;
    run_scenario("fread", "valid", sc_fread_valid, &r);
    CHECK(r.z[0] == 4 && r.nb == 4, "fread: four items read");
    CHECK(bequal(r.b, "ABCD", 4), "fread: bytes correct");
    run_scenario("fread", "zero", sc_fread_zero, &r);
    CHECK(r.z[0] == 0 && r.z[1] == 0, "fread: zero size/count return 0");
    CHECK(r.i[0] == 1 && r.b[0] == 'Z' && r.b[1] == 'Z',
          "fread: zero-size does not touch buffer or stream");
    run_scenario("fread", "short-eof", sc_fread_short_eof, &r);
    CHECK(r.z[0] == 2 && r.nb == 2, "fread: short read returns 2");
    CHECK(bequal(r.b, "xy", 2), "fread: short read bytes correct");
    CHECK(r.i[0] == 1 && r.i[1] == 0, "fread: EOF set, no error");
    run_scenario("fread", "overflow", sc_fread_overflow, &r);
    CHECK(r.z[0] == 0 && r.e[0] == EOVERFLOW,
          "fread: size*count overflow refused");
    run_scenario("fread", "null-args", sc_fread_null, &r);
    CHECK(r.z[0] == 0 && r.z[1] == 0, "fread: NULL buffer/stream guarded");
}

/* ================================================================== */
/* 04. fwrite                                                           */
/* ================================================================== */

static void sc_fwrite_valid(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fwrite_valid", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    r->z[0] = fwrite("WXYZ", 1, 4, fp);
    r->i[0] = cplus_fclose(fp);
    r->z[1] = get_file(p, buf, sizeof buf);
    res_sets(r, 0, buf);
}

static void sc_fwrite_zero(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fwrite_zero", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    r->z[0] = fwrite("abcd", 0, 8, fp);
    r->z[1] = fwrite("abcd", 8, 0, fp);
    r->i[0] = cplus_fclose(fp);
    r->z[2] = get_file(p, buf, sizeof buf);
    res_sets(r, 0, buf);
}

static void sc_fwrite_overflow(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "fwrite_overflow", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    errno = 0;
    r->z[0] = fwrite("abcd", (size_t)-1, 2, fp);
    r->e[0] = errno;
    r->i[0] = cplus_fclose(fp);
}

static void sc_fwrite_null(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "fwrite_null", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    errno = 0;
    r->z[0] = fwrite(NULL, 1, 1, fp);
    r->e[0] = errno;
    errno = 0;
    r->z[1] = fwrite("abcd", 1, 1, NULL);
    r->e[1] = errno;
    r->i[0] = cplus_fclose(fp);
}

/* METHOD: fwrite - valid, zero-size, overflow guard, NULL guard */
static void m_io_04_fwrite(void)
{
    Res r;
    run_scenario("fwrite", "valid", sc_fwrite_valid, &r);
    CHECK(r.z[0] == 4 && r.i[0] == 0, "fwrite: four items written");
    CHECK(r.z[1] == 4 && ceq(r.s[0], "WXYZ"), "fwrite: file content correct");
    run_scenario("fwrite", "zero", sc_fwrite_zero, &r);
    CHECK(r.z[0] == 0 && r.z[1] == 0, "fwrite: zero size/count return 0");
    CHECK(r.z[2] == 0, "fwrite: zero-size writes nothing");
    run_scenario("fwrite", "overflow", sc_fwrite_overflow, &r);
    CHECK(r.z[0] == 0 && r.e[0] == EOVERFLOW,
          "fwrite: size*count overflow refused");
    run_scenario("fwrite", "null-args", sc_fwrite_null, &r);
    CHECK(r.z[0] == 0 && r.z[1] == 0, "fwrite: NULL buffer/stream guarded");
}

/* ================================================================== */
/* 05. fseek / ftell                                                    */
/* ================================================================== */

static void sc_seek_tell(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "seek_tell", 1);
    put_file(p, "0123456789", 10);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_fseek(fp, 5, SEEK_SET);
    r->l[0] = cplus_ftell(fp);
    r->i[1] = cplus_getc(fp);
    r->i[2] = cplus_fseek(fp, -2, SEEK_CUR);
    r->l[1] = cplus_ftell(fp);
    r->i[3] = cplus_fseek(fp, 0, SEEK_END);
    r->l[2] = cplus_ftell(fp);
    r->i[4] = cplus_fseek(fp, 1000, SEEK_SET);
    r->l[3] = cplus_ftell(fp);
    r->i[5] = cplus_fseek(fp, 0, SEEK_SET);
    errno = 0;
    r->i[6] = cplus_fseek(fp, 0, 12345);
    r->e[0] = errno;
    r->i[7] = cplus_fclose(fp);
}

/* METHOD: fseek/ftell - set/cur/end seeks, position, invalid whence */
static void m_io_05_fseek_ftell(void)
{
    Res r;
    run_scenario("fseek/ftell", "positions", sc_seek_tell, &r);
    CHECK(r.i[0] == 0 && r.l[0] == 5 && r.i[1] == '5',
          "fseek/ftell: SEEK_SET position and read");
    CHECK(r.i[2] == 0 && r.l[1] == 4, "fseek/ftell: SEEK_CUR position");
    CHECK(r.i[3] == 0 && r.l[2] == 10, "fseek/ftell: SEEK_END position");
    CHECK(r.i[4] == 0 && r.l[3] == 1000, "fseek/ftell: seek past EOF");
    CHECK(r.i[5] == 0, "fseek/ftell: rewind via seek");
    CHECK(r.i[6] == -1, "fseek/ftell: invalid whence fails");
}

/* ================================================================== */
/* 06. rewind                                                           */
/* ================================================================== */

static void sc_rewind_eof(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "rewind_eof", 1);
    put_file(p, "abc", 3);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_getc(fp);
    r->i[1] = cplus_getc(fp);
    r->i[2] = cplus_getc(fp);
    r->i[3] = cplus_getc(fp); /* EOF */
    r->i[4] = cplus_feof(fp);
    cplus_rewind(fp);
    r->i[5] = cplus_feof(fp);
    r->l[0] = cplus_ftell(fp);
    r->i[6] = cplus_getc(fp);
    r->i[7] = cplus_fclose(fp);
}

/* METHOD: rewind - reset to start and clear EOF */
static void m_io_06_rewind(void)
{
    Res r;
    run_scenario("rewind", "eof-reset", sc_rewind_eof, &r);
    CHECK(r.i[3] == EOF && r.i[4] == 1, "rewind: EOF reached before rewind");
    CHECK(r.i[5] == 0 && r.l[0] == 0, "rewind: EOF cleared, position 0");
    CHECK(r.i[6] == 'a', "rewind: first byte readable again");
}

/* ================================================================== */
/* 07. fgetpos / fsetpos                                                */
/* ================================================================== */

static void sc_pos_roundtrip(Res *r)
{
    char p[PATH_CAP];
    fpos_t pos;
    FILE *fp;
    path_make(p, sizeof p, "fpos", 1);
    put_file(p, "abcdef", 6);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_fgetpos(fp, &pos);
    r->i[1] = cplus_getc(fp);
    r->i[2] = cplus_getc(fp);
    r->l[0] = cplus_ftell(fp);
    r->i[3] = cplus_fsetpos(fp, &pos);
    r->l[1] = cplus_ftell(fp);
    r->i[4] = cplus_getc(fp);
    r->i[5] = cplus_fclose(fp);
}

/* METHOD: fgetpos/fsetpos - save and restore a stream position */
static void m_io_07_fgetpos_fsetpos(void)
{
    Res r;
    run_scenario("fgetpos/fsetpos", "roundtrip", sc_pos_roundtrip, &r);
    CHECK(r.i[0] == 0 && r.i[1] == 'a' && r.i[2] == 'b' && r.l[0] == 2,
          "fgetpos/fsetpos: position captured after two reads");
    CHECK(r.i[3] == 0 && r.l[1] == 0 && r.i[4] == 'a',
          "fgetpos/fsetpos: position restored");
}

/* ================================================================== */
/* 08. fgets                                                            */
/* ================================================================== */

static void sc_fgets_lines(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fgets_lines", 1);
    put_file(p, "hello\nworld\n", 12);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = (cplus_fgets(buf, 8, fp) != NULL);
    res_sets(r, 0, buf);
    r->i[1] = (cplus_fgets(buf, 8, fp) != NULL);
    res_sets(r, 1, buf);
    errno = 0;
    r->i[2] = (cplus_fgets(buf, 8, fp) == NULL);
    r->e[0] = cplus_feof(fp);
    r->i[3] = cplus_fclose(fp);
}

/* METHOD: fgets - bounded line reads and EOF return */
static void m_io_08_fgets(void)
{
    Res r;
    run_scenario("fgets", "lines", sc_fgets_lines, &r);
    CHECK(r.i[0] == 1 && ceq(r.s[0], "hello\n"), "fgets: first line");
    CHECK(r.i[1] == 1 && ceq(r.s[1], "world\n"), "fgets: second line");
    CHECK(r.i[2] == 1 && r.e[0] == 1, "fgets: EOF returns NULL and sets feof");
}

/* ================================================================== */
/* 09. fputs                                                            */
/* ================================================================== */

static void sc_fputs_basic(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fputs_basic", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_fputs("hello", fp);
    r->i[1] = cplus_fputs("", fp); /* empty string is a valid no-op */
    r->i[2] = cplus_fclose(fp);
    r->z[0] = get_file(p, buf, sizeof buf);
    res_sets(r, 0, buf);
}

/* METHOD: fputs - write string and empty string, verify file bytes */
static void m_io_09_fputs(void)
{
    Res r;
    run_scenario("fputs", "basic", sc_fputs_basic, &r);
    CHECK(r.i[0] >= 0 && r.i[1] >= 0 && r.i[2] == 0,
          "fputs: non-negative returns and clean close");
    CHECK(r.z[0] == 5 && ceq(r.s[0], "hello"), "fputs: file content correct");
}

/* ================================================================== */
/* 10. getc / putc                                                      */
/* ================================================================== */

static void sc_getc_putc(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "getc_putc", 1);
    fp = cplus_fopen(p, "w+b");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_putc('A', fp);
    r->i[1] = cplus_putc('B', fp);
    r->i[2] = cplus_fseek(fp, 0, SEEK_SET);
    r->i[3] = cplus_getc(fp);
    r->i[4] = cplus_getc(fp);
    r->i[5] = cplus_getc(fp); /* EOF */
    r->i[6] = cplus_feof(fp);
    r->i[7] = cplus_fclose(fp);
}

/* METHOD: getc/putc - character round-trip and EOF */
static void m_io_10_getc_putc(void)
{
    Res r;
    run_scenario("getc/putc", "roundtrip", sc_getc_putc, &r);
    CHECK(r.i[0] == 'A' && r.i[1] == 'B' && r.i[2] == 0,
          "getc/putc: putc returns written byte");
    CHECK(r.i[3] == 'A' && r.i[4] == 'B' && r.i[5] == EOF && r.i[6] == 1,
          "getc/putc: reads, EOF and feof");
}

/* ================================================================== */
/* 11. ungetc                                                           */
/* ================================================================== */

static void sc_ungetc_basic(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "ungetc", 1);
    put_file(p, "abc", 3);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_getc(fp);
    r->i[1] = cplus_ungetc('a', fp);
    r->i[2] = cplus_getc(fp);
    r->i[3] = cplus_getc(fp);
    r->i[4] = cplus_ungetc(EOF, fp); /* defined failure, returns EOF */
    r->i[5] = cplus_fclose(fp);
}

/* METHOD: ungetc - push back a character, EOF push-back failure */
static void m_io_11_ungetc(void)
{
    Res r;
    run_scenario("ungetc", "pushback", sc_ungetc_basic, &r);
    CHECK(r.i[0] == 'a' && r.i[1] == 'a' && r.i[2] == 'a',
          "ungetc: pushed-back byte is read again");
    CHECK(r.i[3] == 'b', "ungetc: stream continues after push-back");
    CHECK(r.i[4] == EOF, "ungetc: EOF argument fails cleanly");
}

/* ================================================================== */
/* 12. feof / ferror / clearerr                                         */
/* ================================================================== */

static void sc_eof_clear(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "eof_clear", 1);
    put_file(p, "x", 1);
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_ferror(fp);
    r->i[1] = cplus_feof(fp);
    r->i[2] = cplus_getc(fp);
    r->i[3] = cplus_getc(fp); /* EOF */
    r->i[4] = cplus_feof(fp);
    r->i[5] = cplus_ferror(fp);
    cplus_clearerr(fp);
    r->i[6] = cplus_feof(fp);
    r->i[7] = cplus_ferror(fp);
    r->i[8] = cplus_fclose(fp);
}

/* METHOD: feof/ferror/clearerr - EOF and error-indicator transitions */
static void m_io_12_feof_ferror_clearerr(void)
{
    Res r;
    run_scenario("feof/ferror/clearerr", "eof-transition", sc_eof_clear, &r);
    CHECK(r.i[0] == 0 && r.i[1] == 0, "feof/ferror: clean stream initially");
    CHECK(r.i[3] == EOF && r.i[4] == 1 && r.i[5] == 0,
          "feof/ferror: EOF indicator set, error indicator clear");
    CHECK(r.i[6] == 0 && r.i[7] == 0,
          "feof/ferror: clearerr resets both indicators");
}

/* ================================================================== */
/* 13. setvbuf                                                          */
/* ================================================================== */

static void sc_setvbuf_modes(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    char ubuf[64];
    FILE *fp;
    path_make(p, sizeof p, "setvbuf", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_setvbuf(fp, NULL, _IONBF, 0);
    r->i[1] = cplus_fputs("one", fp);
    r->i[2] = cplus_fclose(fp);

    fp = cplus_fopen(p, "ab");
    if (!fp) { r->i[3] = -100; }
    else {
        r->i[3] = cplus_setvbuf(fp, ubuf, _IOFBF, sizeof ubuf);
        r->i[4] = cplus_fputs("two", fp);
        r->i[5] = cplus_fclose(fp);
    }
    fp = cplus_fopen(p, "rb");
    if (!fp) { r->i[6] = -100; }
    else {
        r->i[6] = cplus_setvbuf(fp, NULL, 12345, 64); /* invalid mode */
        r->z[0] = cplus_fread(buf, 1, sizeof buf - 1u, fp);
        res_sets(r, 0, buf);
        r->i[7] = cplus_fclose(fp);
    }
}

/* METHOD: setvbuf - unbuffered, fully buffered, invalid mode */
static void m_io_13_setvbuf(void)
{
    Res r;
    run_scenario("setvbuf", "modes", sc_setvbuf_modes, &r);
    CHECK(r.i[0] == 0 && r.i[1] >= 0 && r.i[2] == 0,
          "setvbuf: _IONBF configured, write and close ok");
    CHECK(r.i[3] == 0 && r.i[4] >= 0 && r.i[5] == 0,
          "setvbuf: _IOFBF with user buffer ok");
    CHECK(r.i[6] != 0, "setvbuf: invalid mode rejected");
    CHECK(r.z[0] == 6 && ceq(r.s[0], "onetwo"),
          "setvbuf: buffered data flushed in order");
}

/* ================================================================== */
/* 14. tmpfile                                                          */
/* ================================================================== */

static void sc_tmpfile_roundtrip(Res *r)
{
    char buf[16];
    FILE *fp = cplus_tmpfile();
    r->i[0] = (fp != NULL);
    if (!fp) return;
    r->z[0] = cplus_fwrite("TEMP", 1, 4, fp);
    cplus_rewind(fp);
    r->z[1] = cplus_fread(buf, 1, 4, fp);
    res_setb(r, buf, r->z[1]);
    r->i[1] = cplus_fclose(fp); /* anonymous file is removed by libc */
}

/* METHOD: tmpfile - anonymous stream write/read/close */
static void m_io_14_tmpfile(void)
{
    Res r;
    run_scenario("tmpfile", "roundtrip", sc_tmpfile_roundtrip, &r);
    CHECK(r.i[0] == 1, "tmpfile: anonymous stream created");
    CHECK(r.z[0] == 4 && r.z[1] == 4 && bequal(r.b, "TEMP", 4),
          "tmpfile: write/read round-trip");
    CHECK(r.i[1] == 0, "tmpfile: close succeeds");
}

/* ================================================================== */
/* 15. freopen                                                          */
/* ================================================================== */

static void sc_freopen_redirect(Res *r)
{
    char a[PATH_CAP];
    char b[PATH_CAP];
    char buf[16];
    FILE *fp;
    FILE *fp2;
    path_make(a, sizeof a, "freopen_a", 1);
    path_make(b, sizeof b, "freopen_b", 1);
    fp = cplus_fopen(a, "wb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = cplus_fputs("old", fp);
    (void)cplus_fflush(fp);
    fp2 = cplus_freopen(b, "wb", fp);
    r->i[1] = (fp2 != NULL);
    if (fp2) {
        r->i[2] = cplus_fputs("new", fp2);
        r->i[3] = cplus_fclose(fp2);
    }
    r->z[0] = get_file(b, buf, sizeof buf);
    res_sets(r, 0, buf);
}

static void sc_freopen_null(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "freopen_null", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    errno = 0;
    r->i[0] = (cplus_freopen(NULL, "wb", fp) == NULL);
    r->e[0] = errno;
    errno = 0;
    r->i[1] = (cplus_freopen(p, NULL, fp) == NULL);
    r->e[1] = errno;
    r->i[2] = cplus_fclose(fp);
}

/* METHOD: freopen - redirect an existing stream, NULL guards */
static void m_io_15_freopen(void)
{
    Res r;
    run_scenario("freopen", "redirect", sc_freopen_redirect, &r);
    CHECK(r.i[0] >= 0 && r.i[1] == 1 && r.i[2] >= 0 && r.i[3] == 0,
          "freopen: existing stream redirected and closed");
    CHECK(r.z[0] == 3 && ceq(r.s[0], "new"), "freopen: new target content");
    run_scenario("freopen", "null-args", sc_freopen_null, &r);
    CHECK(r.i[0] == 1 && r.e[0] == EINVAL, "freopen: NULL path guarded");
    CHECK(r.i[1] == 1 && r.e[1] == EINVAL, "freopen: NULL mode guarded");
}

/* ================================================================== */
/* 16. remove / rename                                                  */
/* ================================================================== */

static void sc_remove_rename(Res *r)
{
    char a[PATH_CAP];
    char b[PATH_CAP];
    char buf[16];
    path_make(a, sizeof a, "rename_a", 1);
    path_make(b, sizeof b, "rename_b", 1);
    put_file(a, "DATA", 4);
    r->i[0] = cplus_rename(a, b);
    r->z[0] = get_file(a, buf, sizeof buf);
    r->i[1] = (r->z[0] == 0);
    r->z[1] = get_file(b, buf, sizeof buf);
    res_sets(r, 0, buf);
    r->i[2] = cplus_remove(b);
    r->z[2] = get_file(b, buf, sizeof buf);
    r->i[3] = (r->z[2] == 0);
    r->i[4] = cplus_remove(b); /* already gone */
    r->i[5] = cplus_rename(b, a); /* source gone */
}

/* METHOD: remove/rename - rename, remove, missing-file failures */
static void m_io_16_remove_rename(void)
{
    Res r;
    run_scenario("remove/rename", "lifecycle", sc_remove_rename, &r);
    CHECK(r.i[0] == 0 && r.i[1] == 1, "remove/rename: rename removes source");
    CHECK(r.z[1] == 4 && ceq(r.s[0], "DATA"), "remove/rename: target content");
    CHECK(r.i[2] == 0 && r.i[3] == 1, "remove/rename: remove deletes file");
    CHECK(r.i[4] != 0 && r.i[5] != 0,
          "remove/rename: missing file operations fail");
}

/* ================================================================== */
/* 17. printf                                                           */
/* ================================================================== */

static void sc_printf_basic(Res *r)
{
#ifdef _WIN32
    /* No POSIX dup2 on Windows: exercise the defined no-output case only. */
    r->i[0] = printf("");
#else
    char p[PATH_CAP];
    FILE *fp = NULL;
    int saved = -1;
    path_make(p, sizeof p, "printf_basic", 1);
    if (stdout_redirect(p, &fp, &saved) != 0) { r->i[0] = -100; return; }
    errno = 0;
    r->i[0] = printf("%s:%d", "m", 7);
    r->i[1] = printf("");
    stdout_restore(fp, saved);
    r->z[0] = get_file(p, (char *)r->b, sizeof r->b);
#endif
}

static void sc_printf_null(Res *r)
{
    const char *volatile bad = NULL;
    errno = 0;
    r->i[0] = ((plain_printf_fn)cplus_printf)(bad); /* guarded */
    r->e[0] = errno;
}

/* METHOD: printf - formatted output, return value, NULL-format guard */
static void m_io_17_printf(void)
{
    Res r;
    run_scenario("printf", "basic", sc_printf_basic, &r);
#ifdef _WIN32
    CHECK(r.i[0] == 0, "printf: empty format writes nothing");
#else
    CHECK(r.i[0] == 3 && r.i[1] == 0, "printf: formatted return values");
    CHECK(r.z[0] == 3 && bequal(r.b, "m:7", 3), "printf: formatted bytes");
#endif
    run_scenario("printf", "null-format", sc_printf_null, &r);
    CHECK(r.i[0] == -1 && r.e[0] == EINVAL, "printf: NULL format guarded");
}

/* ================================================================== */
/* 18. fprintf                                                          */
/* ================================================================== */

static void sc_fprintf_basic(Res *r)
{
    char p[PATH_CAP];
    char buf[16];
    FILE *fp;
    path_make(p, sizeof p, "fprintf_basic", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    r->i[0] = fprintf(fp, "%d-%s", 12, "ok");
    r->i[1] = cplus_fclose(fp);
    r->z[0] = get_file(p, buf, sizeof buf);
    res_sets(r, 0, buf);
}

static void sc_fprintf_null(Res *r)
{
    char p[PATH_CAP];
    const char *volatile bad = NULL;
    FILE *fp;
    path_make(p, sizeof p, "fprintf_null", 1);
    fp = cplus_fopen(p, "wb");
    if (!fp) { r->i[0] = -100; return; }
    errno = 0;
    r->i[0] = ((plain_fprintf_fn)cplus_fprintf)(fp, bad); /* guarded */
    r->e[0] = errno;
    errno = 0;
    r->i[1] = ((plain_fprintf_fn)cplus_fprintf)(NULL, "x"); /* guarded */
    r->e[1] = errno;
    r->i[2] = cplus_fclose(fp);
}

/* METHOD: fprintf - formatted file output, NULL stream/format guards */
static void m_io_18_fprintf(void)
{
    Res r;
    run_scenario("fprintf", "basic", sc_fprintf_basic, &r);
    CHECK(r.i[0] == 5 && r.i[1] == 0, "fprintf: formatted return value");
    CHECK(r.z[0] == 5 && ceq(r.s[0], "12-ok"), "fprintf: file content correct");
    run_scenario("fprintf", "null-args", sc_fprintf_null, &r);
    CHECK(r.i[0] == -1 && r.e[0] == EINVAL, "fprintf: NULL format guarded");
    CHECK(r.i[1] == -1 && r.e[1] == EINVAL, "fprintf: NULL stream guarded");
}

/* ================================================================== */
/* 19. snprintf                                                         */
/* ================================================================== */

static void sc_snprintf_basic(Res *r)
{
    char buf[32];
    r->i[0] = cplus_snprintf(buf, sizeof buf, "%s-%d", "ab", 3);
    res_sets(r, 0, buf);
}

static void sc_snprintf_trunc(Res *r)
{
    char small[8];
    size_t k;
    for (k = 0; k < sizeof small; ++k) small[k] = 'Z';
    r->i[0] = cplus_snprintf(small, 3, "abcdef");
    r->s[0][0] = small[0];
    r->s[0][1] = small[1];
    r->s[0][2] = small[2];
    r->s[0][3] = '\0';
    r->b[0] = (unsigned char)small[7]; /* untouched tail sentinel */
}

static void sc_snprintf_zero(Res *r)
{
    char buf[8];
    size_t k;
    for (k = 0; k < sizeof buf; ++k) buf[k] = 'Z';
    r->i[0] = cplus_snprintf(buf, 0, "abc");
    r->b[0] = (unsigned char)buf[0];
}

static void sc_snprintf_null(Res *r)
{
    char buf[8];
    const char *volatile bad = NULL;
    errno = 0;
    r->i[0] = ((plain_snprintf_fn)cplus_snprintf)(NULL, sizeof buf, "x");
    r->e[0] = errno;
    errno = 0;
    r->i[1] = ((plain_snprintf_fn)cplus_snprintf)(buf, sizeof buf, bad);
    r->e[1] = errno;
}

/* METHOD: snprintf - formatting, truncation, zero size, NULL guards */
static void m_io_19_snprintf(void)
{
    Res r;
    run_scenario("snprintf", "basic", sc_snprintf_basic, &r);
    CHECK(r.i[0] == 4 && ceq(r.s[0], "ab-3"), "snprintf: formatted string");
    run_scenario("snprintf", "truncation", sc_snprintf_trunc, &r);
    CHECK(r.i[0] == 6 && ceq(r.s[0], "ab"), "snprintf: truncated with NUL");
    CHECK(r.b[0] == 'Z', "snprintf: bytes past size untouched");
    run_scenario("snprintf", "zero-size", sc_snprintf_zero, &r);
    CHECK(r.i[0] == 3 && r.b[0] == 'Z',
          "snprintf: zero size reports length, writes nothing");
    run_scenario("snprintf", "null-args", sc_snprintf_null, &r);
    CHECK(r.i[0] == -1 && r.e[0] == EINVAL, "snprintf: NULL dest guarded");
    CHECK(r.i[1] == -1 && r.e[1] == EINVAL, "snprintf: NULL format guarded");
}

/* ================================================================== */
/* 20. checked I/O APIs (SafeCRT layer only; guarded otherwise)         */
/* ================================================================== */

#ifdef HAVE_CHECKED_IO
static void sc_checked_roundtrip(Res *r)
{
    char p[PATH_CAP];
    const unsigned char src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    unsigned char out[16];
    FILE *fp;
    path_make(p, sizeof p, "chk_roundtrip", 1);
    fp = cplus_fopen_s(p, "w+b");
    r->i[0] = (fp != NULL);
    if (!fp) return;
    r->z[0] = cplus_fwrite_s(src, sizeof src, 1, sizeof src, fp);
    cplus_rewind(fp);
    bzero_local(out, sizeof out);
    r->z[1] = cplus_fread_s(out, sizeof out, 1, sizeof src, fp);
    res_setb(r, out, r->z[1]);
    r->i[1] = cplus_fclose_s(fp);
    errno = 0;
    r->i[2] = cplus_fclose_s(fp); /* bounded double-close protection */
    r->e[0] = errno;
}

static void sc_checked_clamp(Res *r)
{
    char p[PATH_CAP];
    const unsigned char src[8] = { 11, 12, 13, 14, 15, 16, 17, 18 };
    unsigned char out[8];
    FILE *fp;
    path_make(p, sizeof p, "chk_clamp", 1);
    fp = cplus_fopen_s(p, "w+b");
    if (!fp) { r->i[0] = -100; return; }
    r->z[0] = cplus_fwrite_s(src, 4, 1, 8, fp); /* bmax clamps to 4 */
    cplus_rewind(fp);
    bzero_local(out, sizeof out);
    r->z[1] = cplus_fread_s(out, 4, 1, 8, fp);
    res_setb(r, out, r->z[1]);
    r->i[0] = cplus_fclose_s(fp);
}

static void sc_checked_short_eof(Res *r)
{
    char p[PATH_CAP];
    unsigned char out[16];
    FILE *fp;
    path_make(p, sizeof p, "chk_short", 1);
    put_file(p, "xy", 2);
    fp = cplus_fopen_s(p, "rb");
    if (!fp) { r->i[0] = -100; return; }
    bzero_local(out, sizeof out);
    r->z[0] = cplus_fread_s(out, sizeof out, 1, 8, fp);
    res_setb(r, out, r->z[0]);
    r->i[0] = cplus_feof(fp);
    r->i[1] = cplus_fclose_s(fp);
}

static void sc_checked_invalid(Res *r)
{
    char p[PATH_CAP];
    unsigned char buf[8];
    FILE *fp;
    path_make(p, sizeof p, "chk_invalid", 1);
    fp = cplus_fopen_s(p, "w+b");
    if (!fp) { r->i[0] = -100; return; }

    errno = 0;
    r->z[0] = cplus_fread_s(buf, sizeof buf, 0, 8, fp);
    r->e[0] = errno;
    r->z[1] = cplus_fwrite_s(buf, sizeof buf, 0, 8, fp);
    r->z[2] = cplus_fread_s(buf, sizeof buf, 1, 0, fp);
    r->z[3] = cplus_fwrite_s(buf, sizeof buf, 1, 0, fp);
    r->z[4] = cplus_fread_s(buf, sizeof buf, (size_t)-1, 2, fp);
    r->z[5] = cplus_fwrite_s(buf, sizeof buf, (size_t)-1, 2, fp);
    r->z[6] = cplus_fread_s(NULL, sizeof buf, 1, 1, fp);
    r->z[7] = cplus_fwrite_s(NULL, sizeof buf, 1, 1, fp);
    r->z[8] = cplus_fread_s(buf, sizeof buf, 1, 1, NULL);
    r->z[9] = cplus_fwrite_s(buf, sizeof buf, 1, 1, NULL);
    r->i[0] = cplus_fclose_s(fp);

    errno = 0;
    r->i[1] = (cplus_fopen_s(NULL, "rb") == NULL);
    r->e[1] = errno;
    errno = 0;
    r->i[2] = (cplus_fopen_s(p, NULL) == NULL);
    r->e[2] = errno;
    errno = 0;
    r->i[3] = (cplus_fclose_s(NULL) == EOF);
    r->e[3] = errno;
}

/* METHOD: cplus_fopen_s/fclose_s/fread_s/fwrite_s - checked I/O APIs */
static void m_io_20_checked_io(void)
{
    Res r;
    run_scenario("checked_io", "roundtrip", sc_checked_roundtrip, &r);
    CHECK(r.i[0] == 1, "checked_io: fopen_s succeeds");
    CHECK(r.z[0] == 8 && r.z[1] == 8 && r.nb == 8,
          "checked_io: fwrite_s/fread_s round-trip");
    CHECK(bequal(r.b, "\1\2\3\4\5\6\7\10", 8), "checked_io: bytes correct");
    CHECK(r.i[1] == 0, "checked_io: fclose_s succeeds");
    CHECK(r.i[2] == EOF && r.e[0] == EINVAL,
          "checked_io: double close refused");

    run_scenario("checked_io", "bmax-clamp", sc_checked_clamp, &r);
    CHECK(r.z[0] == 4 && r.z[1] == 4, "checked_io: bmax clamps count");
    CHECK(bequal(r.b, "\13\14\15\16", 4), "checked_io: clamped bytes correct");

    run_scenario("checked_io", "short-eof", sc_checked_short_eof, &r);
    CHECK(r.z[0] == 2 && r.nb == 2 && bequal(r.b, "xy", 2),
          "checked_io: short read returns available bytes");
    CHECK(r.i[0] == 1, "checked_io: EOF indicator set after short read");

    run_scenario("checked_io", "invalid-args", sc_checked_invalid, &r);
    CHECK(r.z[0] == 0 && r.z[1] == 0 && r.z[2] == 0 && r.z[3] == 0,
          "checked_io: zero size/count return 0");
    CHECK(r.z[4] == 0 && r.z[5] == 0,
          "checked_io: overflowing size*count clamped to 0");
    CHECK(r.z[6] == 0 && r.z[7] == 0 && r.z[8] == 0 && r.z[9] == 0,
          "checked_io: NULL buffer/stream guarded");
    CHECK(r.i[1] == 1 && r.e[1] == EINVAL, "checked_io: fopen_s NULL name");
    CHECK(r.i[2] == 1 && r.e[2] == EINVAL, "checked_io: fopen_s NULL mode");
    CHECK(r.i[3] == 1 && r.e[3] == EINVAL, "checked_io: fclose_s NULL stream");
}
#else  /* !HAVE_CHECKED_IO */
/* The checked I/O API lives in the SafeCRT library, which is intentionally
 * not linked in the plain-libc build. Keep the method present with a defined
 * baseline so both layers report the same method count. */
static void sc_checked_baseline(Res *r)
{
    char p[PATH_CAP];
    FILE *fp;
    path_make(p, sizeof p, "chk_baseline", 1);
    fp = cplus_fopen(p, "w+b");
    r->i[0] = (fp != NULL);
    if (fp) {
        r->z[0] = cplus_fwrite("Q", 1, 1, fp);
        cplus_rewind(fp);
        r->z[1] = cplus_fread((char *)r->b, 1, 1, fp);
        r->i[1] = cplus_fclose(fp);
    }
}

/* Guarded fallback: the SafeCRT library is not linked in this build. */
static void m_io_20_checked_io(void)
{
    Res r;
    run_scenario("checked_io", "baseline", sc_checked_baseline, &r);
    CHECK(r.i[0] == 1 && r.z[0] == 1 && r.z[1] == 1 && r.b[0] == 'Q',
          "checked_io: baseline ordinary-C I/O (checked APIs not linked)");
    fprintf(stderr, "note: checked I/O APIs require the SafeCRT layer; guarded out here\n");
}
#endif /* HAVE_CHECKED_IO */

/* ================================================================== */
/* main                                                                */
/* ================================================================== */
int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

#ifndef PLAIN_LIBC
    /* Inspect defined constraint return values rather than trapping. */
    cplus_runtime_set_violation_trap(0);
#endif

    m_io_01_fopen();
    m_io_02_fclose();
    m_io_03_fread();
    m_io_04_fwrite();
    m_io_05_fseek_ftell();
    m_io_06_rewind();
    m_io_07_fgetpos_fsetpos();
    m_io_08_fgets();
    m_io_09_fputs();
    m_io_10_getc_putc();
    m_io_11_ungetc();
    m_io_12_feof_ferror_clearerr();
    m_io_13_setvbuf();
    m_io_14_tmpfile();
    m_io_15_freopen();
    m_io_16_remove_rename();
    m_io_17_printf();
    m_io_18_fprintf();
    m_io_19_snprintf();
    m_io_20_checked_io();

    logging_set(0);
    temp_cleanup();

    printf("methods_stdio: %d/%d checks ok\n",
           g_checks - g_fails, g_checks);
    return 0;
}
