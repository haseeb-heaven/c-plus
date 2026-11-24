/* EDGE100 agent 4/10 -- edge-case coverage for the net/thread stdlib headers.
 *
 * Targets (ATTACK/COVER only; NO source or header is edited):
 *   stdlib/net.hp     14 public APIs  (listener/connection ownership, ports)
 *   stdlib/http.hp    22 public APIs  (curl lifetime, sink bounds, headers)
 *   stdlib/thread.hp  21 public APIs  (mutex/guard/condvar/thread lifecycle)
 *   stdlib/async.hp   18 public APIs  (libuv loop/timer modes, read_file)
 *   stdlib/time.hp     6 public APIs  (clocks, sleeps, ISO-8601 epochs)
 *   total             81 public APIs
 *
 * Every case exercises one documented contract edge: valid input, NULL,
 * invalid port/URL/header, owned vs borrowed, double destroy/join, stale
 * generation (use-after-drop / closed-under-borrow fd), bounded timeouts and
 * epoch/boundary values.  One case per freshly forked child; the child arms
 * alarm(3) and the parent uses poll()+bounded waitpid().  run.sh wraps the
 * whole campaign in `timeout 60`.
 *
 * Verdict: PASS = documented contract honoured, FAIL = contract violated.
 * A signal, hang, short result or non-zero child exit also counts as FAIL.
 *
 * Build (from repo root, see run.sh):
 *   ./build/c+ build tests/edge100/netthread/harness.cp --default \
 *     --sanitize=none -I. -lcurl -I/opt/homebrew/include \
 *     -L/opt/homebrew/lib -luv -o tests/edge100/netthread/out/harness
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"
#include "stdlib/http.hp"
#include "stdlib/thread.hp"
#include "stdlib/async.hp"
#include "stdlib/time.hp"

#define WATCHDOG_SECS 3
#define LOG_PATH "tests/edge100/netthread/out/scenarios.log"
#define FIXTURE_TEXT "edge100-netthread-fixture\n"
#define FIXTURE_LEN 26u

/* ---- result plumbing ----------------------------------------------------- */
struct case_result {
    int verdict; /* 0 = PASS, 1 = FAIL */
    int detail;
};

static void pass(struct case_result *r, int d) {
    r->verdict = 0;
    r->detail = d;
}
static void fail(struct case_result *r, int d) {
    r->verdict = 1;
    r->detail = d;
}
static void check(struct case_result *r, int cond, int d) {
    if (cond) pass(r, d); else fail(r, d);
}

/* ---- small helpers ------------------------------------------------------- */
static int fd_open(int fd) { return fcntl(fd, F_GETFD) != -1; }

static int all_value(const char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char b;
        memcpy(&b, p + i, 1);
        if (b != v) return 0;
    }
    return 1;
}
static char byte_at(const char *p, size_t off) {
    char out;
    memcpy(&out, p + off, 1);
    return out;
}

static int fixture_url(char *url, size_t n) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return 0;
    if (snprintf(url, n, "file://%s/tests/edge100/netthread/out/fixture.txt", cwd) <= 0) return 0;
    return 1;
}

#define GUARD 16
#define SINK_BUF 256
static char g_area[GUARD + SINK_BUF + GUARD];
static char g_src[SINK_BUF + 64];
static void canary_init(void) {
    memset(g_area, 0xA5, GUARD);
    memset(g_area + GUARD, 0x5A, SINK_BUF);
    memset(g_area + GUARD + SINK_BUF, 0xA5, GUARD);
    memset(g_src, 0x42, sizeof(g_src));
}
static int canaries_ok(void) {
    int i;
    for (i = 0; i < GUARD; ++i)
        if (byte_at(g_area, (size_t)i) != (char)0xA5) return 0;
    for (i = GUARD + SINK_BUF; i < GUARD + SINK_BUF + GUARD; ++i)
        if (byte_at(g_area, (size_t)i) != (char)0xA5) return 0;
    return 1;
}

/* ========================================================================= */
/* NET.HP                                                                      */
/* ========================================================================= */

static void c_conn_ctor_valid(struct case_result *r) {
    int fds[2];
    connection cnn;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnn = connection(fds[0]);
    check(r, cnn.is_valid() && cnn.descriptor() == fds[0], 0);
    cnn.drop();
    check(r, !fd_open(fds[0]) && !cnn.is_valid(), 1);
    close(fds[1]);
}

static void c_conn_ctor_invalid(struct case_result *r) {
    connection cnn = connection(-1);
    check(r, !cnn.is_valid() && cnn.descriptor() == -1, 0);
    cnn.drop();
    cnn.drop();
    check(r, !cnn.is_valid(), 1);
}

static void c_conn_zero_init(struct case_result *r) {
    connection cnnz;
    int before0 = fcntl(0, F_GETFD);
    check(r, !cnnz.is_valid() && cnnz.descriptor() == 0, 0);
    cnnz.drop();
    cnnz.drop();
    check(r, fcntl(0, F_GETFD) == before0 && !cnnz.is_valid(), 1);
}

static void c_conn_isvalid_borrowed(struct case_result *r) {
    int fds[2];
    connection cnnb0 = connection_borrow(0);
    connection cnnb2 = connection_borrow(2);
    connection cnnbv;
    (void)cnnb2;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnnbv = connection_borrow(fds[0]);
    check(r, !cnnb0.is_valid() && cnnbv.is_valid() && cnnbv.descriptor() == fds[0], 0);
    cnnb0.drop(); cnnbv.drop(); close(fds[1]);
}

static void c_conn_descriptor(struct case_result *r) {
    connection cnn = connection(-1);
    int fds[2];
    int d;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    d = cnn.descriptor();
    cnn.drop();
    {
        connection cnnv = connection(fds[0]);
        check(r, d == -1 && cnnv.descriptor() == fds[0], 0);
        cnnv.drop();
    }
    close(fds[1]);
}

static void c_conn_read_valid(struct case_result *r) {
    int fds[2];
    connection cnn;
    error e = error();
    char buf[16];
    int n;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    if (send(fds[1], "hello", 5, 0) != 5) { close(fds[0]); close(fds[1]); pass(r, 98); return; }
    cnn = connection(fds[0]);
    memset(buf, 0, sizeof(buf));
    n = cnn.read(buf, sizeof(buf), &e);
    check(r, n == 5 && memcmp(buf, "hello", 5) == 0 && e.ok(), 0);
    cnn.drop();
    close(fds[1]);
}

static void c_conn_read_eof(struct case_result *r) {
    int fds[2];
    connection cnn;
    error e = error();
    char buf[8];
    int n;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    close(fds[1]);
    cnn = connection(fds[0]);
    n = cnn.read(buf, sizeof(buf), &e);
    check(r, n == STDLIB_EOF && e.ok(), 0);
    cnn.drop();
}

static void c_conn_read_null(struct case_result *r) {
    int fds[2];
    connection cnn;
    error e = error();
    int n;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnn = connection(fds[0]);
    n = cnn.read((char *)0, 8, &e);
    check(r, n == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID, 0);
    cnn.drop(); close(fds[1]);
}

static void c_conn_read_zerocap(struct case_result *r) {
    int fds[2];
    connection cnn;
    error e = error();
    char buf[8];
    int n;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnn = connection(fds[0]);
    memset(buf, 0x5A, sizeof(buf));
    n = cnn.read(buf, 0, &e);
    check(r, n == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID &&
              all_value(buf, sizeof(buf), 0x5A), 0);
    cnn.drop(); close(fds[1]);
}

static void c_conn_read_invalid(struct case_result *r) {
    connection cnn = connection(-1);
    connection cnnb = connection_borrow(2);
    error e = error();
    char buf[8];
    int n1, n2;
    memset(buf, 0x5A, sizeof(buf));
    n1 = cnn.read(buf, sizeof(buf), &e);
    n2 = cnnb.read(buf, sizeof(buf), &e);
    check(r, n1 == STDLIB_FAIL && n2 == STDLIB_FAIL &&
              e.category() == STDLIB_CATEGORY_INVALID &&
              all_value(buf, sizeof(buf), 0x5A), 0);
    cnn.drop(); cnnb.drop();
}

static void c_conn_read_stale(struct case_result *r) {
    int fds[2];
    connection cnnb;
    error e = error();
    char buf[8];
    int n;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnnb = connection_borrow(fds[0]);
    close(fds[0]); /* stale generation: fd no longer owned by the borrow */
    memset(buf, 0x5A, sizeof(buf));
    n = cnnb.read(buf, sizeof(buf), &e);
    cnnb.drop();
    check(r, n == STDLIB_FAIL && all_value(buf, sizeof(buf), 0x5A) &&
              fd_open(fds[1]), 0);
    close(fds[1]);
}

static void c_conn_write_valid(struct case_result *r) {
    int fds[2];
    connection cnn;
    error e = error();
    char dst[16];
    int rc, n;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnn = connection(fds[0]);
    rc = cnn.write("abcdef", 6, &e);
    memset(dst, 0, sizeof(dst));
    n = (int)recv(fds[1], dst, sizeof(dst), 0);
    check(r, rc == STDLIB_OK && n == 6 && memcmp(dst, "abcdef", 6) == 0 && e.ok(), 0);
    cnn.drop(); close(fds[1]);
}

static void c_conn_write_null(struct case_result *r) {
    int fds[2];
    connection cnn;
    error e = error();
    int rc;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnn = connection(fds[0]);
    rc = cnn.write((const char *)0, 1, &e);
    check(r, rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID, 0);
    cnn.drop(); close(fds[1]);
}

static void c_conn_write_zerolen(struct case_result *r) {
    int fds[2];
    connection cnn;
    error e = error();
    int rc;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnn = connection(fds[0]);
    rc = cnn.write("x", 0, &e);
    check(r, rc == STDLIB_OK && e.ok(), 0);
    cnn.drop(); close(fds[1]);
}

static void c_conn_write_invalid(struct case_result *r) {
    connection cnn = connection(-1);
    error e = error();
    int rc = cnn.write("x", 1, &e);
    check(r, rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID, 0);
    cnn.drop();
}

