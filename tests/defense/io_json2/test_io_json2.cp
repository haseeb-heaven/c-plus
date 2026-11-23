/* BLUE-TEAM Agent F: deterministic I/O / file / JSON defense harness.
 *
 * 1200 deterministic cases (fixed SplitMix64-style seed). Every case runs in a
 * forked child with alarm(5); the parent classifies signals as CRASH/HANG and
 * inspects 32-byte canaries around each work buffer for CORRUPT.
 *
 * The headline regression is json_parse_file(): the header promises the
 * CPLUS_JSON_MAX_DEPTH / CPLUS_JSON_MAX_INPUT_LENGTH budget is enforced before
 * yyjson allocates, but the file path used to call yyjson_read_file() directly
 * and skipped the budget entirely (a 20000-deep file parsed fine). A correct
 * implementation must REFUSE an over-budget file the same way json_parse_budget
 * refuses an over-budget buffer.
 *
 * Other families guard against regressions in exact-fit/truncated read_line,
 * EOF handling, invalid handles, borrowed/owned cleanup, malformed JSON,
 * invalid escapes, depth boundaries and json_builder cleanup.
 *
 * Build (from repo root):
 *   ./build/c+ build tests/defense/io_json2/test_io_json2.cp \
 *     --default --sanitize=none -I. -Ithird_party/yyjson \
 *     -Lbuild/ext -lyyjson -o <bin>
 * Run: timeout 60 <bin>
 * Output: "IOJSON2 <cases> <crashes> <corrupt> <mismatch>" then exit status.
 */
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"
#include "stdlib/json.hp"

#define N_CASES 1200
#define GUARD 32u
#define JCAP ((size_t)41000u)
#define ALARM_SEC 5
#define CFG_DEPTH ((size_t)16384u)

static char gj[GUARD + JCAP + GUARD];
static char gl[GUARD + 256u + GUARD];
static char gpath[256];
static char gout[256];

static int g_cases, g_crashes, g_corrupt, g_mismatch;

static unsigned long long seed64(unsigned int id) {
    unsigned long long x = 0x5EED5005A5A50005ULL ^
                           ((unsigned long long)(id + 1u) * 0x9E3779B97F4A7C15ULL);
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    return x * 2685821657736338717ULL;
}
static unsigned int rnd(unsigned long long *s) {
    unsigned long long x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return (unsigned int)((x * 2685821657736338717ULL) >> 32);
}
static void fill(char *p, size_t n) { memset(p, 0xA5, n); }
static int all_a5(const char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) if (p[i] != (char)0xA5) return 0;
    return 1;
}
static int guard_ok(void) {
    return all_a5(gj, GUARD) && all_a5(gj + GUARD + JCAP, GUARD) &&
           all_a5(gl, GUARD) && all_a5(gl + GUARD + 256u, GUARD);
}
static void report(int fd, int id, const char *name, int accepted,
                   int sent, int ok) {
    int n = snprintf(gout, sizeof(gout), "S %04d %31s %d %d %d\n",
                     id, name, accepted ? 1 : 0, sent ? 1 : 0, ok ? 1 : 0);
    if (n > 0) {
        size_t k = (size_t)n;
        if (k >= sizeof(gout)) k = sizeof(gout) - 1u;
        (void)write(fd, gout, k);
    }
}
static size_t read_pipe(int fd, char *out, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c = '\0';
        ssize_t k = read(fd, &c, 1);
        if (k <= 0 || c == '\n') break;
        if (n + 1u < cap) out[n++] = c; else break;
    }
    out[n] = '\0';
    return n;
}

/* ------------------------------------------------------------- fixtures -- */

static void make_depth_file(const char *path, size_t depth) {
    FILE *f = fopen(path, "wb");
    size_t i;
    if (f == (FILE *)0) return;
    for (i = 0; i < depth; i++) fputc('[', f);
    for (i = 0; i < depth; i++) fputc(']', f);
    fclose(f);
}
static void make_text_file(const char *path, const char *s) {
    FILE *f = fopen(path, "wb");
    if (f == (FILE *)0) return;
    fputs(s, f);
    fclose(f);
}
/* --------------------------------------------------------------- cases -- */

