/* RED-TEAM agent 3/20 -- I/O, file, fs and JSON adversary (branch agent/modern-stdlib).
 *
 * Targets: stdlib/io.hp, stdlib/file.hp, stdlib/fs.hp, stdlib/json.hp.
 * ONLY this directory is touched; no source/header edits.
 *
 * 3600 deterministic cases, one per forked child, fixed-seed SplitMix64,
 * alarm(4) watchdog, 0xA5 canaries around every caller buffer, temp files
 * created only under tests/redblue20/red_io/out/tmp.
 *
 * Classification (child -> parent over a pipe):
 *   REFUSED       0  hostile input refused / defined correct behaviour
 *   UNEXPECTED_OK 1  hostile input accepted where the header promises refusal
 *   CRASH         2  signal, fatal exit, watchdog, or missing result (parent)
 *   CORRUPT       3  canary modified, buffer overrun, or stream/line misalignment
 *
 * Build (from repo root):
 *   ./build/c+ build tests/redblue20/red_io/harness.cp --default \
 *     --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -o tests/redblue20/red_io/out/harness
 * Run: timeout 60 <bin> [log-path]
 * Final line: RED20 io <cases> <crash> <corrupt> <unexpected_ok> <refused>
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"
#include "stdlib/json.hp"

#define N_CASES 3600
#define SEED 0x5245443230494F21ULL
#define GOLDEN 0x9E3779B97F4A7C15ULL
#define GUARD 32u
#define CAP 4096u
#define BIG 45000u
#define ALARM_SEC 4
#define TMPDIR "tests/redblue20/red_io/out/tmp"
#define LOGPATH "tests/redblue20/red_io/out/scenarios.log"

#define CLS_REFUSED 0
#define CLS_UNEXPECTED_OK 1
#define CLS_CRASH 2
#define CLS_CORRUPT 3

/* --- global canaried work areas (fixed extents for the C+ checker) -------- */
static char g_buf[GUARD + CAP + GUARD];
static char g_text[GUARD + BIG + GUARD];
static char g_rem[GUARD + CAP + GUARD];
static char g_src[GUARD + CAP + GUARD];
static char g_path[512];
static char g_path2[512];
static char g_line[256];
static unsigned long long g_rng;

struct case_result {
    int cls;
    int sub;
    int got;
};

/* --- deterministic PRNG -------------------------------------------------- */
static unsigned long long next_rand(void) {
    unsigned long long z;
    z = (g_rng += GOLDEN);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static unsigned int rnd(unsigned int n) {
    return (unsigned int)(next_rand() % (unsigned long long)n);
}

static void setres(struct case_result *r, int cls, int sub, int got) {
    r->cls = cls;
    r->sub = sub;
    r->got = got;
}

static void fill_a5(void *p, size_t n) { memset(p, 0xA5, n); }
static int all_a5(const char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i] != (char)0xA5) return 0;
    return 1;
}
/* Guard + "caller area beyond requested size" check for g_buf. */
static int buf_clean(size_t used) {
    size_t i;
    if (!all_a5(g_buf, GUARD)) return 0;
    if (!all_a5(g_buf + GUARD + CAP, GUARD)) return 0;
    if (used > CAP) return 0;
    for (i = used; i < CAP; i++)
        if (g_buf[GUARD + i] != (char)0xA5) return 0;
    return 1;
}
static int text_clean(void) {
    return all_a5(g_text, GUARD) && all_a5(g_text + GUARD + BIG, GUARD);
}
static int rem_clean(void) {
    return all_a5(g_rem, GUARD) && all_a5(g_rem + GUARD + CAP, GUARD);
}
static int src_clean(void) {
    return all_a5(g_src, GUARD) && all_a5(g_src + GUARD + CAP, GUARD);
}

/* --- temp file helpers --------------------------------------------------- */
static void case_path(int id, char *out, size_t cap, const char *suffix) {
    int n = snprintf(out, cap, "%s/case_%05d_%s", TMPDIR, id, suffix);
    if (n < 0 || (size_t)n >= cap) out[0] = '\0';
}
static int write_file(const char *path, const char *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    size_t w;
    int rc;
    if (fp == (FILE *)0) return 0;
    if (len != 0u) {
        w = fwrite(data, 1, len, fp);
        if (w != len) { (void)fclose(fp); return 0; }
    }
    rc = fclose(fp);
    return rc == 0;
}
static size_t read_rest(FILE *fp, char *out, size_t cap) {
    size_t n = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (n + 1u < cap) out[n++] = (char)c;
    }
    if (n >= cap) n = cap - 1u;
    {
        char nul = '\0';
        __builtin_memcpy(out + n, &nul, sizeof(nul));
    }
    return n;
}

/* --- independent line-oriented model -----------------------------------
 * A line is terminated by LF, CR (with an optional following LF consumed),
 * or EOF.  A line whose content is at most size-1 bytes fits.  On overflow
 * the whole physical line is drained.  This mirrors the documented intent
 * of io.hp::read_line / file.hp::read_line, not their implementation. */
struct line_model {
    int rc;
    size_t out_len;
    size_t rem_len;
};
static struct line_model model_line(const char *in, size_t len, size_t size) {
    struct line_model m;
    size_t end = len;
    size_t consumed = len;
    size_t i;
    m.rc = 2;
    m.out_len = 0;
    m.rem_len = 0;
    if (len == 0u) { m.rc = 2; return m; } /* STDLIB_EOF */
    for (i = 0; i < len; i++) {
        if (in[i] == '\n') { end = i; consumed = i + 1u; break; }
        if (in[i] == '\r') {
            end = i;
            consumed = i + 1u;
            if (i + 1u < len && in[i + 1u] == '\n') consumed = i + 2u;
            break;
        }
    }
    if (end <= size - 1u) {
        m.rc = 0; /* STDLIB_OK */
        m.out_len = end;
    } else {
        m.rc = -1; /* STDLIB_FAIL, bounds */
        m.out_len = 0;
    }
    m.rem_len = len - consumed;
    return m;
}