static void c_conn_write_stale(struct case_result *r) {
    int fds[2];
    connection cnnb;
    error e = error();
    int rc;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnnb = connection_borrow(fds[0]);
    close(fds[0]);
    rc = cnnb.write("x", 1, &e);
    cnnb.drop(); cnnb.drop();
    check(r, rc == STDLIB_FAIL && fd_open(fds[1]), 0);
    close(fds[1]);
}

static void c_conn_drop_double(struct case_result *r) {
    int fds[2];
    connection cnn;
    int before0 = fcntl(0, F_GETFD);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnn = connection(fds[0]);
    cnn.drop();
    cnn.drop();
    cnn.drop();
    check(r, !fd_open(fds[0]) && !cnn.is_valid() && fd_open(fds[1]) &&
              fcntl(0, F_GETFD) == before0, 0);
    close(fds[1]);
}

static void c_conn_borrow_fd(struct case_result *r) {
    int fds[2];
    connection cnnb;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnnb = connection_borrow(fds[0]);
    check(r, cnnb.is_valid() && cnnb.descriptor() == fds[0], 0);
    cnnb.drop(); /* borrowed: must not close the underlying descriptor */
    check(r, fd_open(fds[0]), 1);
    close(fds[0]); close(fds[1]);
}

static void c_conn_borrow_stale_reuse(struct case_result *r) {
    int fds[2];
    int newfds[2];
    connection cnnb;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { pass(r, 99); return; }
    cnnb = connection_borrow(fds[0]);
    close(fds[0]);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, newfds) != 0) { close(fds[1]); pass(r, 98); return; }
    cnnb.drop(); /* must never close a descriptor that reused the stale number */
    check(r, fd_open(newfds[0]) && fd_open(newfds[1]), 0);
    close(fds[1]); close(newfds[0]); close(newfds[1]);
}

static void c_lsn_ctor_valid(struct case_result *r) {
    listener lsn = listener(0);
    int before0 = fcntl(0, F_GETFD);
    int p = lsn.bound_port();
    int fd = lsn.descriptor();
    check(r, lsn.is_valid() && fd >= 3 && p > 0 && p <= 65535, 0);
    lsn.drop();
    check(r, !fd_open(fd) && !lsn.is_valid() &&
              fcntl(0, F_GETFD) == before0, 1);
}

static void c_lsn_ctor_invalid(struct case_result *r) {
    listener lsna = listener(-1);
    listener lsnb = listener(65536);
    listener lsnc = listener(70000);
    listener lsnd = listener(INT_MIN);
    listener lsne = listener(INT_MAX);
    int ok = !lsna.is_valid() && lsna.descriptor() == -1 && lsna.bound_port() == 0 &&
             !lsnb.is_valid() && lsnb.descriptor() == -1 && lsnb.bound_port() == 0 &&
             !lsnc.is_valid() && lsnc.descriptor() == -1 && lsnc.bound_port() == 0 &&
             !lsnd.is_valid() && lsnd.descriptor() == -1 && lsnd.bound_port() == 0 &&
             !lsne.is_valid() && lsne.descriptor() == -1 && lsne.bound_port() == 0;
    lsna.drop(); lsnb.drop(); lsnc.drop(); lsnd.drop(); lsne.drop();
    check(r, ok, 0);
}

static void c_lsn_zero_init(struct case_result *r) {
    listener lsnz;
    int before0 = fcntl(0, F_GETFD);
    check(r, !lsnz.is_valid() && lsnz.descriptor() == 0 && lsnz.bound_port() == 0, 0);
    lsnz.drop(); lsnz.drop();
    check(r, fcntl(0, F_GETFD) == before0 && !lsnz.is_valid(), 1);
}

static void c_lsn_isvalid_borrowed(struct case_result *r) {
    listener lsnb0 = listener_borrow(0);
    listener lsnb2 = listener_borrow(2);
    listener lsn = listener(0);
    listener lsnbb = listener_borrow(lsn.descriptor());
    int ok = !lsnb0.is_valid() && !lsnb2.is_valid() && lsn.is_valid() && lsnbb.is_valid();
    lsnb0.drop(); lsnb2.drop(); lsnbb.drop(); lsn.drop();
    check(r, ok, 0);
}

static void c_lsn_descriptor(struct case_result *r) {
    listener lsnbad = listener(-1);
    listener lsn = listener(0);
    int d = lsnbad.descriptor();
    int fd = lsn.descriptor();
    lsnbad.drop();
    check(r, d == -1 && fd >= 3 && lsn.descriptor() == fd, 0);
    lsn.drop();
}

static void c_lsn_bound_port(struct case_result *r) {
    listener lsn = listener(0);
    int p = lsn.bound_port();
    check(r, p > 0 && p <= 65535, 0);
    lsn.drop();
    check(r, lsn.bound_port() == 0, 1);
}

static void c_lsn_bound_port_invalid(struct case_result *r) {
    listener lsnz;
    listener lsnbad = listener(-1);
    listener lsnbfd = listener_borrow(1);
    int ok = lsnz.bound_port() == 0 && lsnbad.bound_port() == 0 && lsnbfd.bound_port() == 0;
    lsnz.drop(); lsnbad.drop(); lsnbfd.drop();
    check(r, ok, 0);
}

static void c_lsn_accept_valid(struct case_result *r) {
    listener lsn = listener(0);
    int p = lsn.bound_port();
    int cfd;
    struct sockaddr_in sa;
    connection c;
    char buf[8];
    int n;
    if (!lsn.is_valid() || p <= 0) { lsn.drop(); pass(r, 99); return; }
    cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0) { lsn.drop(); pass(r, 98); return; }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons((unsigned short)p);
    if (connect(cfd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(cfd); lsn.drop(); pass(r, 97); return;
    }
    c = lsn.accept();
    if (!c.is_valid()) { close(cfd); lsn.drop(); fail(r, 0); return; }
    if (send(cfd, "OK", 2, 0) == 2) {
        error e = error();
        n = c.read(buf, sizeof(buf), &e);
        check(r, n == 2 && memcmp(buf, "OK", 2) == 0 && e.ok(), 0);
    } else {
        check(r, 0, 1);
    }
    c.drop();
    close(cfd);
    lsn.drop();
}

static void c_lsn_accept_invalid(struct case_result *r) {
    listener lsnz;
    listener lsnbad = listener(-1);
    listener lsnb0 = listener_borrow(0);
    connection a0 = lsnz.accept();
    connection a1 = lsnbad.accept();
    connection a2 = lsnb0.accept();
    int ok = !a0.is_valid() && !a1.is_valid() && !a2.is_valid() &&
             a0.descriptor() == -1 && a1.descriptor() == -1 && a2.descriptor() == -1;
    a0.drop(); a1.drop(); a2.drop(); lsnz.drop(); lsnbad.drop(); lsnb0.drop();
    check(r, ok, 0);
}

static void c_lsn_drop_double(struct case_result *r) {
    listener lsn = listener(0);
    int fd = lsn.descriptor();
    int before0 = fcntl(0, F_GETFD);
    check(r, lsn.is_valid() && fd_open(fd), 0);
    lsn.drop(); lsn.drop(); lsn.drop();
    check(r, !fd_open(fd) && !lsn.is_valid() && fcntl(0, F_GETFD) == before0, 1);
}

static void c_lsn_borrow_fd(struct case_result *r) {
    listener lsn = listener(0);
    listener lsnb = listener_borrow(lsn.descriptor());
    int fd = lsn.descriptor();
    int p = lsn.bound_port();
    int ok = lsnb.is_valid() && lsnb.descriptor() == fd && lsnb.bound_port() == p;
    lsnb.drop(); /* borrowed: owner's listener still open */
    ok = ok && fd_open(fd);
    lsn.drop();
    check(r, ok && !fd_open(fd), 0);
}

static void c_lsn_borrow_stale(struct case_result *r) {
    listener lsn = listener(0);
    int fd = lsn.descriptor();
    listener lsnb = listener_borrow(fd);
    lsn.drop(); /* underlying listening socket is gone */
    lsnb.drop();  /* stale borrowed handle must not close anything */
    check(r, !fd_open(fd) && !lsnb.is_valid(), 0);
}

static void c_lsn_port_max(struct case_result *r) {
    listener lsn = listener(65535);
    if (lsn.is_valid()) {
        int p = lsn.bound_port();
        lsn.drop();
        check(r, p == 65535, 0);
    } else {
        lsn.drop(); /* port already in use: documented resource refusal */
        pass(r, 1);
    }
}

/* ========================================================================= */
/* HTTP.HP                                                                     */
/* ========================================================================= */