/* json_parse_file budget: kind selects a freshly-created fixture. */
static int case_parse_file(int id, int fd, unsigned long long *rs) {
    error e = error();
    int kind = (int)(rnd(rs) % 8u);
    int expected_accepted;
    int accepted;
    int ok;
    /* gpath holds the directory; fixtures were created in main() as
     * <dir>/ij2_<kind>.json, so build the full path in a local buffer. */
    {
        char full[256];
        snprintf(full, sizeof(full), "%s/ij2_%d.json", gpath, kind);
        error_reset(&e);
        {
            json j = json_parse_file(full, &e);
            accepted = j.valid();
            if (accepted) j.drop();
        }
    }
    expected_accepted = (kind == 0 || kind == 1 || kind == 2) ? 1 : 0;
    ok = (accepted == expected_accepted &&
          (accepted ? e.ok() : e.failed_now()));
    report(fd, id, "parse_file", accepted, guard_ok(), ok);
    return 0;
}

/* In-memory json_parse_budget with depth sweep / malformed / escapes. */
static int case_parse_budget(int id, int fd, unsigned long long *rs) {
    error e = error();
    int mode = (int)(rnd(rs) % 7u);
    size_t len = 0, i, d;
    int expected_accepted, accepted, ok;
    char *buf = gj + GUARD;

    fill(gj, sizeof(gj));
    if (mode == 0) {
        const char *s = "{\"a\":1,\"b\":[true,false,null],\"c\":\"x\"}";
        len = strlen(s); memcpy(buf, s, len + 1u);
        expected_accepted = 1;
    } else if (mode == 1) {
        const char *s = "{\"a\":[1,2,3";            /* truncated */
        len = strlen(s); memcpy(buf, s, len + 1u);
        expected_accepted = 0;
    } else if (mode == 2) {
        memcpy(buf, "[\"\\q\"]", 7u); len = 7u;     /* invalid escape */
        buf[len] = '\0';
        expected_accepted = 0;
    } else if (mode == 3) {
        d = 16000u;                                  /* within 16384 */
        for (i = 0; i < d; i++) buf[len++] = '[';
        for (i = 0; i < d; i++) buf[len++] = ']';
        buf[len] = '\0';
        expected_accepted = 1;
    } else if (mode == 4) {
        d = 20000u;                                  /* over budget */
        for (i = 0; i < d; i++) buf[len++] = '[';
        for (i = 0; i < d; i++) buf[len++] = ']';
        buf[len] = '\0';
        expected_accepted = 0;
    } else if (mode == 5) {
        const char *s = "[\"[[[[[\",1]";              /* brackets in string */
        len = strlen(s); memcpy(buf, s, len + 1u);
        expected_accepted = 1;
    } else {
        len = 1u + (rnd(rs) % 1024u);                /* random bytes */
        buf[0] = '[';
        for (i = 1; i < len; i++) buf[i] = (char)(1u + (rnd(rs) % 255u));
        buf[len++] = ']';
        buf[len] = '\0';
        expected_accepted = 0;
    }
    error_reset(&e);
    {
        json j = json_parse_budget(buf, len, CFG_DEPTH, &e);
        accepted = j.valid();
        if (accepted) j.drop();
    }
    ok = (accepted == expected_accepted &&
          (accepted ? e.ok() : e.failed_now()));
    report(fd, id, "parse_budget", accepted, guard_ok(), ok);
    return 0;
}