/* Build a hostile line case and its independent expectation. */
static void build_line_case(unsigned int variant, unsigned int k,
                            char *in, size_t *in_len, size_t *size) {
    size_t n = 0;
    size_t i;
    switch (variant) {
    case 0:  for (i = 0; i < k; i++) in[n++] = 'A';
             in[n++] = '\n'; *size = (size_t)k + 1u; break;                /* exact fit */
    case 1:  for (i = 0; i < k; i++) in[n++] = 'A';
             in[n++] = 'A'; in[n++] = '\n'; *size = (size_t)k + 1u; break;  /* one over */
    case 2:  for (i = 0; i < k; i++) in[n++] = 'A';
             in[n++] = '\r'; in[n++] = '\n'; *size = (size_t)k + 1u; break; /* CRLF exact */
    case 3:  for (i = 0; i + 1u < k; i++) in[n++] = 'A';
             in[n++] = '\r'; for (i = 0; i < k; i++) in[n++] = 'B';
             in[n++] = '\n'; *size = (size_t)k + 1u; break;                /* CR inside exact-fit buffer */
    case 4:  for (i = 0; i < k; i++) in[n++] = 'A';
             in[n++] = '\n'; for (i = 0; i < k; i++) in[n++] = 'C';
             in[n++] = '\n'; *size = (size_t)k + 1u; break;                /* exact + next line */
    case 5:  for (i = 0; i < k; i++) in[n++] = 'A';
             in[n++] = 'A'; in[n++] = '\r'; in[n++] = '\n';
             *size = (size_t)k + 1u; break;                                /* overlong CRLF */
    case 6:  n = 1; in[0] = '\n'; *size = (size_t)k + 1u; break;            /* empty LF line */
    case 7:  n = 2; in[0] = '\r'; in[1] = '\n'; *size = (size_t)k + 1u; break;
    case 8:  for (i = 0; i + 1u < k; i++) in[n++] = 'A';
             in[n++] = '\0'; for (i = 0; i < k; i++) in[n++] = 'B';
             in[n++] = '\n'; *size = (size_t)k + 1u; break;                /* embedded NUL inside buffer */
    case 9:  for (i = 0; i < k; i++) in[n++] = 'A';
             *size = (size_t)k + 1u; break;                                 /* EOF fit */
    case 10: for (i = 0; i < k; i++) in[n++] = 'A';
             in[n++] = 'A'; *size = (size_t)k + 1u; break;                 /* EOF overlong */
    case 11: for (i = 0; i < k; i++) in[n++] = 'A';
             in[n++] = '\n'; *size = 1u; break;                             /* size==1 overlong */
    case 12: n = 1; in[0] = '\n'; *size = 1u; break;                        /* size==1 empty LF */
    case 13: n = 1; in[0] = '\r'; *size = 1u; break;                        /* size==1 empty CR */
    default: for (i = 0; i < k; i++) in[n++] = 'A';
             in[n++] = '\n'; *size = (size_t)k + 2u; break;                /* short fit */
    }
    *in_len = n;
}

/* Compare observed read_line result with the independent model. */
static void judge_line(struct case_result *r, int sub, int rc, const char *buf,
                       const char *rem, size_t rem_len, struct line_model m,
                       const char *in, size_t in_len, int clean) {
    if (!clean) { setres(r, CLS_CORRUPT, sub, rc); return; }
    if (m.rc == -1 && rc == 0) { setres(r, CLS_UNEXPECTED_OK, sub, rc); return; }
    if (m.rc == 0 && rc != 0) { setres(r, CLS_CORRUPT, sub, rc); return; }
    if (m.rc == -1 && rc != -1) { setres(r, CLS_CORRUPT, sub, rc); return; }
    if (rc == 0) {
        size_t i;
        for (i = 0; i < m.out_len; i++)
            if (buf[i] != in[i]) { setres(r, CLS_CORRUPT, sub, rc); return; }
    }
    if (rem_len != m.rem_len) { setres(r, CLS_CORRUPT, sub + 64, rc); return; }
    {
        size_t i;
        for (i = 0; i < m.rem_len; i++)
            if (rem[i] != in[in_len - m.rem_len + i]) {
                setres(r, CLS_CORRUPT, sub + 64, rc); return;
            }
    }
    setres(r, CLS_REFUSED, sub, rc);
}

/* ============================ attack families ========================== */

/* 0: io.hp::read_line via stdin (freopen in the forked child). */
static void attack_io_line(int id, unsigned int variant, unsigned int k,
                           struct case_result *r) {
    char in[64];
    size_t in_len = 0, size = 0, got_rem;
    struct line_model m;
    FILE *fp;
    error e = error();
    char *buf = g_buf + GUARD;
    int rc;
    case_path(id, g_path, sizeof(g_path), "io");
    build_line_case(variant, k, in, &in_len, &size);
    if (!write_file(g_path, in, in_len)) { setres(r, CLS_REFUSED, 90, 0); return; }
    fill_a5(g_buf, sizeof(g_buf));
    fill_a5(g_rem, sizeof(g_rem));
    m = model_line(in, in_len, size);
    fp = freopen(g_path, "rb", stdin);
    if (fp == (FILE *)0) { setres(r, CLS_REFUSED, 91, 0); return; }
    error_reset(&e);
    rc = read_line(buf, size, &e);
    got_rem = read_rest(stdin, g_rem + GUARD, CAP);
    setres(r, CLS_REFUSED, 0, 0);
    judge_line(r, (int)variant, rc, buf, g_rem + GUARD, got_rem, m, in, in_len,
               buf_clean(size) && rem_clean());
    (void)fclose(stdin);
    (void)unlink(g_path);
}