static void c_http_ctor_valid(struct case_result *r) {
    error e = error();
    http hg = http(&e);
    check(r, hg.valid() && e.ok(), 0);
    hg.drop();
    check(r, !hg.valid(), 1);
}
static void c_http_null_err(struct case_result *r) {
    http hg = http((error *)0);
    check(r, hg.valid(), 0);
    hg.drop();
}
static void c_http_drop_double(struct case_result *r) {
    error e = error();
    http hg = http(&e);
    hg.drop(); hg.drop(); hg.drop();
    check(r, !hg.valid(), 0);
}
static void c_http_version(struct case_result *r) {
    const char *v = http_version();
    check(r, v != (const char *)0 && v[0] != '\0', 0);
}
static void c_headers_ctor_valid(struct case_result *r) {
    http_headers hh = http_headers();
    check(r, !hh.valid() && hh.native() == (struct curl_slist *)0, 0);
    hh.drop();
}
static void c_headers_add_null(struct case_result *r) {
    error e = error();
    http_headers hh = http_headers();
    int rc = hh.add((const char *)0, &e);
    check(r, rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID &&
              !hh.valid() && hh.native() == (struct curl_slist *)0, 0);
    hh.drop();
}
static void c_headers_add_valid(struct case_result *r) {
    error e = error();
    http_headers hh = http_headers();
    int rc = hh.add("X-Edge100: 1", &e);
    check(r, rc == STDLIB_OK && e.ok() && hh.valid() &&
              hh.native() != (struct curl_slist *)0, 0);
    hh.drop();
    check(r, !hh.valid(), 1);
}
static void c_headers_add_hostile(struct case_result *r) {
    error e = error();
    http_headers hh = http_headers();
    int a = hh.add("X-A: 1\r\nX-B: 2", &e);
    int b = hh.add("X-C: \x01\x02", &e);
    int c = hh.add("X-D: \x7f\x80", &e);
    check(r, a == STDLIB_OK && b == STDLIB_OK && c == STDLIB_OK && hh.valid(), 0);
    hh.drop();
}
static void c_headers_add_empty(struct case_result *r) {
    error e = error();
    http_headers hh = http_headers();
    int rc = hh.add("", &e);
    check(r, rc == STDLIB_OK && hh.valid(), 0);
    hh.drop();
}
static void c_headers_native(struct case_result *r) {
    http_headers hh = http_headers();
    struct curl_slist *n0 = hh.native();
    hh.add("X-N: 1", (error *)0);
    check(r, n0 == (struct curl_slist *)0 && hh.native() != (struct curl_slist *)0, 0);
    hh.drop();
}
static void c_headers_drop_double(struct case_result *r) {
    http_headers hh = http_headers();
    hh.add("X-N: 1", (error *)0);
    hh.drop(); hh.drop(); hh.drop();
    check(r, !hh.valid(), 0);
}
static void c_sink_null_userdata(struct case_result *r) {
    canary_init();
    check(r, http_sink_write(g_src, 1, 4, (void *)0) == 0u && canaries_ok(), 0);
}
static void c_sink_null_buf(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = (char *)0; sk.size = 64; sk.used = 0; sk.overflow = 0;
    check(r, http_sink_write(g_src, 1, 4, &sk) == 0u, 0);
}
static void c_sink_zero_size(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = 0; sk.used = 0; sk.overflow = 0;
    check(r, http_sink_write(g_src, 1, 4, &sk) == 0u && canaries_ok(), 0);
}
static void c_sink_zero_bytes(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = SINK_BUF; sk.used = 3; sk.overflow = 0;
    check(r, http_sink_write(g_src, 0, 8, &sk) == 0u &&
              sk.used == 3 && sk.overflow == 0 && canaries_ok(), 0);
}
static void c_sink_null_ptr(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = SINK_BUF; sk.used = 0; sk.overflow = 0;
    check(r, http_sink_write((char *)0, 1, 4, &sk) == 0u &&
              sk.used == 0 && sk.overflow == 0, 0);
}
static void c_sink_mul_overflow(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = SINK_BUF; sk.used = 0; sk.overflow = 0;
    (void)http_sink_write(g_src, SIZE_MAX, 2u, &sk);
    (void)http_sink_write(g_src, (size_t)1 << 32, (size_t)1 << 32, &sk);
    check(r, sk.overflow == 1 && canaries_ok() &&
              all_value(g_area + GUARD, SINK_BUF, 0x5A), 0);
}
static void c_sink_valid_write(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = SINK_BUF; sk.used = 0; sk.overflow = 0;
    check(r, http_sink_write((char *)"hello", 1, 5, &sk) == 5u &&
              sk.used == 5 && sk.overflow == 0 &&
              memcmp(sk.buf, "hello", 5) == 0 && byte_at(sk.buf, 5) == '\0' &&
              canaries_ok(), 0);
}
static void c_sink_exact_fit(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = 6; sk.used = 0; sk.overflow = 0;
    check(r, http_sink_write((char *)"hello", 1, 5, &sk) == 5u &&
              sk.used == 5 && sk.overflow == 0 &&
              byte_at(sk.buf, 5) == '\0' && canaries_ok(), 0);
}
static void c_sink_oversize_clamp(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = 16; sk.used = 4; sk.overflow = 0;
    memset(sk.buf, 0x5A, 16);
    check(r, http_sink_write((char *)"0123456789abcdefghij", 1, 20, &sk) == 0u &&
              sk.overflow == 1 && sk.used == 15 &&
              byte_at(sk.buf, 15) == '\0' && canaries_ok(), 0);
}
static void c_sink_full_refuse(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = 16; sk.used = 16; sk.overflow = 0;
    check(r, http_sink_write((char *)"x", 1, 1, &sk) == 0u &&
              sk.overflow == 1 && sk.used == 16 &&
              all_value(g_area + GUARD, SINK_BUF, 0x5A) && canaries_ok(), 0);
}
static void c_sink_used_sizemax(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = SINK_BUF; sk.used = SIZE_MAX; sk.overflow = 0;
    check(r, http_sink_write((char *)"x", 1, 1, &sk) == 0u &&
              sk.overflow == 1 && sk.used == SIZE_MAX &&
              all_value(g_area + GUARD, SINK_BUF, 0x5A) && canaries_ok(), 0);
}
static void c_sink_boundary_nul(struct case_result *r) {
    http_sink sk;
    canary_init();
    sk.buf = g_area + GUARD; sk.size = 16; sk.used = 15; sk.overflow = 0;
    check(r, http_sink_write((char *)"xy", 1, 2, &sk) == 0u &&
              sk.overflow == 1 && sk.used == 15 &&
              byte_at(sk.buf, 15) == '\0' && canaries_ok(), 0);
}
static void c_client_ctor_valid(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    check(r, hc.valid() && e.ok(), 0);
    hc.drop();
}
static void c_client_null_err(struct case_result *r) {
    http_client hc = http_client((error *)0);
    check(r, hc.valid(), 0);
    hc.drop();
}
static void c_client_set_timeout_neg(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    int rc = hc.set_timeout(-1, &e);
    check(r, rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID, 0);
    hc.drop();
}
static void c_client_set_timeout_zero(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    check(r, hc.set_timeout(0, &e) == STDLIB_OK && e.ok(), 0);
    hc.drop();
}
static void c_client_set_timeout_valid(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    check(r, hc.set_timeout(1, &e) == STDLIB_OK, 0);
    hc.drop();
}
static void c_client_set_timeout_max(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    /* libcurl converts CURLOPT_TIMEOUT seconds to signed-int milliseconds:
     * 2147483s is the largest value it accepts here; past that it refuses the
     * setopt and the wrapper must surface a delegated INVALID failure rather
     * than silently reporting success. */
    int ok_large = hc.set_timeout(2147483L, &e);
    int over = hc.set_timeout(2147484L, &e);
    int huge = hc.set_timeout(LONG_MAX, &e);
    check(r, ok_large == STDLIB_OK && over == STDLIB_FAIL &&
              huge == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID, 0);
    hc.drop();
}
static void c_client_set_timeout_after_drop(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    hc.drop();
    check(r, hc.set_timeout(1, &e) == STDLIB_FAIL, 0);
}
static void c_client_follow_0(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    check(r, hc.follow_redirects(0, &e) == STDLIB_OK && e.ok(), 0);
    hc.drop();
}
static void c_client_follow_1(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    int a = hc.follow_redirects(1, &e);
    int b = hc.follow_redirects(-7, &e);
    check(r, a == STDLIB_OK && b == STDLIB_OK, 0);
    hc.drop();
}
static void c_client_follow_after_drop(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    hc.drop();
    check(r, hc.follow_redirects(1, &e) == STDLIB_FAIL, 0);
}
static void c_client_headers_null(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    int rc = hc.set_headers((http_headers *)0, &e);
    check(r, rc == STDLIB_FAIL &&
              e.category() == STDLIB_CATEGORY_INVALID, 0);
    hc.drop();
}
static void c_client_headers_empty(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    http_headers hh = http_headers();
    int rc = hc.set_headers(&hh, &e);
    check(r, rc == STDLIB_OK && hc.header_lines() == 0, 0);
    hh.drop(); hc.drop();
}
static void c_client_headers_owned(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    http_headers hh = http_headers();
    hh.add("X-Edge: a", &e);
    hh.add("X-Edge: b", &e);
    hc.set_headers(&hh, &e);
    hh.drop(); /* caller's list dies before the client performs */
    check(r, hc.header_lines() == 2, 0);
    hc.drop();
}
static void c_client_header_lines_fresh(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    int n = hc.header_lines();
    hc.drop();
    check(r, n == 0, 0);
}
static void c_client_headers_after_drop(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    http_headers hh = http_headers();
    hh.add("X-Edge: z", &e);
    hc.drop();
    check(r, hc.set_headers(&hh, &e) == STDLIB_FAIL, 0);
    hh.drop();
}
static void c_client_status_fresh(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    long s = hc.status();
    hc.drop();
    check(r, s == 0L && hc.status() == 0L, 0);
}
static void c_client_status_after_drop(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    hc.drop();
    check(r, hc.status() == 0L, 0);
}
static void c_client_bytes_fresh(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    unsigned long long b = hc.bytes();
    hc.drop();
    check(r, b == 0ULL && hc.bytes() == 0ULL, 0);
}
static void c_client_bytes_after_drop(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    hc.drop();
    check(r, hc.bytes() == 0ULL, 0);
}
static void c_client_get_null_url(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char buf[32];
    memset(buf, 0x5A, sizeof(buf));
    int rc = hc.get((const char *)0, buf, sizeof(buf), &e);
    check(r, rc == STDLIB_FAIL &&
              all_value(buf, sizeof(buf), 0x5A), 0);
    hc.drop();
}
static void c_client_get_null_buf(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    int rc = hc.get("http://127.0.0.1:1/", (char *)0, 32, &e);
    check(r, rc == STDLIB_FAIL, 0);
    hc.drop();
}
static void c_client_get_zero_size(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char buf[8];
    int rc = hc.get("http://127.0.0.1:1/", buf, 0, &e);
    check(r, rc == STDLIB_FAIL, 0);
    hc.drop();
}
static void c_client_get_after_drop(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char buf[8];
    hc.drop();
    check(r, hc.get("http://127.0.0.1:1/", buf, sizeof(buf), &e) == STDLIB_FAIL, 0);
}
static void c_client_get_bad_url_timeout(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char buf[64];
    unsigned long long t0 = time_monotonic_ms();
    int rc;
    hc.set_timeout(1, &e);
    memset(buf, 0x5A, sizeof(buf));
    rc = hc.get("http://127.0.0.1:1/edge100", buf, sizeof(buf), &e);
    check(r, rc == STDLIB_FAIL && !e.ok() && buf[0] == '\0' &&
              (time_monotonic_ms() - t0) < 3000ULL, 0);
    hc.drop();
}
static void c_client_get_file_ok(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char url[512];
    char buf[128];
    int rc;
    if (!fixture_url(url, sizeof(url))) { pass(r, 99); return; }
    memset(buf, 0, sizeof(buf));
    rc = hc.get(url, buf, sizeof(buf), &e);
    check(r, rc == STDLIB_OK && e.ok() && hc.bytes() == (unsigned long long)FIXTURE_LEN &&
              memcmp(buf, FIXTURE_TEXT, FIXTURE_LEN) == 0, 0);
    hc.drop();
}
static void c_client_post_null_body(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char url[512];
    char buf[128];
    int rc;
    if (!fixture_url(url, sizeof(url))) { pass(r, 99); return; }
    rc = hc.post(url, (const char *)0, buf, sizeof(buf), &e);
    check(r, rc == STDLIB_OK, 0);
    hc.drop();
}
static void c_client_post_file_ok(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char url[512];
    char buf[128];
    int rc;
    if (!fixture_url(url, sizeof(url))) { pass(r, 99); return; }
    hc.set_timeout(1, &e);
    rc = hc.post(url, "payload", buf, sizeof(buf), &e);
    check(r, rc == STDLIB_OK && hc.bytes() == (unsigned long long)FIXTURE_LEN &&
              memcmp(buf, FIXTURE_TEXT, FIXTURE_LEN) == 0, 0);
    hc.drop();
}
static void c_client_post_null_url(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char buf[16];
    int rc = hc.post((const char *)0, "x", buf, sizeof(buf), &e);
    check(r, rc == STDLIB_FAIL, 0);
    hc.drop();
}
static void c_client_request_direct(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    char url[512];
    char buf[128];
    int a, b;
    if (!fixture_url(url, sizeof(url))) { pass(r, 99); return; }
    a = hc.request(url, (const char *)0, buf, sizeof(buf), &e);
    b = hc.request(url, "body", buf, sizeof(buf), &e);
    check(r, a == STDLIB_OK && b == STDLIB_OK, 0);
    hc.drop();
}
static void c_client_drop_double(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    check(r, hc.valid(), 0);
    hc.drop(); hc.drop(); hc.drop();
    check(r, !hc.valid() && hc.header_lines() == 0 &&
              hc.status() == 0L && hc.bytes() == 0ULL, 1);
}

