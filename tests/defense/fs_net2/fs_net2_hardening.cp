/* Blue-team Agent H: filesystem / network / http defense regressions.
 *
 * Offline only.  HTTP uses file:// URLs (no sockets, no external service).
 * net.hp uses AF_UNIX socketpairs (local IPC, not network I/O).
 * Every case runs in a forked child with an alarm watchdog; the caller-owned
 * response/path/buffer is surrounded by 0xA5 canary guards.
 *
 * The primary regression is F-H1: http_client::request must report
 * STDLIB_CATEGORY_BOUNDS with the documented "response larger than the caller
 * buffer" message when the response does not fit.  libcurl aborts an
 * over-capacity write callback with CURLE_WRITE_ERROR, so the old ordering
 * reported a generic IO error and the BOUNDS branch was unreachable.
 *
 * Build:  ./build/c+ build tests/defense/fs_net2/fs_net2_hardening.cp \
 *           --default --sanitize=none -lcurl -I. -o <bin>
 * Run:    timeout 60 <bin> [log-path]
 * Prints: FS_NET2 <cases> <pass> <fail> <crashes> <canary_fail>
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"
#include "stdlib/http.hp"
#include "stdlib/net.hp"

#define N_HTTP 1080
#define N_FS 60
#define N_NET 60
#define N_CASES (N_HTTP + N_FS + N_NET)

#define GUARD 16
#define MAXBUF 1080
#define PAYLOAD 256
#define GUARD_BYTE 0xA5
#define ALARM_SEC 5

static const char *PAYLOAD_PATH = "/tmp/cplus_fsnet2_payload.bin";
static const char *FSPATH = "/tmp/cplus_fsnet2_fsfile";
static const char *FSDIR = "/tmp/cplus_fsnet2_fsdir";
static char g_guard[GUARD + MAXBUF + 64 + GUARD];

static void fill_guard(void) { memset(g_guard, GUARD_BYTE, sizeof(g_guard)); }

/* Verify the guards before buf and all untouched bytes after `upto`. */
static int guard_ok(int upto) {
    int i;
    if (upto > MAXBUF) upto = MAXBUF;
    for (i = 0; i < GUARD; i++)
        if ((unsigned char)g_guard[i] != GUARD_BYTE) return 0;
    for (i = GUARD + upto; i < (int)sizeof(g_guard); i++)
        if ((unsigned char)g_guard[i] != GUARD_BYTE) return 0;
    return 1;
}

static void report(int fd, int id, const char *api, const char *result,
                   int sent, int ok) {
    char line[160];
    int n = snprintf(line, sizeof(line), "S %05d %s %s %d %d\n",
                     id, api, result, sent, ok);
    if (n > 0) {
        ssize_t w = write(fd, line, (size_t)n);
        (void)w;
    }
}

/* ---------------- HTTP: overflow error propagation (primary regression) -- */
static void case_http(int id, int fd, int size) {
    error e = error();
    http_client hc = http_client(&e);
    char url[128];
    char *buf = g_guard + GUARD;
    int rc, ok, sent, expect_ok = (size >= PAYLOAD + 1);
    int upto = expect_ok ? PAYLOAD + 1 : size;
    snprintf(url, sizeof(url), "file://%s", PAYLOAD_PATH);
    fill_guard();
    error_reset(&e);
    rc = hc.get(url, buf, (size_t)size, &e);
    sent = guard_ok(upto);
    if (expect_ok) {
        /* whole payload fits: 256 data bytes + NUL at index 256 */
        ok = rc == STDLIB_OK && e.ok() &&
             buf[0] == 'A' && buf[PAYLOAD - 1] != '\0' &&
             buf[PAYLOAD] == '\0';
    } else {
        /* overflow: buf[0..size-2] holds data, buf[size-1] is NUL */
        ok = rc == STDLIB_FAIL && e.failed_now() &&
             e.category() == STDLIB_CATEGORY_BOUNDS &&
             strcmp(e.what(), "response larger than the caller buffer") == 0 &&
             buf[size - 1] == '\0';
    }
    hc.drop();
    report(fd, id, "http.overflow", expect_ok ? "DEFINED_OK" : "BOUNDS_REFUSED",
           sent, ok);
}