/* 1: file.hp::read_line via a real temp file. */
static void attack_file_line(int id, unsigned int variant, unsigned int k,
                             struct case_result *r) {
    char in[64];
    size_t in_len = 0, size = 0, got_rem;
    struct line_model m;
    error e = error();
    file f;
    char *buf = g_buf + GUARD;
    int rc;
    case_path(id, g_path, sizeof(g_path), "fl");
    build_line_case(variant, k, in, &in_len, &size);
    if (!write_file(g_path, in, in_len)) { setres(r, CLS_REFUSED, 90, 0); return; }
    fill_a5(g_buf, sizeof(g_buf));
    fill_a5(g_rem, sizeof(g_rem));
    m = model_line(in, in_len, size);
    f = file(g_path, "rb");
    error_reset(&e);
    rc = f.read_line(buf, size, &e);
    got_rem = read_rest(f.native(), g_rem + GUARD, CAP);
    setres(r, CLS_REFUSED, 0, 0);
    judge_line(r, (int)variant, rc, buf, g_rem + GUARD, got_rem, m, in, in_len,
               buf_clean(size) && rem_clean());
    f.drop();
    (void)unlink(g_path);
}

/* 2: file.hp read/write/seek/close/borrow/temp robustness. */
static void attack_file_ops(int id, unsigned int variant, struct case_result *r) {
    error e = error();
    file f;
    int rc, k;
    size_t n;
    case_path(id, g_path, sizeof(g_path), "fo");
    switch (variant) {
    case 0: /* exact-fit read with canaries */
        if (!write_file(g_path, "0123456789", 10u)) { setres(r, CLS_REFUSED, 90, 0); return; }
        f = file(g_path, "rb");
        fill_a5(g_buf, sizeof(g_buf));
        error_reset(&e);
        rc = f.read(g_buf + GUARD, 10u, &e);
        if (rc != 0 || !buf_clean(10u) || memcmp(g_buf + GUARD, "0123456789", 10u) != 0)
            setres(r, CLS_CORRUPT, 0, rc);
        else setres(r, CLS_REFUSED, 0, rc);
        f.drop(); (void)unlink(g_path); return;
    case 1: /* one-over read request: must not write past requested size */
        if (!write_file(g_path, "0123456789", 10u)) { setres(r, CLS_REFUSED, 90, 0); return; }
        f = file(g_path, "rb");
        fill_a5(g_buf, sizeof(g_buf));
        error_reset(&e);
        rc = f.read(g_buf + GUARD, 4u, &e);
        if (!buf_clean(4u)) setres(r, CLS_CORRUPT, 1, rc);
        else setres(r, CLS_REFUSED, 1, rc);
        f.drop(); (void)unlink(g_path); return;
    case 2: /* null buffer / zero length must refuse */
        f = file(g_path, "wb");
        error_reset(&e);
        rc = f.read((char *)0, 8u, &e);
        if (rc != -1) { setres(r, CLS_UNEXPECTED_OK, 2, rc); f.drop(); return; }
        rc = f.read(g_buf + GUARD, 0u, &e);
        if (rc != -1) { setres(r, CLS_UNEXPECTED_OK, 2, rc); f.drop(); return; }
        setres(r, CLS_REFUSED, 2, rc); f.drop(); return;
    case 3: /* read from a zero-initialised / failed-open handle */
        f = file((const char *)0, (const char *)0);
        fill_a5(g_buf, sizeof(g_buf));
        error_reset(&e);
        rc = f.read(g_buf + GUARD, 8u, &e);
        if (rc != -1 || !buf_clean(8u)) setres(r, CLS_CORRUPT, 3, rc);
        else setres(r, CLS_REFUSED, 3, rc);
        f.drop(); return;
    case 4: /* double close: second must fail, never crash/fclose twice */
        if (!write_file(g_path, "x", 1u)) { setres(r, CLS_REFUSED, 90, 0); return; }
        f = file(g_path, "rb");
        error_reset(&e);
        rc = f.close(&e);
        if (rc != 0) { setres(r, CLS_CORRUPT, 4, rc); return; }
        rc = f.close(&e);
        if (rc != -1) setres(r, CLS_UNEXPECTED_OK, 4, rc);
        else if (f.read(g_buf + GUARD, 8u, &e) != -1) setres(r, CLS_UNEXPECTED_OK, 5, rc);
        else setres(r, CLS_REFUSED, 4, rc);
        (void)unlink(g_path); return;
    case 5: /* drop then close must be a defined refusal */
        f = file_temp(&e);
        if (!f.is_open()) { setres(r, CLS_REFUSED, 92, 0); return; }
        f.drop();
        rc = f.close(&e);
        setres(r, rc == -1 ? CLS_REFUSED : CLS_UNEXPECTED_OK, 6, rc); return;
    case 6: /* borrowed handle: close() must not fclose the underlying stream */
        if (!write_file(g_path, "Z", 1u)) { setres(r, CLS_REFUSED, 90, 0); return; }
        {
            FILE *raw = fopen(g_path, "rb");
            if (raw == (FILE *)0) { setres(r, CLS_REFUSED, 93, 0); return; }
            f = file_borrow(raw);
            error_reset(&e);
            rc = f.close(&e); /* owns == 0: must be a no-op on raw */
            k = fgetc(raw);   /* still usable => borrowed handle was not closed */
            if (rc != 0 || k != (int)'Z') setres(r, CLS_CORRUPT, 7, rc);
            else setres(r, CLS_REFUSED, 7, rc);
            (void)fclose(raw);
        }
        (void)unlink(g_path); return;
    case 7: /* write_bytes one-over source: must copy exactly size bytes */
        f = file(g_path, "wb");
        fill_a5(g_src, sizeof(g_src));
        memcpy(g_src + GUARD, "abcdefghijklmnopqrstuvwxyz012345", 32u);
        error_reset(&e);
        rc = f.write_bytes(g_src + GUARD, 16u, &e);
        if (rc != 0 || !src_clean()) setres(r, CLS_CORRUPT, 8, rc);
        else setres(r, CLS_REFUSED, 8, rc);
        f.drop();
        {
            char back[32];
            FILE *rf = fopen(g_path, "rb");
            size_t got;
            if (rf == (FILE *)0) { setres(r, CLS_REFUSED, 94, 0); return; }
            got = fread(back, 1, sizeof(back), rf);
            (void)fclose(rf);
            if (got != 16u || memcmp(back, g_src + GUARD, 16u) != 0)
                setres(r, CLS_CORRUPT, 9, (int)got);
        }
        (void)unlink(g_path); return;
    case 8: /* null source with non-zero length must refuse; zero length defined */
        f = file(g_path, "wb");
        error_reset(&e);
        rc = f.write_bytes((const char *)0, 16u, &e);
        if (rc != -1) { setres(r, CLS_UNEXPECTED_OK, 10, rc); f.drop(); return; }
        setres(r, CLS_REFUSED, 10, rc); f.drop(); return;
    case 9: /* invalid whence must be refused */
        f = file(g_path, "wb+");
        error_reset(&e);
        rc = f.seek(0L, 99999, &e);
        setres(r, rc == -1 ? CLS_REFUSED : CLS_UNEXPECTED_OK, 11, rc);
        f.drop(); return;
    case 10: /* flush/read on a closed handle must be a defined refusal */
        if (!write_file(g_path, "x", 1u)) { setres(r, CLS_REFUSED, 90, 0); return; }
        f = file(g_path, "rb");
        (void)f.close(&e);
        rc = f.flush(&e);
        if (rc != -1) { setres(r, CLS_UNEXPECTED_OK, 12, rc); return; }
        setres(r, CLS_REFUSED, 12, rc); return;
    default: /* file_temp round trip */
        f = file_temp(&e);
        if (!f.is_open()) { setres(r, CLS_REFUSED, 92, 0); return; }
        error_reset(&e);
        rc = f.write_bytes("temp", 4u, &e);
        if (rc != 0) { setres(r, CLS_CORRUPT, 13, rc); f.drop(); return; }
        f.seek(0L, SEEK_SET, &e);
        fill_a5(g_buf, sizeof(g_buf));
        rc = f.read(g_buf + GUARD, 4u, &e);
        if (rc != 0 || memcmp(g_buf + GUARD, "temp", 4u) != 0 || !buf_clean(4u))
            setres(r, CLS_CORRUPT, 13, rc);
        else setres(r, CLS_REFUSED, 13, rc);
        f.drop(); return;
    }
}