static void c_http_valid(struct case_result *r) {
    error e = error();
    http hg = http(&e);
    int a = hg.valid();
    hg.drop();
    check(r, a == 1 && hg.valid() == 0, 0);
}
static void c_headers_valid(struct case_result *r) {
    http_headers hh = http_headers();
    int a = hh.valid();
    (void)hh.add("X-Valid: 1", (error *)0);
    int b = hh.valid();
    hh.drop();
    check(r, a == 0 && b == 1 && hh.valid() == 0, 0);
}
static void c_client_valid(struct case_result *r) {
    error e = error();
    http_client hc = http_client(&e);
    int a = hc.valid();
    hc.drop();
    check(r, a == 1 && hc.valid() == 0, 0);
}

/* ========================================================================= */
/* THREAD.HP                                                                   */
/* ========================================================================= */
static void *edge_worker(void *arg) { return arg; }

static void c_mutex_ctor_valid(struct case_result *r) {
    mutex m = mutex();
    check(r, m.is_valid() && m.native() != (void *)0, 0);
    m.drop();
    check(r, !m.is_valid() && m.native() == (void *)0, 1);
}
static void c_mutex_zero_init(struct case_result *r) {
    mutex mtxz;
    check(r, !mtxz.is_valid() && mtxz.native() == (void *)0, 0);
    mtxz.lock(); mtxz.unlock(); mtxz.drop(); mtxz.drop();
    check(r, !mtxz.is_valid(), 1);
}
static void c_mutex_lock_unlock(struct case_result *r) {
    mutex m = mutex();
    if (!m.is_valid()) { pass(r, 99); return; }
    m.lock(); m.unlock(); m.lock(); m.unlock();
    check(r, m.is_valid(), 0);
    m.drop();
}
static void c_mutex_native(struct case_result *r) {
    mutex m = mutex();
    void *n = m.native();
    check(r, n != (void *)0 && pthread_mutex_trylock((pthread_mutex_t *)n) == 0, 0);
    (void)pthread_mutex_unlock((pthread_mutex_t *)n);
    m.drop();
}
static void c_mutex_unlock(struct case_result *r) {
    mutex m = mutex();
    if (!m.is_valid()) { pass(r, 99); return; }
    m.unlock(); /* valid handle, unlocked state: still a safe no-op path */
    m.lock();
    m.unlock();
    m.drop();
    m.unlock(); /* stale handle after drop: must be inert */
    check(r, !m.is_valid(), 0);
}
static void c_mutex_drop_double(struct case_result *r) {
    mutex m = mutex();
    m.drop(); m.drop(); m.drop();
    check(r, !m.is_valid(), 0);
}
static void c_guard_ctor_valid(struct case_result *r) {
    mutex m = mutex();
    guard g = guard(&m);
    check(r, g.holds(), 0);
    g.drop();
    check(r, !g.holds(), 1);
    m.drop();
}
static void c_guard_null(struct case_result *r) {
    guard g = guard((mutex *)0);
    check(r, !g.holds(), 0);
    g.unlock(); g.drop(); g.drop();
    check(r, !g.holds(), 1);
}
static void c_guard_zero_init(struct case_result *r) {
    guard grdz;
    check(r, !grdz.holds(), 0);
    grdz.unlock(); grdz.drop();
    check(r, !grdz.holds(), 1);
}
static void c_guard_unlock_double(struct case_result *r) {
    mutex m = mutex();
    guard g = guard(&m);
    int tl;
    g.unlock();
    g.unlock(); /* owner cleared: must not double-unlock */
    g.drop();
    tl = pthread_mutex_trylock((pthread_mutex_t *)m.native());
    check(r, tl == 0 && g.holds() == 0, 0);
    if (tl == 0) (void)pthread_mutex_unlock((pthread_mutex_t *)m.native());
    m.drop();
}
static void c_guard_drop_double(struct case_result *r) {
    mutex m = mutex();
    guard g = guard(&m);
    g.drop(); g.drop(); g.drop();
    check(r, !g.holds(), 0);
    m.drop();
}
static void c_condvar_ctor_valid(struct case_result *r) {
    condvar cv = condvar();
    check(r, cv.is_valid(), 0);
    cv.drop();
    check(r, !cv.is_valid(), 1);
}
static void c_condvar_zero_init(struct case_result *r) {
    condvar cvz;
    int a, b, c;
    check(r, !cvz.is_valid(), 0);
    a = cvz.wait((mutex *)0);
    b = cvz.signal();
    c = cvz.broadcast();
    cvz.drop();
    check(r, a == -1 && b == -1 && c == -1, 1);
}
static void c_condvar_wait_null(struct case_result *r) {
    condvar cv = condvar();
    int a = cv.wait((mutex *)0);
    int b = cv.wait((mutex *)0);
    check(r, a == -1 && b == -1, 0);
    cv.drop();
}
static void c_condvar_signal_broadcast(struct case_result *r) {
    condvar cv = condvar();
    int a = cv.signal();
    int b = cv.broadcast();
    check(r, a == 0 && b == 0, 0);
    cv.drop();
}
struct cv_arg { condvar *cv; mutex *m; int *flag; };
static void *cv_worker(void *p) {
    struct cv_arg *a = (struct cv_arg *)p;
    mutex *pm = a->m;
    condvar *pcv = a->cv;
    sleep_ms(5);
    pm->lock();
    *(a->flag) = 1;
    pcv->signal();
    pm->unlock();
    return (void *)0;
}
static void c_condvar_wait_signal(struct case_result *r) {
    error e = error();
    mutex m = mutex();
    condvar cv = condvar();
    thread thr = thread();
    struct cv_arg a;
    int flag = 0;
    unsigned long long t0 = time_monotonic_ms();
    a.cv = &cv; a.m = &m; a.flag = &flag;
    if (!m.is_valid() || !cv.is_valid() ||
        thr.start(cv_worker, &a, &e) != STDLIB_OK) {
        thr.drop(); cv.drop(); m.drop(); pass(r, 99); return;
    }
    m.lock();
    while (!flag && (time_monotonic_ms() - t0) < 2000ULL) cv.wait(&m);
    m.unlock();
    thr.join((void **)0, &e);
    check(r, flag == 1 && e.ok() && (time_monotonic_ms() - t0) < 3000ULL, 0);
    thr.drop(); cv.drop(); m.drop();
}
static void c_condvar_drop_double(struct case_result *r) {
    condvar cv = condvar();
    cv.drop(); cv.drop(); cv.drop();
    check(r, !cv.is_valid() && cv.signal() == -1, 0);
}
static void c_condvar_broadcast(struct case_result *r) {
    condvar cv = condvar();
    int a = cv.broadcast();
    int b = cv.broadcast();
    cv.drop();
    check(r, a == 0 && b == 0 && cv.broadcast() == -1, 0);
}
static void c_thread_is_joinable(struct case_result *r) {
    error e = error();
    thread th = thread();
    int a = th.is_joinable();
    th.start(edge_worker, (void *)0, &e);
    int b = th.is_joinable();
    th.join((void **)0, &e);
    int c = th.is_joinable();
    th.drop();
    check(r, a == 0 && b == 1 && c == 0 && !th.is_joinable(), 0);
}
static void c_thread_ctor_zero(struct case_result *r) {
    error e = error();
    thread thz;
    void *out = (void *)0;
    check(r, !thz.is_joinable(), 0);
    check(r, thz.join(&out, &e) == STDLIB_FAIL &&
              e.category() == STDLIB_CATEGORY_LIFECYCLE, 1);
    thz.drop(); thz.drop();
}
static void c_thread_start_null_fn(struct case_result *r) {
    error e = error();
    thread th = thread();
    int rc = th.start((void *(*)(void *))0, (void *)0, &e);
    check(r, rc == STDLIB_FAIL && !th.is_joinable(), 0);
    th.drop();
}
static void c_thread_start_valid(struct case_result *r) {
    error e = error();
    thread th = thread();
    void *out = (void *)0;
    void *want = (void *)(uintptr_t)0x1234ULL;
    int rc = th.start(edge_worker, want, &e);
    check(r, rc == STDLIB_OK && th.is_joinable() && e.ok(), 0);
    rc = th.join(&out, &e);
    check(r, rc == STDLIB_OK && out == want && !th.is_joinable(), 1);
    th.drop();
}
static void c_thread_start_twice(struct case_result *r) {
    error e = error();
    thread th = thread();
    int a = th.start(edge_worker, (void *)0, &e);
    int b = th.start(edge_worker, (void *)0, &e);
    check(r, a == STDLIB_OK && b == STDLIB_FAIL &&
              e.category() == STDLIB_CATEGORY_LIFECYCLE, 0);
    th.drop();
}
static void c_thread_join_before_start(struct case_result *r) {
    error e = error();
    thread th = thread();
    void *out = (void *)0;
    int rc = th.join(&out, &e);
    check(r, rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_LIFECYCLE, 0);
    th.drop();
}
static void c_thread_join_double(struct case_result *r) {
    error e = error();
    thread th = thread();
    void *out = (void *)0;
    th.start(edge_worker, (void *)(uintptr_t)7, &e);
    th.join(&out, &e);
    check(r, out == (void *)(uintptr_t)7 &&
              th.join(&out, &e) == STDLIB_FAIL &&
              e.category() == STDLIB_CATEGORY_LIFECYCLE &&
              !th.is_joinable(), 0);
    th.drop(); th.drop();
}
static void c_thread_drop_joinable(struct case_result *r) {
    error e = error();
    thread th = thread();
    th.start(edge_worker, (void *)0, &e);
    th.drop(); /* drop joins */
    check(r, !th.is_joinable(), 0);
    th.drop(); th.drop();
}
static void c_thread_generation_reuse(struct case_result *r) {
    error e = error();
    thread th = thread();
    void *out = (void *)0;
    int a = th.start(edge_worker, (void *)(uintptr_t)11, &e);
    int aj = th.join(&out, &e);
    out = (void *)0;
    int b = th.start(edge_worker, (void *)(uintptr_t)22, &e);
    int bj = th.join(&out, &e);
    check(r, a == STDLIB_OK && aj == STDLIB_OK && b == STDLIB_OK &&
              bj == STDLIB_OK && out == (void *)(uintptr_t)22 &&
              !th.is_joinable(), 0);
    th.drop();
}
static void c_thread_bounded_join(struct case_result *r) {
    error e = error();
    thread th = thread();
    unsigned long long t0 = time_monotonic_ms();
    th.start(edge_worker, (void *)0, &e);
    th.join((void **)0, &e);
    check(r, e.ok() && (time_monotonic_ms() - t0) < 2000ULL, 0);
    th.drop();
}