/* json_input_within_budget boundary/overflow cases. */
static int case_budget_query(int id, int fd, unsigned long long *rs) {
    error e = error();
    int mode = (int)(rnd(rs) % 6u);
    int within;
    fill(gj, sizeof(gj));
    error_reset(&e);
    if (mode == 0) {
        within = json_input_within_budget((const char *)0, 16u, CFG_DEPTH, &e);
    } else if (mode == 1) {
        within = json_input_within_budget("1", 0u, CFG_DEPTH, &e);
    } else if (mode == 2) {
        within = json_input_within_budget("1", SIZE_MAX, CFG_DEPTH, &e);
    } else if (mode == 3) {
        char *buf = gj + GUARD;
        size_t i;
        for (i = 0; i < CFG_DEPTH; i++) buf[i] = '[';
        within = json_input_within_budget(buf, CFG_DEPTH, CFG_DEPTH, &e);
    } else if (mode == 4) {
        char *buf = gj + GUARD;
        size_t i;
        for (i = 0; i < CFG_DEPTH + 1u; i++) buf[i] = '[';
        within = json_input_within_budget(buf, CFG_DEPTH + 1u, CFG_DEPTH, &e);
    } else {
        within = json_input_within_budget("[]", 2u, 0u, &e);
    }
    {
        int ok = (mode == 3) ? (within == 1 && e.ok())
                             : (within == 0 && e.failed_now());
        report(fd, id, "budget_query", within, guard_ok(), ok);
    }
    return 0;
}

/* file::read_line / file::read / owned+borrowed cleanup with canaries. */
static int case_file(int id, int fd, unsigned long long *rs) {
    error e = error();
    int kind = (int)(rnd(rs) % 8u);
    char full[256];
    char *buf = gl + GUARD;
    int rc, ok = 1;
    fill(gl, sizeof(gl));
    memset(buf, 'Z', 256u);
    snprintf(full, sizeof(full), "%s/ij2_line_%d.txt", gpath, kind);
    error_reset(&e);
    {
        file f = file(full, "rb");
        if (!f.is_open()) {
            ok = 0;
        } else if (kind == 0) {
            rc = f.read_line(buf, 8u, &e);           /* exact-fit + newline */
            ok = (rc == STDLIB_OK && strcmp(buf, "ABCDEFG") == 0 && e.ok());
        } else if (kind == 1) {
            rc = f.read_line(buf, 8u, &e);           /* overlong: drained */
            ok = (rc == STDLIB_FAIL && e.failed_now() &&
                  e.category() == STDLIB_CATEGORY_BOUNDS);
        } else if (kind == 2) {
            rc = f.read_line(buf, 1u, &e);           /* 1-byte dest */
            ok = (rc == STDLIB_FAIL && e.failed_now());
        } else if (kind == 3) {
            rc = f.read(buf, 4u, &e);                /* partial read */
            ok = (rc == STDLIB_OK && memcmp(buf, "ABCDEFGH", 4) == 0);
        } else if (kind == 4) {
            char one[8];
            FILE *raw = tmpfile();
            if (raw == (FILE *)0) { ok = 0; }
            else {
                fputs("borrow", raw); rewind(raw);
                file b = file_borrow(raw);
                rc = b.read(one, 6u, &e);
                ok = ok && rc == STDLIB_OK && memcmp(one, "borrow", 6) == 0;
                b.drop();                            /* must NOT close raw */
                rc = fgetc(raw) == EOF;              /* raw still usable */
                ok = ok && rc;
                fclose(raw);
            }
        } else if (kind == 5) {
            file t = file_temp(&e);                  /* owned temp */
            ok = ok && t.is_open();
            t.drop();
        } else if (kind == 6) {
            rc = f.seek(0L, SEEK_SET, &e);
            rc = f.read(buf, 3u, &e);
            ok = (rc == STDLIB_OK && memcmp(buf, "ABC", 3) == 0);
        } else {
            rc = f.close(&e);                        /* owned close */
            ok = ok && rc == STDLIB_OK;
            rc = f.close(&e);                        /* double close refused */
            ok = ok && rc == STDLIB_FAIL && e.failed_now();
        }
        f.drop();
    }
    report(fd, id, "file", ok ? 1 : 0, guard_ok(), ok);
    return 0;
}