/* 3: fs.hp hostile paths and mkdtemp templates. */
static void attack_fs(int id, unsigned int variant, struct case_result *r) {
    error e = error();
    long long sz;
    int rc, ok;
    case_path(id, g_path, sizeof(g_path), "fs");
    case_path(id, g_path2, sizeof(g_path2), "fz");
    switch (variant) {
    case 0:
        if (!write_file(g_path, "hello", 5u)) { setres(r, CLS_REFUSED, 90, 0); return; }
        ok = fs_exists(g_path) && fs_is_file(g_path) && !fs_is_dir(g_path);
        sz = fs_size(g_path, &e);
        ok = ok && sz == 5 && e.ok();
        setres(r, ok ? CLS_REFUSED : CLS_CORRUPT, 0, (int)sz);
        (void)unlink(g_path); return;
    case 1: /* null + missing paths must never report success */
        errno = 0;
        ok = !fs_exists((const char *)0) && !fs_is_file((const char *)0) &&
             !fs_is_dir((const char *)0) && !fs_exists(g_path2);
        sz = fs_size((const char *)0, &e);
        ok = ok && sz == -1 && e.failed_now();
        sz = fs_size(g_path2, &e);
        ok = ok && sz == -1 && e.failed_now();
        setres(r, ok ? CLS_REFUSED : CLS_UNEXPECTED_OK, 1, (int)sz); return;
    case 2: /* directory classification */
        if (mkdir(g_path, 0700) != 0) { setres(r, CLS_REFUSED, 91, 0); return; }
        ok = fs_exists(g_path) && fs_is_dir(g_path) && !fs_is_file(g_path);
        setres(r, ok ? CLS_REFUSED : CLS_CORRUPT, 2, 0);
        (void)rmdir(g_path); return;
    case 3: /* fs_remove on a file, then on a missing file */
        if (!write_file(g_path, "x", 1u)) { setres(r, CLS_REFUSED, 90, 0); return; }
        rc = fs_remove(g_path, &e);
        if (rc != 0 || fs_exists(g_path)) { setres(r, CLS_CORRUPT, 3, rc); return; }
        rc = fs_remove(g_path, &e);
        setres(r, rc == -1 && e.failed_now() ? CLS_REFUSED : CLS_UNEXPECTED_OK, 3, rc);
        return;
    case 4: /* null paths on mutating APIs */
        rc = fs_remove((const char *)0, &e);
        if (rc != -1) { setres(r, CLS_UNEXPECTED_OK, 4, rc); return; }
        rc = fs_rename((const char *)0, g_path2, &e);
        if (rc != -1) { setres(r, CLS_UNEXPECTED_OK, 4, rc); return; }
        rc = fs_rename(g_path, (const char *)0, &e);
        if (rc != -1) { setres(r, CLS_UNEXPECTED_OK, 4, rc); return; }
        rc = fs_mkdir((const char *)0, &e);
        setres(r, rc == -1 ? CLS_REFUSED : CLS_UNEXPECTED_OK, 4, rc); return;
    case 5: /* rename valid + rename onto an existing target */
        if (!write_file(g_path, "r", 1u) || !write_file(g_path2, "t", 1u)) {
            setres(r, CLS_REFUSED, 90, 0); return;
        }
        rc = fs_rename(g_path, g_path2, &e);
        ok = rc == 0 && !fs_exists(g_path) && fs_exists(g_path2);
        setres(r, ok ? CLS_REFUSED : CLS_CORRUPT, 5, rc);
        (void)unlink(g_path); (void)unlink(g_path2); return;
    case 6: /* mkdtemp valid template */
        fill_a5(g_buf, sizeof(g_buf));
        {
            size_t n = strlen(TMPDIR "/dt_XXXXXX");
            memcpy(g_buf + GUARD, TMPDIR "/dt_XXXXXX", n + 1u);
            rc = fs_mkdtemp(g_buf + GUARD, n + 1u, &e);
            if (rc != 0 || !fs_is_dir(g_buf + GUARD) || !buf_clean(n + 1u))
                setres(r, CLS_CORRUPT, 6, rc);
            else setres(r, CLS_REFUSED, 6, rc);
            if (rc == 0) (void)rmdir(g_buf + GUARD);
        }
        return;
    case 7: /* mkdtemp: size 6 cannot hold "XXXXXX" + NUL: must refuse
             * (strnlen reaches size, so the header's len>=size guard fires). */
        fill_a5(g_buf, sizeof(g_buf));
        memcpy(g_buf + GUARD, "XXXXXX", 6u);
        rc = fs_mkdtemp(g_buf + GUARD, 6u, &e);
        setres(r, rc == -1 ? CLS_REFUSED : CLS_UNEXPECTED_OK, 7, rc); return;
    case 8: /* mkdtemp: wrong suffix must be refused */
        fill_a5(g_buf, sizeof(g_buf));
        memcpy(g_buf + GUARD, "abcdeY", 7u);
        rc = fs_mkdtemp(g_buf + GUARD, 7u, &e);
        setres(r, rc == -1 ? CLS_REFUSED : CLS_UNEXPECTED_OK, 8, rc); return;
    case 9: /* mkdtemp: embedded NUL before XXXXXX must be refused */
        fill_a5(g_buf, sizeof(g_buf));
        memcpy(g_buf + GUARD, "/tmp/a\0XXXXXX", 12u);
        rc = fs_mkdtemp(g_buf + GUARD, 12u, &e);
        setres(r, rc == -1 ? CLS_REFUSED : CLS_UNEXPECTED_OK, 9, rc); return;
    default: /* mkdir valid, then mkdir again must fail */
        rc = fs_mkdir(g_path, &e);
        if (rc != 0) { setres(r, CLS_REFUSED, 95, 0); return; }
        rc = fs_mkdir(g_path, &e);
        setres(r, rc == -1 ? CLS_REFUSED : CLS_UNEXPECTED_OK, 10, rc);
        (void)rmdir(g_path); return;
    }
}