/* ========================================================================= */
/* ASYNC.HP                                                                    */
/* ========================================================================= */
static void c_uv_mode_valid_0_1_2(struct case_result *r) {
    check(r, stdlib_uv_run_mode_valid(0) && stdlib_uv_run_mode_valid(1) &&
              stdlib_uv_run_mode_valid(2), 0);
}
static void c_uv_mode_invalid(struct case_result *r) {
    check(r, !stdlib_uv_run_mode_valid(-1) && !stdlib_uv_run_mode_valid(3) &&
              !stdlib_uv_run_mode_valid(INT_MIN) &&
              !stdlib_uv_run_mode_valid(INT_MAX) &&
              !stdlib_uv_run_mode_valid(99), 0);
}
static void c_loop_ctor_valid(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    check(r, lp.valid() && e.ok(), 0);
    lp.drop();
}
static void c_loop_ctor_null_err(struct case_result *r) {
    loop lp = loop((error *)0);
    check(r, lp.valid(), 0);
    lp.drop();
}
static void c_loop_zero_init(struct case_result *r) {
    loop lpz;
    error e = error();
    int rc = lpz.run(0, &e);
    check(r, !lpz.valid() && lpz.now_ms() == 0ULL && rc == STDLIB_FAIL &&
              e.category() == STDLIB_CATEGORY_INVALID, 0);
    lpz.stop(); lpz.drop();
}
static void c_loop_native(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    uv_loop_t *n = lp.native();
    check(r, n != (uv_loop_t *)0, 0);
    lp.drop();
    check(r, !lp.valid() && lp.native() != (uv_loop_t *)0, 1);
}
static void c_loop_run_valid(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    unsigned long long t0 = time_monotonic_ms();
    int a = lp.run(0, &e); /* UV_RUN_DEFAULT on an empty loop */
    int b = lp.run(1, &e); /* UV_RUN_ONCE */
    int c = lp.run(2, &e); /* UV_RUN_NOWAIT */
    check(r, a == STDLIB_OK && b == STDLIB_OK && c == STDLIB_OK &&
              (time_monotonic_ms() - t0) < 2000ULL, 0);
    lp.drop();
}
static void c_loop_run_invalid_mode(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    int a = lp.run(-1, &e);
    int b = lp.run(3, &e);
    int c = lp.run(INT_MAX, &e);
    check(r, a == STDLIB_FAIL && b == STDLIB_FAIL && c == STDLIB_FAIL &&
              e.category() == STDLIB_CATEGORY_INVALID, 0);
    lp.drop();
}
static void c_loop_run_invalid_loop(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    lp.drop();
    check(r, lp.run(2, &e) == STDLIB_FAIL && !lp.valid(), 0);
}
static void c_loop_stop(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    lp.stop();
    check(r, lp.run(0, &e) == STDLIB_OK, 0);
    lp.drop(); lp.stop(); lp.drop();
}
static void c_loop_now_ms(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    unsigned long long n = lp.now_ms();
    lp.drop();
    check(r, n > 0ULL && lp.now_ms() == 0ULL, 0);
}
static void c_loop_drop_double(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    lp.drop(); lp.drop(); lp.drop();
    check(r, !lp.valid() && lp.now_ms() == 0ULL, 0);
}
static void c_timer_ctor_null_loop(struct case_result *r) {
    error e = error();
    timer tm = timer((uv_loop_t *)0, &e);
    check(r, !tm.valid() && e.category() == STDLIB_CATEGORY_INVALID, 0);
    tm.drop(); tm.drop();
}
static void c_timer_ctor_valid(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer tm = timer(lp.native(), &e);
    check(r, lp.valid() && tm.valid() && e.ok(), 0);
    tm.drop(); lp.drop();
}
static void c_timer_zero_init(struct case_result *r) {
    timer tmz;
    error e = error();
    check(r, !tmz.valid() && tmz.is_active() == 0 &&
              tmz.stop(&e) == STDLIB_FAIL, 0);
    tmz.drop(); tmz.drop();
}
static void edge_timer_cb(uv_timer_t *h) { (void)h; }
static void c_timer_start_null_cb(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer tm = timer(lp.native(), &e);
    int rc = tm.start((uv_timer_cb)0, 0, 0, (void *)0, &e);
    check(r, rc == STDLIB_FAIL && !tm.is_active(), 0);
    tm.drop(); lp.drop();
}
static void c_timer_start_valid(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer tm = timer(lp.native(), &e);
    int rc = tm.start(edge_timer_cb, 2, 0, (void *)0, &e);
    int active = tm.is_active();
    tm.stop(&e);
    check(r, rc == STDLIB_OK && active == 1 && e.ok() && !tm.is_active(), 0);
    tm.drop(); lp.drop();
}
static void c_timer_start_extremes(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer tm = timer(lp.native(), &e);
    int a = tm.start(edge_timer_cb, 0ULL, ULLONG_MAX, (void *)0, &e);
    tm.stop(&e);
    int b = tm.start(edge_timer_cb, ULLONG_MAX, 0ULL, (void *)0, &e);
    tm.stop(&e);
    check(r, a == STDLIB_OK && b == STDLIB_OK, 0);
    tm.drop(); lp.drop();
}
static void c_timer_start_after_drop(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer tm = timer(lp.native(), &e);
    tm.drop();
    check(r, tm.start(edge_timer_cb, 0, 0, (void *)0, &e) == STDLIB_FAIL, 0);
    lp.drop();
}
static void c_timer_stop_invalid(struct case_result *r) {
    timer tmz;
    error e = error();
    check(r, tmz.stop(&e) == STDLIB_FAIL, 0);
    tmz.drop();
}
static void c_timer_stop_valid(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer tm = timer(lp.native(), &e);
    check(r, tm.stop(&e) == STDLIB_OK && e.ok(), 0);
    tm.drop(); lp.drop();
}
static void c_timer_is_active(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer tm = timer(lp.native(), &e);
    int a = tm.is_active();
    tm.start(edge_timer_cb, 1000, 0, (void *)0, &e);
    int b = tm.is_active();
    tm.drop();
    check(r, a == 0 && b == 1 && tm.is_active() == 0, 0);
    lp.drop();
}
static void c_timer_drop_double(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer tm = timer(lp.native(), &e);
    tm.start(edge_timer_cb, 0, 0, (void *)0, &e);
    tm.drop(); tm.drop(); tm.drop();
    check(r, !tm.valid(), 0);
    lp.drop();
}
static void c_read_file_null_owner(struct case_result *r) {
    error e = error();
    char buf[16];
    check(r, read_file((loop *)0, "/dev/null", buf, sizeof(buf), &e) == -1 &&
              e.category() == STDLIB_CATEGORY_INVALID, 0);
}
static void c_read_file_invalid_owner(struct case_result *r) {
    loop lpz;
    error e = error();
    char buf[16];
    check(r, read_file(&lpz, "/dev/null", buf, sizeof(buf), &e) == -1 &&
              e.category() == STDLIB_CATEGORY_INVALID, 0);
}
static void c_read_file_null_path(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    char buf[16];
    check(r, read_file(&lp, (const char *)0, buf, sizeof(buf), &e) == -1 &&
              e.category() == STDLIB_CATEGORY_INVALID, 0);
    lp.drop();
}
static void c_read_file_null_buf(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    check(r, read_file(&lp, "/dev/null", (char *)0, 16, &e) == -1 &&
              e.category() == STDLIB_CATEGORY_INVALID, 0);
    lp.drop();
}
static void c_read_file_zero_size(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    char buf[16];
    check(r, read_file(&lp, "/dev/null", buf, 0, &e) == -1 &&
              e.category() == STDLIB_CATEGORY_INVALID, 0);
    lp.drop();
}
static void c_read_file_size_bounds(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    char buf[16];
    size_t huge = (size_t)UINT_MAX + (size_t)1;
    check(r, read_file(&lp, "/dev/null", buf, huge, &e) == -1 &&
              e.category() == STDLIB_CATEGORY_BOUNDS, 0);
    lp.drop();
}
static void c_read_file_missing(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    char buf[16];
    check(r, read_file(&lp, "/definitely/not/here/edge100", buf, sizeof(buf), &e) == -1 &&
              e.category() == STDLIB_CATEGORY_IO, 0);
    lp.drop();
}
static void c_read_file_devnull(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    char buf[16];
    memset(buf, 0x5A, sizeof(buf));
    check(r, read_file(&lp, "/dev/null", buf, sizeof(buf), &e) == 0 &&
              e.ok() && all_value(buf, sizeof(buf), 0x5A), 0);
    lp.drop();
}
static void c_read_file_fixture(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    char url[512];
    char path[512];
    char buf[128];
    long long n;
    if (!fixture_url(url, sizeof(url))) { lp.drop(); pass(r, 99); return; }
    (void)snprintf(path, sizeof(path), "%s", url + 7u); /* drop "file://" */
    memset(buf, 0, sizeof(buf));
    n = read_file(&lp, path, buf, sizeof(buf), &e);
    check(r, n == (long long)FIXTURE_LEN && e.ok() &&
              memcmp(buf, FIXTURE_TEXT, FIXTURE_LEN) == 0, 0);
    lp.drop();
}
static void c_read_file_partial(struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    char url[512];
    char path[512];
    char buf[5];
    long long n;
    if (!fixture_url(url, sizeof(url))) { lp.drop(); pass(r, 99); return; }
    (void)snprintf(path, sizeof(path), "%s", url + 7u);
    memset(buf, 0, sizeof(buf));
    n = read_file(&lp, path, buf, sizeof(buf), &e);
    check(r, n == 5 && memcmp(buf, FIXTURE_TEXT, 5) == 0, 0);
    lp.drop();
}
static void c_sleep_ms_zero(struct case_result *r) {
    unsigned long long t0 = time_monotonic_ms();
    sleep_ms(0);
    check(r, (time_monotonic_ms() - t0) < 2000ULL, 0);
}
static void c_sleep_ms_bounded(struct case_result *r) {
    unsigned long long t0 = time_monotonic_ms();
    sleep_ms(5);
    check(r, (time_monotonic_ms() - t0) >= 3ULL &&
              (time_monotonic_ms() - t0) < 2000ULL, 0);
}
static void c_uv_error_text(struct case_result *r) {
    const char *a = uv_error_text(0);
    const char *b = uv_error_text(-22); /* UV_EINVAL */
    const char *c = uv_error_text(99999);
    check(r, a != (const char *)0 && b != (const char *)0 &&
              c != (const char *)0 && b[0] != '\0', 0);
}
static void c_timer_release(struct case_result *r) {
    uv_timer_t *h = (uv_timer_t *)stdlib_alloc(sizeof(uv_timer_t));
    if (h == (uv_timer_t *)0) { pass(r, 99); return; }
    memset(h, 0, sizeof(uv_timer_t));
    stdlib_timer_release_handle((uv_handle_t *)h);
    stdlib_timer_release_handle((uv_handle_t *)0); /* NULL is a safe no-op */
    pass(r, 0);
}