/* ---------------- filesystem: defined edge behaviour ---------------------- */
static void case_fs(int id, int fd, int mode) {
    error e = error();
    char *path = g_guard + GUARD;
    int ok = 1, sent, upto;
    long long sz;
    int rc;
    fill_guard();
    switch (mode % 6) {
    case 0: /* NULL path: every helper must refuse, never dereference */
        ok = fs_exists((const char *)0) == 0 &&
             fs_is_file((const char *)0) == 0 &&
             fs_is_dir((const char *)0) == 0;
        error_reset(&e);
        sz = fs_size((const char *)0, &e);
        ok = ok && sz == -1 && e.category() == STDLIB_CATEGORY_INVALID;
        error_reset(&e);
        rc = fs_remove((const char *)0, &e);
        ok = ok && rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID;
        error_reset(&e);
        rc = fs_mkdir((const char *)0, &e);
        ok = ok && rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID;
        break;
    case 1: /* empty path: queries 0, operations IO failure, template INVALID */
        path[0] = '\0';
        ok = fs_exists(path) == 0 && fs_is_file(path) == 0 && fs_is_dir(path) == 0;
        error_reset(&e);
        sz = fs_size(path, &e);
        ok = ok && sz == -1 && e.category() == STDLIB_CATEGORY_IO;
        error_reset(&e);
        rc = fs_remove(path, &e);
        ok = ok && rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_IO;
        error_reset(&e);
        rc = fs_mkdir(path, &e);
        ok = ok && rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_IO;
        break;
    case 2: /* missing path */
        snprintf(path, MAXBUF, "/tmp/cplus_fsnet2_absent_%d", id);
        ok = fs_exists(path) == 0 && fs_is_file(path) == 0 && fs_is_dir(path) == 0;
        error_reset(&e);
        sz = fs_size(path, &e);
        ok = ok && sz == -1 && e.category() == STDLIB_CATEGORY_IO;
        break;
    case 3: /* regular file: size + classification */
        snprintf(path, MAXBUF, "%s", FSPATH);
        ok = fs_exists(path) == 1 && fs_is_file(path) == 1 && fs_is_dir(path) == 0;
        error_reset(&e);
        sz = fs_size(path, &e);
        ok = ok && sz == 3 && e.ok();
        break;
    case 4: /* directory: classified as dir, unlink-only fs_remove refuses */
        snprintf(path, MAXBUF, "%s", FSDIR);
        ok = fs_exists(path) == 1 && fs_is_dir(path) == 1 && fs_is_file(path) == 0;
        error_reset(&e);
        rc = fs_remove(path, &e);
        ok = ok && rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_IO;
        break;
    default: /* fs_mkdtemp template validation: bad suffix must refuse */
        snprintf(path, MAXBUF, "/tmp/cplus_fsnet2_noXXXX");
        error_reset(&e);
        rc = fs_mkdtemp(path, (size_t)strlen(path) + 1u, &e);
        ok = rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID;
        break;
    }
    upto = (mode % 6 == 0) ? 0 : (int)strlen(path) + 1;
    sent = guard_ok(upto);
    report(fd, id, "fs.edge", "DEFINED", sent, ok);
}

/* ---------------- network: socketpair, borrowed handles, disconnect ------ */
static void case_net(int id, int fd, int mode) {
    error e = error();
    int sp[2];
    int rc, ok = 1, sent;
    char *buf = g_guard + GUARD;
    fill_guard();
    if ((mode % 4) == 0) {
        /* invalid borrowed descriptor: both directions refuse, canary intact */
        connection c = connection_borrow(-1);
        error_reset(&e);
        rc = c.read(buf, 32u, &e);
        ok = rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID;
        error_reset(&e);
        rc = c.write("x", 1u, &e);
        ok = ok && rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID;
        c.drop();
    } else if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        if ((mode % 4) == 1) {
            /* round trip over local socketpair */
            ssize_t w = write(sp[1], "abcd", 4u);
            connection c = connection_borrow(sp[0]);
            error_reset(&e);
            rc = c.read(buf, 16u, &e);
            ok = w == 4 && rc == 4 && e.ok() && memcmp(buf, "abcd", 4u) == 0;
            c.drop();
        } else if ((mode % 4) == 2) {
            /* peer closed: write must return a defined IO failure.  SIGPIPE is
             * caller-managed by documented contract and ignored by this harness. */
            connection c = connection_borrow(sp[0]);
            close(sp[1]);
            error_reset(&e);
            rc = c.write("data", 4u, &e);
            ok = rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_IO;
            c.drop();
        } else {
            /* clean EOF after peer close */
            connection c = connection_borrow(sp[0]);
            close(sp[1]);
            error_reset(&e);
            rc = c.read(buf, 16u, &e);
            ok = rc == STDLIB_EOF && e.ok();
            c.drop();
        }
        close(sp[0]);
    } else {
        ok = 0; /* socketpair failure is an environment problem */
    }
    sent = guard_ok(32);
    report(fd, id, "net.edge", "DEFINED", sent, ok);
}