/* 4: json budget / depth-bypass attempts. */
static void attack_json_budget(unsigned int variant, struct case_result *r) {
    error e = error();
    size_t depth = 0, i, len = 0;
    int within, expect, ok;
    const char *s;
    switch (variant) {
    case 0: depth = 1u; break;
    case 1: depth = 4096u; break;
    case 2: depth = 16384u; break;
    case 3: depth = 16385u; break;
    case 4: depth = 20000u; break;
    case 5: depth = 20001u + rnd(1000u); break;
    default: depth = rnd(20000u); break;
    }
    fill_a5(g_text, sizeof(g_text));
    for (i = 0; i < depth; i++) g_text[GUARD + len++] = '[';
    for (i = 0; i < depth; i++) g_text[GUARD + len++] = ']';
    g_text[GUARD + len] = '\0';
    error_reset(&e);
    within = json_input_within_budget(g_text + GUARD, len, 16384u, &e);
    expect = (depth <= 16384u);
    ok = (expect && within == 1 && e.ok()) || (!expect && within == 0 && e.failed_now());
    if (!text_clean()) { setres(r, CLS_CORRUPT, 20, within); return; }
    setres(r, ok ? CLS_REFUSED : CLS_UNEXPECTED_OK, 20 + (int)(variant % 8u), within);
    return;
    (void)s;
}