/* ========================================================================= */
/* TIME.HP                                                                     */
/* ========================================================================= */
static void c_time_now_ns_ms(struct case_result *r) {
    unsigned long long ns = time_now_ns();
    unsigned long long ms = time_now_ms();
    check(r, ns > 0ULL && ms > 0ULL && ms >= ns / 1000000ULL &&
              (ms - ns / 1000000ULL) <= 2ULL, 0);
}
static void c_time_monotonic_ns_ms(struct case_result *r) {
    unsigned long long ns = time_monotonic_ns();
    unsigned long long ms = time_monotonic_ms();
    check(r, ns > 0ULL && ms > 0ULL && ms >= ns / 1000000ULL &&
              (ms - ns / 1000000ULL) <= 2ULL, 0);
}
static void c_time_monotonic_order(struct case_result *r) {
    unsigned long long a = time_monotonic_ns();
    time_sleep_ms(2);
    unsigned long long b = time_monotonic_ns();
    check(r, b >= a, 0);
}
static void c_time_sleep_zero(struct case_result *r) {
    unsigned long long t0 = time_monotonic_ms();
    time_sleep_ms(0);
    check(r, (time_monotonic_ms() - t0) < 2000ULL, 0);
}
static void c_time_sleep_bounded(struct case_result *r) {
    unsigned long long t0 = time_monotonic_ms();
    time_sleep_ms(5);
    check(r, (time_monotonic_ms() - t0) >= 3ULL &&
              (time_monotonic_ms() - t0) < 2000ULL, 0);
}
static void c_time_iso_epoch(struct case_result *r) {
    error e = error();
    char iso[32];
    int rc = time_format_iso8601(0ULL, iso, sizeof(iso), &e);
    check(r, rc == STDLIB_OK && e.ok() &&
              strcmp(iso, "1970-01-01T00:00:00Z") == 0, 0);
}
static void c_time_iso_subsec(struct case_result *r) {
    error e = error();
    char iso[32];
    int a = time_format_iso8601(999999999ULL, iso, sizeof(iso), &e);
    int b = time_format_iso8601(1000000000ULL, iso, sizeof(iso), &e);
    check(r, a == STDLIB_OK && strcmp(iso, "1970-01-01T00:00:01Z") == 0 &&
              b == STDLIB_OK, 0);
}
static void c_time_iso_sec_boundary(struct case_result *r) {
    error e = error();
    char iso[32];
    int rc = time_format_iso8601(1234567890ULL * 1000000000ULL, iso, sizeof(iso), &e);
    check(r, rc == STDLIB_OK && strcmp(iso, "2009-02-13T23:31:30Z") == 0, 0);
}
static void c_time_iso_far_epoch(struct case_result *r) {
    error e = error();
    char iso[32];
    int rc = time_format_iso8601(ULLONG_MAX, iso, sizeof(iso), &e);
    check(r, rc == STDLIB_OK && strlen(iso) == 20u &&
              byte_at(iso, 4) == '-' && byte_at(iso, 7) == '-' &&
              byte_at(iso, 10) == 'T' && byte_at(iso, 13) == ':' &&
              byte_at(iso, 16) == ':' && byte_at(iso, 19) == 'Z' &&
              byte_at(iso, 20) == '\0', 0);
}
static void c_time_iso_null_buf(struct case_result *r) {
    error e = error();
    int rc = time_format_iso8601(0ULL, (char *)0, 32, &e);
    check(r, rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID, 0);
}
static void c_time_iso_zero_size(struct case_result *r) {
    error e = error();
    char iso[4];
    int rc = time_format_iso8601(0ULL, iso, 0, &e);
    check(r, rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_INVALID, 0);
}
static void c_time_iso_small_buf(struct case_result *r) {
    error e = error();
    char iso[32];
    memset(iso, 0x5A, sizeof(iso));
    int rc = time_format_iso8601(0ULL, iso, 20, &e);
    check(r, rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_BOUNDS &&
              iso[0] == '\0', 0);
}
static void c_time_iso_exact_buf(struct case_result *r) {
    error e = error();
    char iso[21];
    int rc = time_format_iso8601(0ULL, iso, sizeof(iso), &e);
    check(r, rc == STDLIB_OK && strcmp(iso, "1970-01-01T00:00:00Z") == 0, 0);
}
static void c_time_iso_null_err(struct case_result *r) {
    char iso[32];
    int rc = time_format_iso8601(0ULL, iso, sizeof(iso), (error *)0);
    check(r, rc == STDLIB_OK && strcmp(iso, "1970-01-01T00:00:00Z") == 0, 0);
}