/* io.hp::read_line from a redirected stdin (EOF / exact-fit / truncation). */
static int case_stdin_line(int id, int fd, unsigned long long *rs) {
    error e = error();
    int kind = (int)(rnd(rs) % 4u);
    char full[256];
    char *buf = gl + GUARD;
    int rc, ok = 1;
    fill(gl, sizeof(gl));
    memset(buf, 'Z', 256u);
    snprintf(full, sizeof(full), "%s/ij2_stdin_%d.txt", gpath, kind);
    if (freopen(full, "rb", stdin) == (FILE *)0) { ok = 0; }
    else {
        error_reset(&e);
        if (kind == 0) {
            rc = read_line(buf, 8u, &e);
            ok = (rc == STDLIB_OK && strcmp(buf, "ABCDEFG") == 0);
        } else if (kind == 1) {
            rc = read_line(buf, 8u, &e);
            ok = (rc == STDLIB_FAIL && e.failed_now());
        } else if (kind == 2) {
            rc = read_line(buf, 1u, &e);
            ok = (rc == STDLIB_FAIL && e.failed_now());
        } else {
            rc = read_line(buf, 8u, &e);             /* empty line at EOF */
            ok = (rc == STDLIB_OK || rc == STDLIB_EOF);
        }
    }
    report(fd, id, "stdin_line", ok ? 1 : 0, guard_ok(), ok);
    return 0;
}

/* fs queries + json_view/json_builder round-trip and cleanup. */
static int case_misc(int id, int fd, unsigned long long *rs) {
    error e = error();
    int ok = 1;
    char full[256];
    (void)rnd(rs);
    fill(gj, sizeof(gj));
    snprintf(full, sizeof(full), "%s/ij2_0.json", gpath);
    error_reset(&e);
    {
        long long sz = fs_size(full, &e);
        ok = ok && sz > 0 && e.ok();
    }
    ok = ok && fs_exists(full) && fs_is_file(full) && !fs_is_dir(full);
    error_reset(&e);
    {
        json_builder jb = json_builder();
        char *out; size_t olen = 0;
        ok = ok && jb.valid();
        ok = ok && jb.add_int("i", 42, &e) == STDLIB_OK;
        ok = ok && jb.add_double("nan", (double)0.0 / (double)0.0, &e) == STDLIB_FAIL;
        ok = ok && jb.add_string("null", (const char *)0, &e) == STDLIB_FAIL;
        out = jb.write(&olen);
        ok = ok && out != (char *)0 && olen > 0;
        json_free(out);
        jb.drop();
    }
    report(fd, id, "misc", ok ? 1 : 0, guard_ok(), ok);
    return 0;
}

static void dispatch(int id, int fd, unsigned long long *rs) {
    fill(gj, sizeof(gj));
    fill(gl, sizeof(gl));
    if (id % 5 == 0) case_parse_file(id, fd, rs);
    else if (id % 5 == 1) case_parse_budget(id, fd, rs);
    else if (id % 5 == 2) case_budget_query(id, fd, rs);
    else if (id % 5 == 3) case_file(id, fd, rs);
    else if (id % 10 == 4) case_stdin_line(id, fd, rs);
    else case_misc(id, fd, rs);
}

static int fork_case(int id, int pfds[2], pid_t *pid) {
    if (pipe(pfds) != 0) return -1;
    *pid = fork();
    if (*pid < 0) { close(pfds[0]); close(pfds[1]); return -1; }
    if (*pid == 0) {
        unsigned long long s;
        close(pfds[0]);
        alarm(ALARM_SEC);
        s = seed64((unsigned int)id);
        dispatch(id, pfds[1], &s);
        close(pfds[1]);
        _exit(0);
    }
    close(pfds[1]);
    return 0;
}