/* 4b: hostile explicit lengths for json_input_within_budget (SIZE_MAX family). */
static void attack_json_lengths(unsigned int variant, struct case_result *r) {
    error e = error();
    int ok;
    switch (variant % 6u) {
    case 0: ok = json_input_within_budget("1", SIZE_MAX, 16384u, &e) == 0 &&
                 e.failed_now(); break;
    case 1: ok = json_input_within_budget("1", SIZE_MAX - 8u, 16384u, &e) == 0 &&
                 e.failed_now(); break;
    case 2: ok = json_input_within_budget("1", SIZE_MAX - 4u, 16384u, &e) == 0 &&
                 e.failed_now(); break;
    case 3: ok = json_input_within_budget("1", 0u, 16384u, &e) == 0 &&
                 e.failed_now(); break;
    case 4: ok = json_input_within_budget((const char *)0, 16u, 16384u, &e) == 0 &&
                 e.failed_now(); break;
    default: ok = json_input_within_budget("1",
                    (size_t)64u * 1024u * 1024u + 1u, 16384u, &e) == 0 &&
                 e.failed_now(); break;
    }
    setres(r, ok ? CLS_REFUSED : CLS_UNEXPECTED_OK, 70, 0);
}

/* 5: explicit-length JSON parse: escape trickery, malformed UTF-8, truncation. */
static void attack_json_parse(unsigned int variant, struct case_result *r) {
    error e = error();
    size_t len = 0, i, depth;
    int valid, expect_refuse = 1;
    static const char *malformed[] = {
        "{\"a\":1,}", "[1,2,]", "1 2", "01", "+1", ".5", "1.", "NaN",
        "Infinity", "{a:1}", "{'a':1}", "0x10", "{\"a\":1} junk", "[1,,2]",
        "[\"\\uD800\"]", "[\"unterminated]", "{\"a\" 1}", "[true false]"
    };
    static const char *good[] = {
        "{\"a\":1}", "[1,2,3]", "true", "null", "-12.5e+3", "\"hi\"",
        "{\"a\":[true,false,null],\"b\":\"x\"}", "[]", "{}", "0"
    };
    fill_a5(g_text, sizeof(g_text));
    if (variant < 40u) {
        const char *p = malformed[variant % (sizeof(malformed)/sizeof(malformed[0]))];
        len = strlen(p); memcpy(g_text + GUARD, p, len + 1u);
        expect_refuse = 1;
    } else if (variant < 60u) {
        const char *p = good[variant % (sizeof(good)/sizeof(good[0]))];
        len = strlen(p); memcpy(g_text + GUARD, p, len + 1u);
        expect_refuse = 0;
    } else if (variant < 80u) {
        /* invalid UTF-8 sequences inside a string */
        const unsigned char seqs[][8] = {
            {0x5b,0x22,0xc3,0x28,0x22,0x5d},
            {0x5b,0x22,0xe0,0x80,0x80,0x22},
            {0x5b,0x22,0x80,0x22,0x5d,0x00},
            {0x5b,0x22,0xf0,0x80,0x80,0x80,0x22,0x5d}
        };
        const unsigned char *q = seqs[variant % 4u];
        size_t qlen = (variant % 4u == 3u) ? 8u : ((variant % 4u == 2u) ? 5u : 6u);
        memcpy(g_text + GUARD, q, qlen);
        len = qlen;
        expect_refuse = 1;
    } else if (variant < 100u) {
        /* embedded NUL and truncated structured documents */
        const char *p = (variant & 1u) ? "{\"a\":[1,2,3]}\0tail" : "{\"a\":[1,2,3";
        len = (variant & 1u) ? 16u : strlen(p);
        memcpy(g_text + GUARD, p, len + 1u);
        expect_refuse = 1;
    } else if (variant < 120u) {
        /* brackets hidden in strings must not trip the budget; escaped quotes
         * must not terminate a string early. */
        static const char *esc[] = {
            "[\"[[[[[[]\"]", "[\"b\\\\\\\"[[[[\"]", "\"[[[[[[[[[\"",
            "{\"k\":\"[ { ] }\"}", "[\"\\\\\\\"[[[[\"]"
        };
        const char *p = esc[variant % 5u];
        len = strlen(p); memcpy(g_text + GUARD, p, len + 1u);
        expect_refuse = 0;
    } else {
        /* over-depth through the explicit-length entry point */
        depth = 17000u + rnd(2000u);
        for (i = 0; i < depth; i++) g_text[GUARD + len++] = '[';
        for (i = 0; i < depth; i++) g_text[GUARD + len++] = ']';
        expect_refuse = 1;
    }
    error_reset(&e);
    {
        json j = json_parse_budget(g_text + GUARD, len, 16384u, &e);
        valid = j.valid();
        if (valid) {
            json_view rv = j.root();
            (void)rv.size();
            j.drop();
        }
    }
    if (!text_clean()) { setres(r, CLS_CORRUPT, 40, valid); return; }
    if (expect_refuse && valid) setres(r, CLS_UNEXPECTED_OK, 41, valid);
    else if (!expect_refuse && !valid) setres(r, CLS_CORRUPT, 42, valid);
    else setres(r, CLS_REFUSED, 43, valid);
}