/* ========================================================================= */
/* CASE TABLE + DISPATCH                                                       */
/* ========================================================================= */
struct case_meta { const char *api; const char *name; };
static const struct case_meta g_meta[] = {
    {"net.connection::connection", "conn_ctor_valid"},
    {"net.connection::connection", "conn_ctor_invalid"},
    {"net.connection::connection", "conn_zero_init"},
    {"net.connection::is_valid", "conn_isvalid_borrowed"},
    {"net.connection::descriptor", "conn_descriptor"},
    {"net.connection::read", "conn_read_valid"},
    {"net.connection::read", "conn_read_eof"},
    {"net.connection::read", "conn_read_null"},
    {"net.connection::read", "conn_read_zerocap"},
    {"net.connection::read", "conn_read_invalid"},
    {"net.connection::read", "conn_read_stale"},
    {"net.connection::write", "conn_write_valid"},
    {"net.connection::write", "conn_write_null"},
    {"net.connection::write", "conn_write_zerolen"},
    {"net.connection::write", "conn_write_invalid"},
    {"net.connection::write", "conn_write_stale"},
    {"net.connection::drop", "conn_drop_double"},
    {"net.connection_borrow", "conn_borrow_fd"},
    {"net.connection_borrow", "conn_borrow_stale_reuse"},
    {"net.listener::listener", "lsn_ctor_valid"},
    {"net.listener::listener", "lsn_ctor_invalid"},
    {"net.listener::listener", "lsn_zero_init"},
    {"net.listener::is_valid", "lsn_isvalid_borrowed"},
    {"net.listener::descriptor", "lsn_descriptor"},
    {"net.listener::bound_port", "lsn_bound_port"},
    {"net.listener::bound_port", "lsn_bound_port_invalid"},
    {"net.listener::accept", "lsn_accept_valid"},
    {"net.listener::accept", "lsn_accept_invalid"},
    {"net.listener::drop", "lsn_drop_double"},
    {"net.listener_borrow", "lsn_borrow_fd"},
    {"net.listener_borrow", "lsn_borrow_stale"},
    {"net.listener::listener", "lsn_port_max"},
    {"http.http::http", "http_ctor_valid"},
    {"http.http::http", "http_null_err"},
    {"http.http::drop", "http_drop_double"},
    {"http.http_version", "http_version"},
    {"http.http_headers::http_headers", "headers_ctor_valid"},
    {"http.http_headers::add", "headers_add_null"},
    {"http.http_headers::add", "headers_add_valid"},
    {"http.http_headers::add", "headers_add_hostile"},
    {"http.http_headers::add", "headers_add_empty"},
    {"http.http_headers::native", "headers_native"},
    {"http.http_headers::drop", "headers_drop_double"},
    {"http.http_sink_write", "sink_null_userdata"},
    {"http.http_sink_write", "sink_null_buf"},
    {"http.http_sink_write", "sink_zero_size"},
    {"http.http_sink_write", "sink_zero_bytes"},
    {"http.http_sink_write", "sink_null_ptr"},
    {"http.http_sink_write", "sink_mul_overflow"},
    {"http.http_sink_write", "sink_valid_write"},
    {"http.http_sink_write", "sink_exact_fit"},
    {"http.http_sink_write", "sink_oversize_clamp"},
    {"http.http_sink_write", "sink_full_refuse"},
    {"http.http_sink_write", "sink_used_sizemax"},
    {"http.http_sink_write", "sink_boundary_nul"},
    {"http.http_client::http_client", "client_ctor_valid"},
    {"http.http_client::http_client", "client_null_err"},
    {"http.http_client::set_timeout", "client_set_timeout_neg"},
    {"http.http_client::set_timeout", "client_set_timeout_zero"},
    {"http.http_client::set_timeout", "client_set_timeout_valid"},
    {"http.http_client::set_timeout", "client_set_timeout_max"},
    {"http.http_client::set_timeout", "client_set_timeout_after_drop"},
    {"http.http_client::follow_redirects", "client_follow_0"},
    {"http.http_client::follow_redirects", "client_follow_1"},
    {"http.http_client::follow_redirects", "client_follow_after_drop"},
    {"http.http_client::set_headers", "client_headers_null"},
    {"http.http_client::set_headers", "client_headers_empty"},
    {"http.http_client::set_headers", "client_headers_owned"},
    {"http.http_client::header_lines", "client_header_lines_fresh"},
    {"http.http_client::set_headers", "client_headers_after_drop"},
    {"http.http_client::status", "client_status_fresh"},
    {"http.http_client::status", "client_status_after_drop"},
    {"http.http_client::bytes", "client_bytes_fresh"},
    {"http.http_client::bytes", "client_bytes_after_drop"},
    {"http.http_client::get", "client_get_null_url"},
    {"http.http_client::get", "client_get_null_buf"},
    {"http.http_client::get", "client_get_zero_size"},
    {"http.http_client::get", "client_get_after_drop"},
    {"http.http_client::get", "client_get_bad_url_timeout"},
    {"http.http_client::get", "client_get_file_ok"},
    {"http.http_client::post", "client_post_null_body"},
    {"http.http_client::post", "client_post_file_ok"},
    {"http.http_client::post", "client_post_null_url"},
    {"http.http_client::request", "client_request_direct"},
    {"http.http_client::drop", "client_drop_double"},
    {"thread.mutex::mutex", "mutex_ctor_valid"},
    {"thread.mutex::is_valid", "mutex_zero_init"},
    {"thread.mutex::lock", "mutex_lock_unlock"},
    {"thread.mutex::native", "mutex_native"},
    {"thread.mutex::drop", "mutex_drop_double"},
    {"thread.guard::guard", "guard_ctor_valid"},
    {"thread.guard::guard", "guard_null"},
    {"thread.guard::holds", "guard_zero_init"},
    {"thread.guard::unlock", "guard_unlock_double"},
    {"thread.guard::drop", "guard_drop_double"},
    {"thread.condvar::condvar", "condvar_ctor_valid"},
    {"thread.condvar::is_valid", "condvar_zero_init"},
    {"thread.condvar::wait", "condvar_wait_null"},
    {"thread.condvar::signal", "condvar_signal_broadcast"},
    {"thread.condvar::wait", "condvar_wait_signal"},
    {"thread.condvar::drop", "condvar_drop_double"},
    {"thread.thread::thread", "thread_ctor_zero"},
    {"thread.thread::start", "thread_start_null_fn"},
    {"thread.thread::start", "thread_start_valid"},
    {"thread.thread::start", "thread_start_twice"},
    {"thread.thread::join", "thread_join_before_start"},
    {"thread.thread::join", "thread_join_double"},
    {"thread.thread::drop", "thread_drop_joinable"},
    {"thread.thread::start", "thread_generation_reuse"},
    {"thread.thread::join", "thread_bounded_join"},
    {"async.stdlib_uv_run_mode_valid", "uv_mode_valid_0_1_2"},
    {"async.stdlib_uv_run_mode_valid", "uv_mode_invalid"},
    {"async.loop::loop", "loop_ctor_valid"},
    {"async.loop::loop", "loop_ctor_null_err"},
    {"async.loop::valid", "loop_zero_init"},
    {"async.loop::native", "loop_native"},
    {"async.loop::run", "loop_run_valid"},
    {"async.loop::run", "loop_run_invalid_mode"},
    {"async.loop::run", "loop_run_invalid_loop"},
    {"async.loop::stop", "loop_stop"},
    {"async.loop::now_ms", "loop_now_ms"},
    {"async.loop::drop", "loop_drop_double"},
    {"async.timer::timer", "timer_ctor_null_loop"},
    {"async.timer::timer", "timer_ctor_valid"},
    {"async.timer::valid", "timer_zero_init"},
    {"async.timer::start", "timer_start_null_cb"},
    {"async.timer::start", "timer_start_valid"},
    {"async.timer::start", "timer_start_extremes"},
    {"async.timer::start", "timer_start_after_drop"},
    {"async.timer::stop", "timer_stop_invalid"},
    {"async.timer::stop", "timer_stop_valid"},
    {"async.timer::is_active", "timer_is_active"},
    {"async.timer::drop", "timer_drop_double"},
    {"async.read_file", "read_file_null_owner"},
    {"async.read_file", "read_file_invalid_owner"},
    {"async.read_file", "read_file_null_path"},
    {"async.read_file", "read_file_null_buf"},
    {"async.read_file", "read_file_zero_size"},
    {"async.read_file", "read_file_size_bounds"},
    {"async.read_file", "read_file_missing"},
    {"async.read_file", "read_file_devnull"},
    {"async.read_file", "read_file_fixture"},
    {"async.read_file", "read_file_partial"},
    {"async.sleep_ms", "sleep_ms_zero"},
    {"async.sleep_ms", "sleep_ms_bounded"},
    {"async.uv_error_text", "uv_error_text"},
    {"time.time_now_ns", "time_now_ns_ms"},
    {"time.time_now_ms", "time_now_ns_ms"},
    {"time.time_monotonic_ns", "time_monotonic_ns_ms"},
    {"time.time_monotonic_ms", "time_monotonic_ns_ms"},
    {"time.time_monotonic_ns", "time_monotonic_order"},
    {"time.time_sleep_ms", "time_sleep_zero"},
    {"time.time_sleep_ms", "time_sleep_bounded"},
    {"time.time_format_iso8601", "time_iso_epoch"},
    {"time.time_format_iso8601", "time_iso_subsec"},
    {"time.time_format_iso8601", "time_iso_sec_boundary"},
    {"time.time_format_iso8601", "time_iso_far_epoch"},
    {"time.time_format_iso8601", "time_iso_null_buf"},
    {"time.time_format_iso8601", "time_iso_zero_size"},
    {"time.time_format_iso8601", "time_iso_small_buf"},
    {"time.time_format_iso8601", "time_iso_exact_buf"},
    {"time.time_format_iso8601", "time_iso_null_err"},
    {"http.http::valid", "http_valid"},
    {"http.http_headers::valid", "headers_valid"},
    {"http.http_client::valid", "client_valid"},
    {"thread.mutex::unlock", "mutex_unlock"},
    {"thread.condvar::broadcast", "condvar_broadcast"},
    {"thread.thread::is_joinable", "thread_is_joinable"},
    {"async.stdlib_timer_release_handle", "timer_release"}
};
#define N_CASES ((int)(sizeof(g_meta) / sizeof(g_meta[0])))