static int read_line(int fd, char *out, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c = '\0';
        ssize_t k = read(fd, &c, 1);
        if (k <= 0 || c == '\n') break;
        if (n + 1 < cap) out[n++] = c; else break;
    }
    out[n] = '\0';
    return (int)n;
}

static int write_fixture(const char *path, char fill, size_t len) {
    FILE *f;
    size_t wrote;
    char data[PAYLOAD];
    size_t i;
    for (i = 0; i < len; i++) data[i] = (char)(fill + (char)(i % 26u));
    f = fopen(path, "wb");
    if (f == (FILE *)0) return -1;
    wrote = fwrite(data, 1u, len, f);
    if (fclose(f) != 0 || wrote != len) return -1;
    return 0;
}

static int make_fixtures(void) {
    if (write_fixture(PAYLOAD_PATH, 'A', PAYLOAD) != 0) return -1;
    if (write_fixture(FSPATH, 'x', 3u) != 0) return -1;
    if (mkdir(FSDIR, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

int main(int argc, char **argv) {
    const char *logpath = (argc > 1) ? argv[1] : "tests/defense/fs_net2/out/scenarios.log";
    FILE *log;
    int id, pfds[2], status, rc, cases = 0, pass = 0, fail = 0;
    int crashes = 0, canary_fail = 0;
    pid_t pid;
    char line[256];

    signal(SIGPIPE, SIG_IGN); /* documented caller-managed contract */
    log = fopen(logpath, "w");
    if (log == (FILE *)0) { fprintf(stderr, "FS_NET2: cannot open log\n"); return 2; }
    if (make_fixtures() != 0) {
        fprintf(stderr, "FS_NET2: cannot create offline fixtures\n");
        return 2;
    }

    for (id = 0; id < N_CASES; id++) {
        char text[320];
        int v_id, v_sent, v_ok, parsed, n;
        char api[48], result[48];
        if (pipe(pfds) != 0) { crashes++; continue; }
        pid = fork();
        if (pid < 0) { close(pfds[0]); close(pfds[1]); crashes++; continue; }
        if (pid == 0) {
            close(pfds[0]);
            alarm(ALARM_SEC);
            if (id < N_HTTP) case_http(id, pfds[1], (id % MAXBUF) + 1);
            else if (id < N_HTTP + N_FS) case_fs(id, pfds[1], (id - N_HTTP));
            else case_net(id, pfds[1], (id - N_HTTP - N_FS));
            close(pfds[1]);
            _exit(0);
        }
        close(pfds[1]);
        n = read_line(pfds[0], line, sizeof(line));
        close(pfds[0]);
        rc = waitpid(pid, &status, 0);
        cases++;
        if (rc != pid || WIFSIGNALED(status) ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0 || n == 0) {
            crashes++;
            snprintf(text, sizeof(text), "E %05d CRASH rc=%d\n", id,
                     WIFSIGNALED(status) ? WTERMSIG(status) :
                     (WIFEXITED(status) ? WEXITSTATUS(status) : -1));
            fputs(text, log);
            continue;
        }
        parsed = sscanf(line, "S %d %47s %47s %d %d",
                        &v_id, api, result, &v_sent, &v_ok);
        snprintf(text, sizeof(text), "%s\n", line);
        fputs(text, log);
        if (parsed != 5 || v_id != id) { crashes++; continue; }
        if (!v_sent) canary_fail++;
        if (v_ok) pass++; else fail++;
    }
    fclose(log);
    unlink(PAYLOAD_PATH);
    unlink(FSPATH);
    rmdir(FSDIR);
    printf("FS_NET2 %d %d %d %d %d\n", cases, pass, fail, crashes, canary_fail);
    return (fail == 0 && crashes == 0 && canary_fail == 0) ? 0 : 1;
}