/* 6: json_parse_file: deep / truncated / oversized / embedded-NUL files. */
static void attack_json_file(int id, unsigned int variant, struct case_result *r) {
    error e = error();
    size_t i, len = 0;
    int valid, expect_refuse = 1, fd;
    off_t tr;
    case_path(id, g_path, sizeof(g_path), "jf");
    fill_a5(g_text, sizeof(g_text));
    switch (variant) {
    case 0: /* valid small file */
        {
            const char *okdoc = "{\"a\":1,\"b\":[1,2,3]}";
            len = strlen(okdoc); memcpy(g_text + GUARD, okdoc, len + 1u);
        }
        expect_refuse = 0; break;
    case 1: /* truncated structured file */
        memcpy(g_text + GUARD, "{\"a\":[1,2,3", 12u); len = 12u; break;
    case 2: /* empty file */
        len = 0u; break;
    case 3: /* embedded NUL */
        memcpy(g_text + GUARD, "{\"a\":1}\0x", 10u); len = 10u; break;
    case 4: /* trailing garbage */
        memcpy(g_text + GUARD, "[1,2] garbage", 13u); len = 13u; break;
    case 5: /* deep 16000: within default budget */
        for (i = 0; i < 16000u; i++) g_text[GUARD + len++] = '[';
        for (i = 0; i < 16000u; i++) g_text[GUARD + len++] = ']';
        expect_refuse = 0; break;
    case 6: /* deep 20000: must be refused on the file path too */
        for (i = 0; i < 20000u; i++) g_text[GUARD + len++] = '[';
        for (i = 0; i < 20000u; i++) g_text[GUARD + len++] = ']';
        break;
    default: /* deep 17000..19000 */
        {
            size_t d = 17000u + rnd(2000u);
            for (i = 0; i < d; i++) g_text[GUARD + len++] = '[';
            for (i = 0; i < d; i++) g_text[GUARD + len++] = ']';
        }
        break;
    }
    if (!write_file(g_path, g_text + GUARD, len)) { setres(r, CLS_REFUSED, 90, 0); return; }
    if (!text_clean()) { setres(r, CLS_CORRUPT, 50, 0); return; }
    error_reset(&e);
    {
        json j = json_parse_file(g_path, &e);
        valid = j.valid();
        if (valid) j.drop();
    }
    if (expect_refuse && valid) setres(r, CLS_UNEXPECTED_OK, 51, valid);
    else if (!expect_refuse && !valid) setres(r, CLS_CORRUPT, 52, valid);
    else setres(r, CLS_REFUSED, 53, valid);
    /* Oversized sparse file: must be refused from stat, before allocation. */
    if (variant == 6u) {
        fd = open(g_path, O_WRONLY | O_TRUNC, 0600);
        if (fd >= 0) {
            tr = ftruncate(fd, (off_t)((size_t)64u * 1024u * 1024u) + 1u);
            (void)tr;
            (void)close(fd);
            error_reset(&e);
            {
                json big = json_parse_file(g_path, &e);
                if (big.valid()) { big.drop(); setres(r, CLS_UNEXPECTED_OK, 54, 1); }
            }
        }
    }
    /* Directory path must not be parsed as JSON. */
    if (variant == 4u) {
        error_reset(&e);
        {
            json dj = json_parse_file(TMPDIR, &e);
            if (dj.valid()) { dj.drop(); setres(r, CLS_UNEXPECTED_OK, 55, 1); }
        }
    }
    (void)unlink(g_path);
}

/* 7: json_view / json_builder misuse -- must stay in defined territory. */
static void attack_json_view(unsigned int variant, struct case_result *r) {
    error e = error();
    const char *doc = "{\"arr\":[1,2,3],\"n\":1.5,\"b\":true,\"s\":\"hi\"}";
    size_t len = strlen(doc), olen = 0;
    int rc, val = 0, ok = 1;
    double dv;
    const char *strv;
    char *out;
    fill_a5(g_text, sizeof(g_text));
    memcpy(g_text + GUARD, doc, len + 1u);
    error_reset(&e);
    {
        json j = json_parse_budget(g_text + GUARD, len, 16384u, &e);
        json_view root = j.root();
        json_view arr = root.member("arr");
        if (!j.valid()) ok = 0;
        switch (variant % 8u) {
        case 0: { json_view v = arr.at(SIZE_MAX); if (v.valid()) ok = 0; break; }
        case 1: { json_view v = arr.at(999999u); if (v.valid()) ok = 0; break; }
        case 2: { json_view v = root.member((const char *)0); if (v.valid()) ok = 0; break; }
        case 3: { json_view v = root.member("missing"); if (v.valid()) ok = 0; break; }
        case 4: if (root.get_int(&val, &e) == STDLIB_OK) ok = 0; break;
        case 5: { json_view v = root.member("n"); if (v.get_int(&val, &e) == STDLIB_OK) ok = 0; break; }
        case 6: {
            json_view nv = root.member("n");
            json_view bv = root.member("b");
            json_view sv = root.member("s");
            if (nv.get_double(&dv, &e) != STDLIB_OK) ok = 0;
            if (bv.get_bool(&val, &e) != STDLIB_OK || val != 1) ok = 0;
            if (sv.get_string(&strv, &e) != STDLIB_OK) ok = 0;
            break;
        }
        default: break;
        }
        out = root.write(&olen);
        if (out != (char *)0) json_free(out);
        j.drop();
        j.drop(); /* documented idempotent drop */
    }
    {
        json_builder jb = json_builder();
        if (!jb.valid()) ok = 0;
        error_reset(&e);
        if (jb.add_int("i", 42, &e) != STDLIB_OK) ok = 0;
        if (jb.add_string("k", "v", &e) != STDLIB_OK) ok = 0;
        if (jb.add_int((const char *)0, 1, &e) != STDLIB_FAIL) ok = 0;
        if (jb.add_string("z", (const char *)0, &e) != STDLIB_FAIL) ok = 0;
        if (jb.add_double("nan", (double)0.0 / (double)0.0, &e) != STDLIB_FAIL) ok = 0;
        if (jb.add_double("inf", (double)1.0 / (double)0.0, &e) != STDLIB_FAIL) ok = 0;
        out = jb.write(&olen);
        if (out != (char *)0) json_free(out);
        jb.drop();
        jb.drop();
    }
    rc = ok && text_clean() ? CLS_REFUSED : CLS_CORRUPT;
    setres(r, rc, 60, ok);
}