int main(int argc, char **argv) {
    int i, pfds[2], status, mismatch = 0, corrupt = 0, crashes = 0;
    pid_t pid;
    const char *dir = (argc > 1) ? argv[1] : ".";

    snprintf(gpath, sizeof(gpath), "%s", dir);

    /* Fixtures. kind 0/1/2 accepted; 3/4/5/6/7 refused (over depth/bad). */
    {
        char full[256];
        snprintf(full, sizeof(full), "%s/ij2_0.json", gpath);
        make_text_file(full, "{\"a\":1,\"b\":[true,false,null],\"c\":\"x\"}");
        snprintf(full, sizeof(full), "%s/ij2_1.json", gpath);
        make_depth_file(full, 16000u);
        snprintf(full, sizeof(full), "%s/ij2_2.json", gpath);
        make_depth_file(full, CFG_DEPTH);
        snprintf(full, sizeof(full), "%s/ij2_3.json", gpath);
        make_depth_file(full, CFG_DEPTH + 1u);
        snprintf(full, sizeof(full), "%s/ij2_4.json", gpath);
        make_depth_file(full, 20000u);
        snprintf(full, sizeof(full), "%s/ij2_5.json", gpath);
        make_text_file(full, "{\"a\":[1,2,3");
        snprintf(full, sizeof(full), "%s/ij2_6.json", gpath);
        make_text_file(full, "[\"\\q\"]");
        snprintf(full, sizeof(full), "%s/ij2_7.json", gpath);
        make_text_file(full, "");
        /* read_line fixtures */
        snprintf(full, sizeof(full), "%s/ij2_line_0.txt", gpath);
        make_text_file(full, "ABCDEFG\nREST\n");
        snprintf(full, sizeof(full), "%s/ij2_line_1.txt", gpath);
        make_text_file(full, "ABCDEFGHIJKLMNOP\nREST\n");
        snprintf(full, sizeof(full), "%s/ij2_line_2.txt", gpath);
        make_text_file(full, "LONG_LINE\n");
        snprintf(full, sizeof(full), "%s/ij2_line_3.txt", gpath);
        make_text_file(full, "ABCDEFGH\n");
        snprintf(full, sizeof(full), "%s/ij2_line_4.txt", gpath);
        make_text_file(full, "borrow");
        snprintf(full, sizeof(full), "%s/ij2_line_5.txt", gpath);
        make_text_file(full, "temp");
        snprintf(full, sizeof(full), "%s/ij2_line_6.txt", gpath);
        make_text_file(full, "ABCDEF\n");
        snprintf(full, sizeof(full), "%s/ij2_line_7.txt", gpath);
        make_text_file(full, "close");
        /* stdin fixtures */
        snprintf(full, sizeof(full), "%s/ij2_stdin_0.txt", gpath);
        make_text_file(full, "ABCDEFG\n");
        snprintf(full, sizeof(full), "%s/ij2_stdin_1.txt", gpath);
        make_text_file(full, "ABCDEFGHIJKLMNOP\n");
        snprintf(full, sizeof(full), "%s/ij2_stdin_2.txt", gpath);
        make_text_file(full, "LONG\n");
        snprintf(full, sizeof(full), "%s/ij2_stdin_3.txt", gpath);
        make_text_file(full, "");
    }

    for (i = 0; i < N_CASES; i++) {
        char line[256];
        char name[48];
        int n, v_id, v_acc, v_sent, v_ok, rc;
        if (fork_case(i, pfds, &pid) != 0) { crashes++; g_cases++; continue; }
        n = (int)read_pipe(pfds[0], line, sizeof(line));
        close(pfds[0]);
        rc = waitpid(pid, &status, 0);
        g_cases++;
        if (rc != pid || WIFSIGNALED(status) || n == 0) { crashes++; continue; }
        if (sscanf(line, "S %d %31s %d %d %d", &v_id, name, &v_acc, &v_sent,
                   &v_ok) != 5 || v_id != i) { crashes++; continue; }
        if (!v_sent) corrupt++;
        if (!v_ok) mismatch++;
    }
    printf("IOJSON2 %d %d %d %d\n", g_cases, crashes, corrupt, mismatch);
    return (crashes || corrupt || mismatch) ? 1 : 0;
}