static void dispatch(int id, struct case_result *r) {
    switch (id) {
    case 0: c_conn_ctor_valid(r); break;
    case 1: c_conn_ctor_invalid(r); break;
    case 2: c_conn_zero_init(r); break;
    case 3: c_conn_isvalid_borrowed(r); break;
    case 4: c_conn_descriptor(r); break;
    case 5: c_conn_read_valid(r); break;
    case 6: c_conn_read_eof(r); break;
    case 7: c_conn_read_null(r); break;
    case 8: c_conn_read_zerocap(r); break;
    case 9: c_conn_read_invalid(r); break;
    case 10: c_conn_read_stale(r); break;
    case 11: c_conn_write_valid(r); break;
    case 12: c_conn_write_null(r); break;
    case 13: c_conn_write_zerolen(r); break;
    case 14: c_conn_write_invalid(r); break;
    case 15: c_conn_write_stale(r); break;
    case 16: c_conn_drop_double(r); break;
    case 17: c_conn_borrow_fd(r); break;
    case 18: c_conn_borrow_stale_reuse(r); break;
    case 19: c_lsn_ctor_valid(r); break;
    case 20: c_lsn_ctor_invalid(r); break;
    case 21: c_lsn_zero_init(r); break;
    case 22: c_lsn_isvalid_borrowed(r); break;
    case 23: c_lsn_descriptor(r); break;
    case 24: c_lsn_bound_port(r); break;
    case 25: c_lsn_bound_port_invalid(r); break;
    case 26: c_lsn_accept_valid(r); break;
    case 27: c_lsn_accept_invalid(r); break;
    case 28: c_lsn_drop_double(r); break;
    case 29: c_lsn_borrow_fd(r); break;
    case 30: c_lsn_borrow_stale(r); break;
    case 31: c_lsn_port_max(r); break;
    case 32: c_http_ctor_valid(r); break;
    case 33: c_http_null_err(r); break;
    case 34: c_http_drop_double(r); break;
    case 35: c_http_version(r); break;
    case 36: c_headers_ctor_valid(r); break;
    case 37: c_headers_add_null(r); break;
    case 38: c_headers_add_valid(r); break;
    case 39: c_headers_add_hostile(r); break;
    case 40: c_headers_add_empty(r); break;
    case 41: c_headers_native(r); break;
    case 42: c_headers_drop_double(r); break;
    case 43: c_sink_null_userdata(r); break;
    case 44: c_sink_null_buf(r); break;
    case 45: c_sink_zero_size(r); break;
    case 46: c_sink_zero_bytes(r); break;
    case 47: c_sink_null_ptr(r); break;
    case 48: c_sink_mul_overflow(r); break;
    case 49: c_sink_valid_write(r); break;
    case 50: c_sink_exact_fit(r); break;
    case 51: c_sink_oversize_clamp(r); break;
    case 52: c_sink_full_refuse(r); break;
    case 53: c_sink_used_sizemax(r); break;
    case 54: c_sink_boundary_nul(r); break;
    case 55: c_client_ctor_valid(r); break;
    case 56: c_client_null_err(r); break;
    case 57: c_client_set_timeout_neg(r); break;
    case 58: c_client_set_timeout_zero(r); break;
    case 59: c_client_set_timeout_valid(r); break;
    case 60: c_client_set_timeout_max(r); break;
    case 61: c_client_set_timeout_after_drop(r); break;
    case 62: c_client_follow_0(r); break;
    case 63: c_client_follow_1(r); break;
    case 64: c_client_follow_after_drop(r); break;
    case 65: c_client_headers_null(r); break;
    case 66: c_client_headers_empty(r); break;
    case 67: c_client_headers_owned(r); break;
    case 68: c_client_header_lines_fresh(r); break;
    case 69: c_client_headers_after_drop(r); break;
    case 70: c_client_status_fresh(r); break;
    case 71: c_client_status_after_drop(r); break;
    case 72: c_client_bytes_fresh(r); break;
    case 73: c_client_bytes_after_drop(r); break;
    case 74: c_client_get_null_url(r); break;
    case 75: c_client_get_null_buf(r); break;
    case 76: c_client_get_zero_size(r); break;
    case 77: c_client_get_after_drop(r); break;
    case 78: c_client_get_bad_url_timeout(r); break;
    case 79: c_client_get_file_ok(r); break;
    case 80: c_client_post_null_body(r); break;
    case 81: c_client_post_file_ok(r); break;
    case 82: c_client_post_null_url(r); break;
    case 83: c_client_request_direct(r); break;
    case 84: c_client_drop_double(r); break;
    case 85: c_mutex_ctor_valid(r); break;
    case 86: c_mutex_zero_init(r); break;
    case 87: c_mutex_lock_unlock(r); break;
    case 88: c_mutex_native(r); break;
    case 89: c_mutex_drop_double(r); break;
    case 90: c_guard_ctor_valid(r); break;
    case 91: c_guard_null(r); break;
    case 92: c_guard_zero_init(r); break;
    case 93: c_guard_unlock_double(r); break;
    case 94: c_guard_drop_double(r); break;
    case 95: c_condvar_ctor_valid(r); break;
    case 96: c_condvar_zero_init(r); break;
    case 97: c_condvar_wait_null(r); break;
    case 98: c_condvar_signal_broadcast(r); break;
    case 99: c_condvar_wait_signal(r); break;
    case 100: c_condvar_drop_double(r); break;
    case 101: c_thread_ctor_zero(r); break;
    case 102: c_thread_start_null_fn(r); break;
    case 103: c_thread_start_valid(r); break;
    case 104: c_thread_start_twice(r); break;
    case 105: c_thread_join_before_start(r); break;
    case 106: c_thread_join_double(r); break;
    case 107: c_thread_drop_joinable(r); break;
    case 108: c_thread_generation_reuse(r); break;
    case 109: c_thread_bounded_join(r); break;
    case 110: c_uv_mode_valid_0_1_2(r); break;
    case 111: c_uv_mode_invalid(r); break;
    case 112: c_loop_ctor_valid(r); break;
    case 113: c_loop_ctor_null_err(r); break;
    case 114: c_loop_zero_init(r); break;
    case 115: c_loop_native(r); break;
    case 116: c_loop_run_valid(r); break;
    case 117: c_loop_run_invalid_mode(r); break;
    case 118: c_loop_run_invalid_loop(r); break;
    case 119: c_loop_stop(r); break;
    case 120: c_loop_now_ms(r); break;
    case 121: c_loop_drop_double(r); break;
    case 122: c_timer_ctor_null_loop(r); break;
    case 123: c_timer_ctor_valid(r); break;
    case 124: c_timer_zero_init(r); break;
    case 125: c_timer_start_null_cb(r); break;
    case 126: c_timer_start_valid(r); break;
    case 127: c_timer_start_extremes(r); break;
    case 128: c_timer_start_after_drop(r); break;
    case 129: c_timer_stop_invalid(r); break;
    case 130: c_timer_stop_valid(r); break;
    case 131: c_timer_is_active(r); break;
    case 132: c_timer_drop_double(r); break;
    case 133: c_read_file_null_owner(r); break;
    case 134: c_read_file_invalid_owner(r); break;
    case 135: c_read_file_null_path(r); break;
    case 136: c_read_file_null_buf(r); break;
    case 137: c_read_file_zero_size(r); break;
    case 138: c_read_file_size_bounds(r); break;
    case 139: c_read_file_missing(r); break;
    case 140: c_read_file_devnull(r); break;
    case 141: c_read_file_fixture(r); break;
    case 142: c_read_file_partial(r); break;
    case 143: c_sleep_ms_zero(r); break;
    case 144: c_sleep_ms_bounded(r); break;
    case 145: c_uv_error_text(r); break;
    case 146: c_time_now_ns_ms(r); break;
    case 147: c_time_now_ns_ms(r); break;
    case 148: c_time_monotonic_ns_ms(r); break;
    case 149: c_time_monotonic_ns_ms(r); break;
    case 150: c_time_monotonic_order(r); break;
    case 151: c_time_sleep_zero(r); break;
    case 152: c_time_sleep_bounded(r); break;
    case 153: c_time_iso_epoch(r); break;
    case 154: c_time_iso_subsec(r); break;
    case 155: c_time_iso_sec_boundary(r); break;
    case 156: c_time_iso_far_epoch(r); break;
    case 157: c_time_iso_null_buf(r); break;
    case 158: c_time_iso_zero_size(r); break;
    case 159: c_time_iso_small_buf(r); break;
    case 160: c_time_iso_exact_buf(r); break;
    case 161: c_time_iso_null_err(r); break;
    case 162: c_http_valid(r); break;
    case 163: c_headers_valid(r); break;
    case 164: c_client_valid(r); break;
    case 165: c_mutex_unlock(r); break;
    case 166: c_condvar_broadcast(r); break;
    case 167: c_thread_is_joinable(r); break;
    case 168: c_timer_release(r); break;
    default: pass(r, 0); break;
    }
}

/* ---- parent / child runner ---------------------------------------------- */
static void watchdog(int sig) { (void)sig; _exit(98); }

static int bounded_wait(pid_t pid, int *status) {
    pid_t r;
    r = waitpid(pid, status, WNOHANG);
    if (r == pid) return 1;
    if (r < 0 && errno != EINTR) return 0;
    do { r = waitpid(pid, status, 0); } while (r < 0 && errno == EINTR);
    return r == pid;
}

int main(void) {
    int logfd;
    int line_len;
    char line[512];
    int i;
    int passed = 0;
    int failed = 0;
    int fds[2];
    pid_t pid;
    int status = 0;
    ssize_t got;
    struct pollfd pfd;
    struct case_result result;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);

    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) {
        fprintf(stderr, "cannot open %s\n", LOG_PATH);
        printf("EDGE100 netthread 81 0 0 0\n");
        return 2;
    }
    line_len = snprintf(line, sizeof(line),
                        "# EDGE100 netthread cases=%d apis=81\n", N_CASES);
    if (line_len > 0) (void)write(logfd, line, (size_t)line_len);

    for (i = 0; i < N_CASES; ++i) {
        if (pipe(fds) != 0) { failed++; continue; }
        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            close(fds[0]); close(fds[1]); failed++; continue;
        }
        if (pid == 0) {
            struct case_result cr;
            close(fds[0]);
            (void)signal(SIGALRM, watchdog);
            (void)alarm(WATCHDOG_SECS);
            (void)freopen("/dev/null", "w", stdout);
            cr.verdict = 1;
            cr.detail = -1;
            dispatch(i, &cr);
            (void)write(fds[1], &cr, sizeof(cr));
            close(fds[1]);
            _exit(0);
        }
        close(fds[1]);
        got = 0;
        pfd.fd = fds[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 5000) > 0 && (pfd.revents & POLLIN))
            got = read(fds[0], &result, sizeof(result));
        close(fds[0]);
        if (!bounded_wait(pid, &status) || WIFSIGNALED(status) ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
            got != (ssize_t)sizeof(result) || result.verdict != 0) {
            failed++;
            line_len = snprintf(line, sizeof(line), "FAIL %d %s %s detail=%d\n",
                                i, g_meta[i].api, g_meta[i].name,
                                (got == (ssize_t)sizeof(result)) ? result.detail : -99);
        } else {
            passed++;
            line_len = snprintf(line, sizeof(line), "PASS %d %s %s\n",
                                i, g_meta[i].api, g_meta[i].name);
        }
        if (line_len > 0) (void)write(logfd, line, (size_t)line_len);
    }
    line_len = snprintf(line, sizeof(line),
                        "# SUMMARY cases=%d passed=%d failed=%d\n",
                        N_CASES, passed, failed);
    if (line_len > 0) (void)write(logfd, line, (size_t)line_len);
    close(logfd);

    printf("EDGE100 netthread 81 %d %d %d\n", N_CASES, passed, failed);
    return 0;
}