/* --- dispatch ----------------------------------------------------------- */
static void dispatch(int id, struct case_result *r) {
    unsigned int variant, k;
    g_rng = SEED ^ ((unsigned long long)(id + 1) * GOLDEN);
    variant = rnd(1000u);
    k = 2u + rnd(7u); /* 2..8 */
    if (id < 720) attack_io_line(id, variant % 15u, k, r);
    else if (id < 1440) attack_file_line(id, variant % 15u, k, r);
    else if (id < 1800) attack_file_ops(id, variant % 12u, r);
    else if (id < 2100) attack_fs(id, variant % 11u, r);
    else if (id < 2400) attack_json_budget(variant % 8u, r);
    else if (id < 2500) attack_json_lengths(variant % 6u, r);
    else if (id < 2900) attack_json_parse(variant % 140u, r);
    else if (id < 3300) attack_json_file(id, variant % 8u, r);
    else attack_json_view(variant % 8u, r);
}

/* --- parent / classifier ------------------------------------------------ */
static const char *class_name(int cls) {
    if (cls == CLS_UNEXPECTED_OK) return "UNEXPECTED_OK";
    if (cls == CLS_CRASH) return "CRASH";
    if (cls == CLS_CORRUPT) return "CORRUPT";
    return "REFUSED";
}
static int classify_child(int waited_ok, int status, ssize_t got,
                          struct case_result *res) {
    int cls;
    if (!waited_ok || WIFSIGNALED(status) || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || got != (ssize_t)sizeof(*res))
        return CLS_CRASH;
    cls = res->cls;
    if (cls < CLS_REFUSED || cls > CLS_CORRUPT) return CLS_CRASH;
    return cls;
}
static int selfcheck(void) {
    struct case_result r = {0, 0, 0};
    int status = 0, passed = 0;
    r.cls = CLS_UNEXPECTED_OK;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_UNEXPECTED_OK) passed++;
    r.cls = CLS_REFUSED;
    if (classify_child(1, (7 << 8), (ssize_t)sizeof(r), &r) == CLS_CRASH) passed++;
    if (classify_child(1, 11, (ssize_t)sizeof(r), &r) == CLS_CRASH) passed++;
    r.cls = CLS_CORRUPT;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CORRUPT) passed++;
    r.cls = 99;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CRASH) passed++;
    printf("SELFCHECK %d 5\n", passed);
    return passed == 5 ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *sc = getenv("REDTEAM_SELFCHECK");
    const char *logpath = (argc > 1) ? argv[1] : LOGPATH;
    int logfd, line_len, i, counts[4], fds[2], status = 0;
    pid_t pid;
    ssize_t got;
    struct case_result result;
    int cls;
    if (sc != (const char *)0 && strcmp(sc, "1") == 0) return selfcheck();
    for (i = 0; i < 4; i++) counts[i] = 0;
    (void)mkdir(TMPDIR, 0700);
    logfd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) {
        fprintf(stderr, "RED20 io: cannot open %s\n", logpath);
        printf("RED20 io 0 0 0 0 0\n");
        return 2;
    }
    line_len = snprintf(g_line, sizeof(g_line),
                        "# RED20 io seed=%016llx cases=%d\n", SEED, N_CASES);
    if (line_len > 0) (void)write(logfd, g_line, (size_t)line_len);
    for (i = 0; i < N_CASES; i++) {
        if (pipe(fds) != 0) { fprintf(stderr, "pipe failed at %d\n", i); break; }
        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed at %d\n", i);
            (void)close(fds[0]); (void)close(fds[1]); break;
        }
        if (pid == 0) {
            struct case_result child_result;
            (void)close(fds[0]);
            (void)alarm(ALARM_SEC);
            dispatch(i, &child_result);
            (void)write(fds[1], &child_result, sizeof(child_result));
            (void)close(fds[1]);
            _exit(0);
        }
        (void)close(fds[1]);
        got = read(fds[0], &result, sizeof(result));
        (void)close(fds[0]);
        cls = classify_child(waitpid(pid, &status, 0) == pid, status, got, &result);
        counts[cls]++;
        line_len = snprintf(g_line, sizeof(g_line),
                            "%05d %s sub=%d got=%d cls=%d\n", i, class_name(cls),
                            cls == CLS_CRASH ? -1 : result.sub,
                            cls == CLS_CRASH ? -1 : result.got, cls);
        if (line_len > 0) (void)write(logfd, g_line, (size_t)line_len);
    }
    line_len = snprintf(g_line, sizeof(g_line),
                        "# SUMMARY cases=%d crash=%d corrupt=%d unexpected_ok=%d refused=%d\n",
                        N_CASES, counts[CLS_CRASH], counts[CLS_CORRUPT],
                        counts[CLS_UNEXPECTED_OK], counts[CLS_REFUSED]);
    if (line_len > 0) (void)write(logfd, g_line, (size_t)line_len);
    (void)close(logfd);
    printf("RED20 io %d %d %d %d %d\n", N_CASES, counts[CLS_CRASH],
           counts[CLS_CORRUPT], counts[CLS_UNEXPECTED_OK], counts[CLS_REFUSED]);
    return 0;
}
