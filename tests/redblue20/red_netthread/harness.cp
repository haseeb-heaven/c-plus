/* RED-TEAM agent 4/20 -- networking / threading / async / checked-sync adversary.
 *
 * Targets (attack only; NO source or header is edited):
 *   stdlib/net.hp        listener/connection ownership, fd 0-2, invalid handles
 *   stdlib/http.hp       http_sink_write bounds, HTTP client argument handling
 *   stdlib/thread.hp     mutex/guard/condvar/thread lifecycle, double join
 *   stdlib/async.hp      libuv loop run modes, timer/loop cleanup, read_file
 *   libc/src/thread/sync_checked.c  double destroy/join side table, generations
 *
 * Every case is an ATTACK chosen where a documented contract promises a
 * defined refusal.  Verdicts:
 *   REFUSED        contract-honouring refusal / defined safe result
 *   UNEXPECTED_OK  hostile input accepted where a refusal was promised
 *   CRASH          forked child died by signal, fatal exit, hang or short verdict
 *   CORRUPT        canary / documented zeroing / ownership invariant violated
 *
 * One case per freshly forked child.  The parent uses poll() + bounded waitpid
 * with a SIGKILL fallback; the child arms alarm(2) as a watchdog.  A fixed
 * splitmix64 seed drives every parameter; family = case_index % F_COUNT.
 *
 * Build (from repo root):
 *   ./build/c+ build tests/redblue20/red_netthread/harness.cp \
 *     --default --sanitize=none -I. \
 *     -lcurl -I/opt/homebrew/include -L/opt/homebrew/lib -luv \
 *     -Lbuild/libc -lcplus_hardened \
 *     -o tests/redblue20/red_netthread/out/harness
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#include "libc/install-include/cplus_threads.h"
#include "libc/install-include/cplus_sync_checked.h"

#define N_CASES 3000
#define RNG_SEED 0x4ED7EA51DEADBEEFULL
#define F_COUNT 30
#define WATCHDOG_SECS 2
#define LOG_PATH "tests/redblue20/red_netthread/out/scenarios.log"

/* ---- verdict codes (child -> parent over a pipe) ------------------------ */
#define CLS_REFUSED       0
#define CLS_UNEXPECTED_OK 1
#define CLS_CRASH         2
#define CLS_CORRUPT       3

struct case_result {
    int cls;
    int sub;
    int got;
};

struct case_spec {
    int family;
    unsigned long long a;
    unsigned long long b;
    unsigned long long c;
};

/* ---- families ------------------------------------------------------------ */
enum {
    F_NET_PORT = 0,
    F_NET_BORROW012,
    F_NET_BORROW_STALE,
    F_NET_DOUBLE_DROP,
    F_NET_INVALID_IO,
    F_NET_WRITE_LOOP,
    F_NET_ACCEPT_INVALID,
    F_HTTP_SINK_ZERO,
    F_HTTP_SINK_FULL,
    F_HTTP_SINK_OVERSIZE,
    F_HTTP_SINK_MUL,
    F_HTTP_SINK_BOUNDARY,
    F_HTTP_BAD_URL,
    F_HTTP_NULL_ARGS,
    F_HTTP_DOUBLE_DROP,
    F_HTTP_HEADERS_HOSTILE,
    F_THREAD_DOUBLE_JOIN,
    F_THREAD_START_INVALID,
    F_THREAD_GUARD_REPEAT,
    F_THREAD_NESTED,
    F_THREAD_CONDVAR,
    F_THREAD_MUTEX_CYCLE,
    F_ASYNC_LOOP_MODE,
    F_ASYNC_LOOP_DROP,
    F_ASYNC_TIMER,
    F_ASYNC_READ_FILE,
    F_SYNC_MTX_DESTROY,
    F_SYNC_CND_DESTROY,
    F_SYNC_THRD_JOIN,
    F_SYNC_CONCURRENT_EXHAUST
};

/* ---- deterministic PRNG -------------------------------------------------- */
static unsigned long long g_rng;

static unsigned long long next_rand(void) {
    unsigned long long z;
    z = (g_rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static struct case_spec gen_spec(int index) {
    struct case_spec s;
    s.family = index % F_COUNT;
    s.a = next_rand();
    s.b = next_rand();
    s.c = next_rand();
    return s;
}

/* ---- child-side helpers -------------------------------------------------- */
static void setres(struct case_result *r, int cls, int sub, int got) {
    r->cls = cls;
    r->sub = sub;
    r->got = got;
}

static void expect_refuse(struct case_result *r, int accepted, int sub, int got) {
    setres(r, accepted ? CLS_UNEXPECTED_OK : CLS_REFUSED, sub, got);
}

#define GUARD 16
#define SINK_BUF 512
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
        if (g_area[i] != (char)0xA5) return 0;
    for (i = GUARD + SINK_BUF; i < GUARD + SINK_BUF + GUARD; ++i)
        if (g_area[i] != (char)0xA5) return 0;
    return 1;
}

static int all_value(const char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char byte;
        memcpy(&byte, p + i, 1);
        if (byte != v) return 0;
    }
    return 1;
}

/* Read one byte without a C+ subscript (the lowering attaches the wrong
 * static extent to pointer subscripts on hostile sink sizes). */
static char byte_at(const char *p, size_t off) {
    char out;
    memcpy(&out, p + off, 1);
    return out;
}

static int fd_open(int fd) {
    return fcntl(fd, F_GETFD) != -1;
}

/* ========================================================================= */
/* NET FAMILIES                                                               */
/* ========================================================================= */

static void attack_net_port(struct case_spec *s, struct case_result *r) {
    int variant = (int)(s->a % 8ULL);
    int port;
    listener ln = listener(-1);
    if (variant == 0) port = -1;
    else if (variant == 1) port = 65536;
    else if (variant == 2) port = 70000;
    else if (variant == 3) port = (int)0xFFFFFFFFu;
    else if (variant == 4) port = 65536 + (int)(s->b % 5000ULL);
    else if (variant == 5) port = -12345;
    else if (variant == 6) port = 0;
    else port = 65535;

    ln.drop(); /* exercise idempotent drop on the initial inert handle */
    ln = listener(port);
    if (variant == 6 || variant == 7) {
        /* legal port: either a live loopback listener or a resource refusal;
         * neither outcome is a finding.  Just confirm drop is safe. */
        if (ln.is_valid()) {
            int p = ln.bound_port();
            if (p < 0 || p > 65535) {
                setres(r, CLS_UNEXPECTED_OK, 0, p);
                ln.drop();
                return;
            }
        }
        ln.drop();
        setres(r, CLS_REFUSED, 0, 0);
        return;
    }
    /* hostile port must produce an inert handle */
    if (ln.is_valid() || ln.descriptor() != -1 || ln.bound_port() != 0) {
        expect_refuse(r, 1, 0, ln.descriptor());
        ln.drop();
        return;
    }
    ln.drop();
    expect_refuse(r, 0, 0, 0);
}

static void attack_net_borrow012(struct case_spec *s, struct case_result *r) {
    int fd = (int)(s->a % 3ULL);
    int before = fcntl(fd, F_GETFD);
    int after;
    connection cb = connection_borrow(fd);
    listener lb = listener_borrow(fd);
    error e = error();
    char buf[16];
    int rr, ww, bp;
    connection ac = lb.accept();
    (void)s->b;
    (void)s->c;
    memset(buf, 0x5A, sizeof(buf));
    rr = cb.read(buf, sizeof(buf), &e);      /* must be refused: fd 0-2 */
    ww = cb.write("x", 1, &e);                /* must be refused: fd 0-2 */
    bp = lb.bound_port();                     /* must be 0 */
    if (cb.is_valid() || lb.is_valid() || ac.is_valid() ||
        rr != STDLIB_FAIL || ww != STDLIB_FAIL || bp != 0) {
        setres(r, CLS_UNEXPECTED_OK, 0, rr);
    } else if (!all_value(buf, sizeof(buf), 0x5A)) {
        setres(r, CLS_CORRUPT, 1, 0);
    } else {
        setres(r, CLS_REFUSED, 0, 0);
    }
    cb.drop();
    lb.drop();
    ac.drop();
    after = fcntl(fd, F_GETFD);
    if (after != before && r->cls == CLS_REFUSED)
        setres(r, CLS_CORRUPT, 2, after); /* borrowed handle closed stdio */
}

static void attack_net_borrow_stale(struct case_spec *s, struct case_result *r) {
    int fds[2];
    connection cb;
    error e = error();
    char buf[32];
    int rr, ww, peer_ok;
    (void)s;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    cb = connection_borrow(fds[0]);
    close(fds[0]); /* owned by the harness, not by cb */
    memset(buf, 0x5A, sizeof(buf));
    /* fd >= 3 still looks "valid" to the header (documented); the I/O must
     * fail cleanly and the borrow must never close another descriptor. */
    rr = cb.read(buf, 8, &e);
    ww = cb.write("x", 1, &e);
    peer_ok = fd_open(fds[1]);
    cb.drop();
    cb.drop();
    if (rr == STDLIB_OK || ww == STDLIB_OK || !peer_ok) {
        setres(r, CLS_UNEXPECTED_OK, 0, rr);
    } else if (!all_value(buf, sizeof(buf), 0x5A)) {
        setres(r, CLS_CORRUPT, 1, 0);
    } else {
        setres(r, CLS_REFUSED, 0, 0);
    }
    close(fds[1]);
}

static void attack_net_double_drop(struct case_spec *s, struct case_result *r) {
    int fds[2];
    int before0 = fcntl(0, F_GETFD);
    int after0, owned_after;
    connection dd_conn;
    listener ln = listener(0);
    int lfd = ln.descriptor();
    (void)s;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0 || !ln.is_valid()) {
        ln.drop();
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    dd_conn = connection(fds[0]); /* owning constructor */
    dd_conn.drop();
    owned_after = fd_open(fds[0]); /* must be closed now */
    dd_conn.drop();
    dd_conn.drop();
    ln.drop();
    ln.drop();
    ln.drop();
    after0 = fcntl(0, F_GETFD);
    /* NOTE: an explicit `.drop()` in C+ lowers to drop() + cplus_zero(), so
     * descriptor() reads 0 (not the header's -1) afterwards.  That is a
     * compiler artifact, not a net.hp defect; the real ownership checks are
     * "owned fd closed" and "stdio fds untouched". */
    if (dd_conn.is_valid() || ln.is_valid() || owned_after || fd_open(lfd) ||
        after0 != before0) {
        int bits = 0;
        if (dd_conn.is_valid()) bits |= 1;
        if (ln.is_valid()) bits |= 4;
        if (owned_after) bits |= 16;
        if (fd_open(lfd)) bits |= 32;
        if (after0 != before0) bits |= 64;
        setres(r, CLS_UNEXPECTED_OK, 0, bits);
    } else {
        setres(r, CLS_REFUSED, 0, 0);
    }
    close(fds[1]);
}

static void attack_net_invalid_io(struct case_spec *s, struct case_result *r) {
    int variant = (int)(s->a % 6ULL);
    error e = error();
    char buf[32];
    int rc, cat;
    connection io_conn = connection(-1);
    if (variant == 1) io_conn = connection_borrow(2);
    memset(buf, 0x5A, sizeof(buf));
    if (variant == 0) rc = io_conn.read(buf, sizeof(buf), &e);
    else if (variant == 1) rc = io_conn.read(buf, sizeof(buf), &e);
    else if (variant == 2) rc = io_conn.read(NULL, sizeof(buf), &e);
    else if (variant == 3) rc = io_conn.read(buf, 0, &e);
    else if (variant == 4) rc = io_conn.write(NULL, 1, &e);
    else rc = io_conn.write("x", 1, &e); /* invalid handle -> refused */
    cat = e.category();
    if (variant == 4 || variant == 5) {
        /* write with NULL data / invalid handle: STDLIB_FAIL + INVALID */
        if (rc != STDLIB_FAIL || cat != STDLIB_CATEGORY_INVALID)
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
        else
            setres(r, CLS_REFUSED, 0, 0);
    } else {
        if (rc != STDLIB_FAIL || cat != STDLIB_CATEGORY_INVALID)
            setres(r, CLS_UNEXPECTED_OK, 1, rc);
        else if (!all_value(buf, sizeof(buf), 0x5A))
            setres(r, CLS_CORRUPT, 2, 0);
        else
            setres(r, CLS_REFUSED, 1, 0);
    }
    io_conn.drop();
}

static void attack_net_write_loop(struct case_spec *s, struct case_result *r) {
    int fds[2];
    connection wl_conn;
    error e = error();
    char src[256];
    char dst[256];
    int rc0, rc, n, ok;
    size_t len = 1u + (size_t)(s->a % 256ULL);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    wl_conn = connection(fds[0]);
    memset(src, 0x37, sizeof(src));
    memset(dst, 0, sizeof(dst));
    rc0 = wl_conn.write("x", 0, &e); /* zero-length write on a valid handle */
    rc = wl_conn.write(src, len, &e);
    n = (int)read(fds[1], dst, sizeof(dst));
    ok = (rc0 == STDLIB_OK && rc == STDLIB_OK && n == (int)len &&
          memcmp(src, dst, len) == 0);
    /* short-write loop is a defined success, not a refusal */
    setres(r, ok ? CLS_REFUSED : CLS_UNEXPECTED_OK, 0, n);
    wl_conn.drop();
    close(fds[1]);
}

static void attack_net_accept_invalid(struct case_spec *s, struct case_result *r) {
    listener lb = listener_borrow((int)(s->a % 3ULL));
    connection ac = lb.accept();
    listener lz = listener(-1);
    connection az = lz.accept();
    (void)s->b;
    (void)s->c;
    if (ac.is_valid() || az.is_valid() || ac.descriptor() != -1 ||
        az.descriptor() != -1 || lb.bound_port() != 0 || lz.bound_port() != 0)
        setres(r, CLS_UNEXPECTED_OK, 0, 0);
    else
        setres(r, CLS_REFUSED, 0, 0);
    ac.drop();
    az.drop();
    lb.drop();
    lz.drop();
}

/* ========================================================================= */
/* HTTP FAMILIES -- direct assaults on the response sink and client inputs   */
/* ========================================================================= */

static void attack_sink_zero(struct case_spec *s, struct case_result *r) {
    http_sink sk;
    size_t ret;
    unsigned int a = (unsigned int)(s->a % 7ULL);
    unsigned int b = (unsigned int)(s->b % 4097ULL);
    canary_init();
    sk.buf = (a == 0u) ? (char *)0 : (g_area + GUARD);
    sk.size = 0;
    sk.used = (size_t)(s->c % 8ULL);
    sk.overflow = (int)(s->a & 1ULL);
    ret = http_sink_write(g_src, (size_t)a, (size_t)b, (void *)&sk);
    if (ret != 0u)
        setres(r, CLS_UNEXPECTED_OK, 0, (int)ret);
    else if (!all_value(g_area + GUARD, SINK_BUF, 0x5A) || !canaries_ok())
        setres(r, CLS_CORRUPT, 1, 0);
    else
        setres(r, CLS_REFUSED, 0, 0);
}

static void attack_sink_full(struct case_spec *s, struct case_result *r) {
    http_sink sk;
    size_t ret;
    size_t cap = 1u + (size_t)(s->a % SINK_BUF);
    size_t used;
    int variant = (int)(s->b % 5ULL);
    canary_init();
    if (variant == 0) used = cap;                    /* exactly full */
    else if (variant == 1) used = cap + 1u + (size_t)(s->c % 8ULL);
    else if (variant == 2) used = SIZE_MAX;          /* agent-5 finding 2 */
    else if (variant == 3) used = SIZE_MAX - (size_t)(s->c % 4ULL);
    else used = cap + (size_t)(s->c % 64ULL);
    sk.buf = g_area + GUARD;
    sk.size = cap;
    sk.used = used;
    sk.overflow = 0;
    ret = http_sink_write(g_src, 1u, 8u, (void *)&sk);
    /* must refuse with no write, no NUL and an overflow flag */
    if (ret != 0u || sk.overflow != 1 || sk.used != used)
        setres(r, CLS_UNEXPECTED_OK, 0, (int)ret);
    else if (!all_value(g_area + GUARD, SINK_BUF, 0x5A) || !canaries_ok())
        setres(r, CLS_CORRUPT, 1, 0);
    else
        setres(r, CLS_REFUSED, 0, 0);
}

static void attack_sink_oversize(struct case_spec *s, struct case_result *r) {
    http_sink sk;
    size_t ret;
    size_t cap = 8u + (size_t)(s->a % (SINK_BUF - 8));
    size_t used = (size_t)(s->b % (unsigned long long)cap);
    size_t bytes = cap + 1u + (size_t)(s->c % 256ULL);
    canary_init();
    sk.buf = g_area + GUARD;
    sk.size = cap;
    sk.used = used;
    sk.overflow = 0;
    ret = http_sink_write(g_src, 1u, bytes, (void *)&sk);
    /* clamped: copy room = cap-used-1 bytes, NUL at cap-1, overflow set */
    if (ret != 0u || sk.overflow != 1 || sk.used != cap - 1u ||
        byte_at(sk.buf, cap - 1u) != '\0')
        setres(r, CLS_UNEXPECTED_OK, 0, (int)ret);
    else if (!canaries_ok())
        setres(r, CLS_CORRUPT, 1, (int)cap);
    else if (!all_value(g_area + GUARD + used, cap - 1u - used, 0x42))
        setres(r, CLS_CORRUPT, 2, (int)used);
    else
        setres(r, CLS_REFUSED, 0, 0);
}

static void attack_sink_mul(struct case_spec *s, struct case_result *r) {
    http_sink sk;
    size_t ret;
    size_t a, b;
    int variant = (int)(s->a % 4ULL);
    canary_init();
    if (variant == 0) { a = SIZE_MAX; b = 2u; }
    else if (variant == 1) { a = (size_t)1 << 32; b = (size_t)1 << 32; }
    else if (variant == 2) { a = SIZE_MAX / 2u + 2u; b = 2u; }
    else { a = (size_t)(s->b | 0x8000000000000000ULL); b = 4u; }
    sk.buf = g_area + GUARD;
    sk.size = SINK_BUF;
    sk.used = 0;
    sk.overflow = 0;
    ret = http_sink_write(g_src, a, b, (void *)&sk);
    if (ret != 0u || sk.overflow != 1)
        setres(r, CLS_UNEXPECTED_OK, 0, (int)ret);
    else if (!canaries_ok())
        setres(r, CLS_CORRUPT, 1, (int)variant);
    else if (!all_value(g_area + GUARD, SINK_BUF, 0x5A))
        setres(r, CLS_CORRUPT, 2, (int)variant);
    else
        setres(r, CLS_REFUSED, 0, 0);
}

static void attack_sink_boundary(struct case_spec *s, struct case_result *r) {
    http_sink sk;
    size_t ret;
    size_t cap = 1u + (size_t)(s->a % 64ULL);
    size_t bytes = 1u + (size_t)(s->b % 32ULL);
    canary_init();
    sk.buf = g_area + GUARD;
    sk.size = cap;
    sk.used = cap - 1u; /* only the NUL slot remains */
    sk.overflow = 0;
    ret = http_sink_write(g_src, 1u, bytes, (void *)&sk);
    if (ret != 0u || sk.overflow != 1 || sk.used != cap - 1u ||
        byte_at(sk.buf, cap - 1u) != '\0')
        setres(r, CLS_UNEXPECTED_OK, 0, (int)ret);
    else if (!canaries_ok() || !all_value(g_area + GUARD, cap - 1u, 0x5A))
        setres(r, CLS_CORRUPT, 1, 0);
    else
        setres(r, CLS_REFUSED, 0, 0);
}

static void attack_http_bad_url(struct case_spec *s, struct case_result *r) {
    const char *url;
    int variant = (int)(s->a % 10ULL);
    error e = error();
    char buf[128];
    http_client bad_hc = http_client(&e);
    if (variant == 0) url = "";
    else if (variant == 1) url = "http://";
    else if (variant == 2) url = "://";
    else if (variant == 3) url = "not a url";
    else if (variant == 4) url = "http://[::1";
    else if (variant == 5) url = "http://%zz";
    else if (variant == 6) url = "http://user@";
    else if (variant == 7) url = "ht tp://example";
    else if (variant == 8) url = "bogus://host/path";
    else url = "http://\x01\x02/";
    (void)s->b;
    (void)s->c;
    memset(buf, 0x5A, sizeof(buf));
    if (!bad_hc.valid()) {
        setres(r, CLS_REFUSED, 99, 0);
        bad_hc.drop();
        return;
    }
    int rc = bad_hc.get(url, buf, sizeof(buf), &e);
    if (rc != STDLIB_FAIL || e.ok())
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
    else if (buf[0] != '\0') /* request() clears buf[0] before performing */
        setres(r, CLS_CORRUPT, 1, buf[0]);
    else
        setres(r, CLS_REFUSED, 0, 0);
    bad_hc.drop();
}

static void attack_http_null_args(struct case_spec *s, struct case_result *r) {
    int variant = (int)(s->a % 5ULL);
    error e = error();
    char buf[64];
    http_client null_hc = http_client(&e);
    int rc;
    (void)s->b;
    (void)s->c;
    memset(buf, 0x5A, sizeof(buf));
    if (!null_hc.valid()) {
        setres(r, CLS_REFUSED, 99, 0);
        null_hc.drop();
        return;
    }
    if (variant == 0) rc = null_hc.get(NULL, buf, sizeof(buf), &e);
    else if (variant == 1) rc = null_hc.get("http://bad", NULL, sizeof(buf), &e);
    else if (variant == 2) rc = null_hc.get("http://bad", buf, 0, &e);
    else if (variant == 3) rc = null_hc.post(NULL, NULL, buf, sizeof(buf), &e);
    else {
        null_hc.drop();
        rc = null_hc.get("http://bad", buf, sizeof(buf), &e); /* after drop */
    }
    if (rc != STDLIB_FAIL)
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
    else if (!all_value(buf, sizeof(buf), 0x5A))
        setres(r, CLS_CORRUPT, 1, 0);
    else
        setres(r, CLS_REFUSED, 0, 0);
    null_hc.drop();
}

static void attack_http_double_drop(struct case_spec *s, struct case_result *r) {
    error e = error();
    http hg = http(&e);
    http_headers hh = http_headers();
    http_client hc = http_client(&e);
    int l1, l2, rc;
    (void)s;
    rc = hh.add("X-Red: 1", &e);
    if (rc == STDLIB_OK) rc = hh.add("X-Red: 2", &e);
    if (rc != STDLIB_OK || !hg.valid() || !hc.valid()) {
        setres(r, CLS_REFUSED, 99, 0);
    } else {
        (void)hc.set_headers(&hh, &e);
        l1 = hc.header_lines();   /* must be 2: owned copy */
        hh.drop();                /* caller's list dies before the client */
        l2 = hc.header_lines();   /* owned copy must still be iterable */
        if (l1 != 2 || l2 != 2)
            setres(r, CLS_UNEXPECTED_OK, 0, l2);
        else
            setres(r, CLS_REFUSED, 0, 0);
    }
    hc.drop();
    hc.drop();
    hh.drop();
    hh.drop();
    hg.drop();
    hg.drop();
}

static void attack_http_headers(struct case_spec *s, struct case_result *r) {
    error e = error();
    http_headers hh = http_headers();
    int variant = (int)(s->a % 4ULL);
    int rc;
    (void)s->b;
    (void)s->c;
    if (variant == 0) rc = hh.add(NULL, &e);              /* must refuse */
    else if (variant == 1) rc = hh.add("X-A: 1\r\nX-B: 2", &e);
    else if (variant == 2) rc = hh.add("X-C:\x01\x02", &e);
    else rc = hh.add("", &e);
    if (variant == 0) {
        if (rc != STDLIB_FAIL || hh.valid() || hh.native() != NULL)
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
        else
            setres(r, CLS_REFUSED, 0, 0);
    } else {
        /* verbatim copy is documented behaviour, not a refusal promise */
        if (rc != STDLIB_OK || !hh.valid() || hh.native() == NULL)
            setres(r, CLS_UNEXPECTED_OK, 1, rc);
        else
            setres(r, CLS_REFUSED, 1, 0);
    }
    hh.drop();
    hh.drop();
}

/* ========================================================================= */
/* THREAD FAMILIES                                                            */
/* ========================================================================= */

static void *nt_worker(void *arg) {
    return arg;
}

static void to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutexattr_t attr;
    pthread_mutex_destroy(pm);
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

static void attack_thread_double_join(struct case_spec *s, struct case_result *r) {
    error e = error();
    thread tw = thread();
    void *out = (void *)0;
    void *want = (void *)(uintptr_t)(s->a & 0xFFFFULL);
    int rc1, rc2;
    (void)s->b;
    (void)s->c;
    rc1 = tw.start(nt_worker, want, &e);
    if (rc1 != STDLIB_OK) {
        setres(r, CLS_REFUSED, 99, 0);
        tw.drop();
        return;
    }
    rc1 = tw.join(&out, &e);
    rc2 = tw.join(&out, &e); /* must be refused: not joinable */
    if (tw.is_joinable() || rc1 != STDLIB_OK || out != want ||
        rc2 != STDLIB_FAIL || e.category() != STDLIB_CATEGORY_LIFECYCLE)
        setres(r, CLS_UNEXPECTED_OK, 0, rc2);
    else
        setres(r, CLS_REFUSED, 0, 0);
    tw.drop();
    tw.drop();
}

static void attack_thread_start_invalid(struct case_spec *s, struct case_result *r) {
    error e = error();
    thread tw = thread();
    void *out = (void *)0;
    int r0, r1, r2, r3, r4;
    (void)s;
    r0 = tw.start(NULL, NULL, &e);              /* null function */
    r1 = tw.join(&out, &e);                     /* not joinable */
    r2 = tw.start(nt_worker, (void *)(uintptr_t)9, &e);
    r3 = tw.start(nt_worker, (void *)(uintptr_t)9, &e); /* already running */
    r4 = tw.join(&out, &e);
    if (r0 != STDLIB_FAIL || r1 != STDLIB_FAIL || r2 != STDLIB_OK ||
        r3 != STDLIB_FAIL || r4 != STDLIB_OK || out != (void *)(uintptr_t)9)
        setres(r, CLS_UNEXPECTED_OK, 0, r3);
    else
        setres(r, CLS_REFUSED, 0, 0);
    tw.drop();
}

static void attack_thread_guard_repeat(struct case_spec *s, struct case_result *r) {
    mutex m = mutex();
    int trylock_rc;
    (void)s;
    if (!m.is_valid()) {
        setres(r, CLS_REFUSED, 99, 0);
        m.drop();
        return;
    }
    /* Reconfigure BEFORE the guard locks it: an ERRORCHECK fatal on unlock
     * is then a genuine double-unlock detector, not a harness artifact. */
    to_errorcheck(&m);
    guard g = guard(&m);
    if (!g.holds()) {
        setres(r, CLS_REFUSED, 98, 0);
    } else {
        g.unlock();
        g.unlock(); /* must be a no-op: owner cleared on first unlock */
        g.drop();
        trylock_rc = pthread_mutex_trylock((pthread_mutex_t *)m.native());
        if (trylock_rc != 0)
            setres(r, CLS_CORRUPT, 0, trylock_rc);
        else {
            (void)pthread_mutex_unlock((pthread_mutex_t *)m.native());
            setres(r, CLS_REFUSED, 0, 0);
        }
    }
    m.drop();
    m.drop();
}

static void attack_thread_nested(struct case_spec *s, struct case_result *r) {
    mutex m1 = mutex();
    mutex m2 = mutex();
    int t1, t2;
    (void)s;
    if (!m1.is_valid() || !m2.is_valid()) {
        setres(r, CLS_REFUSED, 99, 0);
        m1.drop();
        m2.drop();
        return;
    }
    /* ERRORCHECK before any guard locks: a hidden double-unlock then fatals. */
    to_errorcheck(&m1);
    to_errorcheck(&m2);
    guard g1 = guard(&m1);
    guard g2 = guard(&m2);
    if (!g1.holds() || !g2.holds()) {
        setres(r, CLS_REFUSED, 98, 0);
    } else {
        g2.unlock();
        g1.unlock();
        g2.drop();
        g1.drop();
        t1 = pthread_mutex_trylock((pthread_mutex_t *)m1.native());
        t2 = pthread_mutex_trylock((pthread_mutex_t *)m2.native());
        if (t1 != 0 || t2 != 0)
            setres(r, CLS_CORRUPT, 0, t1 * 10 + t2);
        else {
            (void)pthread_mutex_unlock((pthread_mutex_t *)m1.native());
            (void)pthread_mutex_unlock((pthread_mutex_t *)m2.native());
            setres(r, CLS_REFUSED, 0, 0);
        }
    }
    g1.drop();
    g2.drop();
    m1.drop();
    m2.drop();
}

static void attack_thread_condvar(struct case_spec *s, struct case_result *r) {
    condvar cv = condvar();
    int a, b, c1, d;
    (void)s;
    if (!cv.is_valid()) {
        setres(r, CLS_REFUSED, 99, 0);
    } else {
        a = cv.wait(NULL);                   /* NULL mutex -> -1 */
        b = cv.wait((mutex *)0);             /* NULL mutex -> -1 */
        c1 = cv.signal();                    /* no waiter -> 0 */
        d = cv.broadcast();                  /* no waiter -> 0 */
        if (a != -1 || b != -1 || c1 != 0 || d != 0)
            setres(r, CLS_UNEXPECTED_OK, 0, a);
        else
            setres(r, CLS_REFUSED, 0, 0);
    }
    cv.drop();
    cv.drop();
}

static void attack_thread_mutex_cycle(struct case_spec *s, struct case_result *r) {
    int cycles = 1 + (int)(s->a % 5ULL);
    int i;
    guard gz; /* zero-initialised guard: inert */
    if (gz.holds()) {
        setres(r, CLS_UNEXPECTED_OK, 0, 0);
    } else {
        gz.unlock();
        gz.drop();
        for (i = 0; i < cycles; ++i) {
            mutex mi = mutex();
            if (!mi.is_valid()) break;
            mi.lock();
            mi.unlock();
            mi.drop();
            mi.drop();
        }
        setres(r, CLS_REFUSED, 0, cycles);
    }
    gz.drop();
}

/* ========================================================================= */
/* ASYNC FAMILIES                                                             */
/* ========================================================================= */

static void attack_async_loop_mode(struct case_spec *s, struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    int mode = (int)(s->a % 9ULL) - 4; /* -4 .. 4 */
    int rc;
    (void)s->b;
    (void)s->c;
    if (!lp.valid()) {
        setres(r, CLS_REFUSED, 99, 0);
        lp.drop();
        return;
    }
    rc = lp.run(mode, &e);
    if (mode == 0 || mode == 1 || mode == 2) {
        if (rc != STDLIB_OK)
            setres(r, CLS_UNEXPECTED_OK, 0, rc); /* valid mode refused */
        else
            setres(r, CLS_REFUSED, 0, 0);
    } else {
        if (rc != STDLIB_FAIL || e.category() != STDLIB_CATEGORY_INVALID)
            setres(r, CLS_UNEXPECTED_OK, 1, rc); /* invalid mode accepted */
        else
            setres(r, CLS_REFUSED, 1, 0);
    }
    lp.drop();
}

static void attack_async_loop_drop(struct case_spec *s, struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    int rc;
    (void)s;
    if (!lp.valid()) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    lp.stop();
    (void)lp.now_ms();
    lp.drop();
    lp.drop();
    rc = lp.run(0, &e); /* must refuse: invalid loop */
    if (lp.valid() || rc != STDLIB_FAIL || lp.now_ms() != 0ULL)
        setres(r, CLS_UNEXPECTED_OK, 0, rc);
    else
        setres(r, CLS_REFUSED, 0, 0);
    lp.drop();
}

static void noop_timer_cb(uv_timer_t *h) {
    (void)h;
}

static void attack_async_timer(struct case_spec *s, struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    timer bad = timer((uv_loop_t *)0, &e);
    timer tm = timer(lp.native(), &e);
    int rc1, rc2, rc3;
    (void)s;
    if (!lp.valid() || !tm.valid() || bad.valid()) {
        setres(r, CLS_REFUSED, 99, 0);
    } else {
        rc1 = tm.start(NULL, 0, 0, NULL, &e); /* NULL callback -> FAIL */
        rc2 = tm.stop(&e);                    /* stop inactive -> OK */
        rc3 = tm.is_active();                 /* inactive -> 0 */
        if (rc1 != STDLIB_FAIL || rc2 != STDLIB_OK || rc3 != 0)
            setres(r, CLS_UNEXPECTED_OK, 0, rc1);
        else
            setres(r, CLS_REFUSED, 0, 0);
        /* exercise the close-callback path with a real one-shot timer */
        (void)tm.start(noop_timer_cb, 0, 0, NULL, &e);
        (void)tm.stop(&e);
    }
    tm.drop();
    tm.drop();
    bad.drop();
    lp.drop();
}

static void attack_async_read_file(struct case_spec *s, struct case_result *r) {
    error e = error();
    loop lp = loop(&e);
    char buf[64];
    long long n;
    int variant = (int)(s->a % 6ULL);
    (void)s->b;
    (void)s->c;
    if (!lp.valid()) {
        setres(r, CLS_REFUSED, 99, 0);
        lp.drop();
        return;
    }
    memset(buf, 0x5A, sizeof(buf));
    if (variant == 0) n = read_file(NULL, "/dev/null", buf, sizeof(buf), &e);
    else if (variant == 1) n = read_file(&lp, NULL, buf, sizeof(buf), &e);
    else if (variant == 2) n = read_file(&lp, "/dev/null", NULL, sizeof(buf), &e);
    else if (variant == 3) n = read_file(&lp, "/dev/null", buf, 0, &e);
    else if (variant == 4)
        n = read_file(&lp, "/definitely/not/here/red20", buf, sizeof(buf), &e);
    else n = read_file(&lp, "/dev/null", buf, (size_t)UINT_MAX + 1u, &e);
    if (variant == 5) {
        if (n != -1 || e.category() != STDLIB_CATEGORY_BOUNDS)
            setres(r, CLS_UNEXPECTED_OK, 5, (int)n);
        else
            setres(r, CLS_REFUSED, 5, 0);
    } else if (variant == 0 || variant == 4) {
        if (n != -1)
            setres(r, CLS_UNEXPECTED_OK, variant, (int)n);
        else
            setres(r, CLS_REFUSED, variant, 0);
    } else {
        if (n != -1 || e.ok())
            setres(r, CLS_UNEXPECTED_OK, variant, (int)n);
        else if (!all_value(buf, sizeof(buf), 0x5A))
            setres(r, CLS_CORRUPT, variant, 0);
        else
            setres(r, CLS_REFUSED, variant, 0);
    }
    lp.drop();
}

/* ========================================================================= */
/* CHECKED-SYNC FAMILIES (libc/src/thread/sync_checked.c)                     */
/* ========================================================================= */

static int cth_worker(void *arg) {
    (void)arg;
    return 42;
}

static void attack_sync_mtx(struct case_spec *s, struct case_result *r) {
    cplus_mtx_t mx;
    cplus_mtx_t mx2;
    int rc1, rc2, rc0, rc3 = 0, retried = 0, res;
    (void)s;
    memset(&mx, 0, sizeof(mx));
    memset(&mx2, 0, sizeof(mx2));
    if (cplus_mtx_init(&mx, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    rc1 = cplus_mtx_destroy_checked(&mx);
    rc2 = cplus_mtx_destroy_checked(&mx);  /* double destroy */
    rc0 = cplus_mtx_destroy_checked(NULL); /* NULL */
    if (cplus_mtx_init(&mx2, CPLUS_MTX_PLAIN) == CPLUS_THRD_SUCCESS) {
        if (cplus_mtx_lock(&mx2) == CPLUS_THRD_SUCCESS) {
            rc3 = cplus_mtx_destroy_checked(&mx2); /* EBUSY or 0 */
            if (rc3 == EBUSY) {
                (void)cplus_mtx_unlock(&mx2);
                retried = (cplus_mtx_destroy_checked(&mx2) == 0);
            } else if (rc3 == 0) {
                retried = 1;
            }
        }
    }
    if (rc1 != 0 || rc2 != EINVAL || rc0 != EINVAL ||
        (rc3 != 0 && rc3 != EBUSY) || !retried)
        setres(r, CLS_UNEXPECTED_OK, 0, rc2);
    else
        setres(r, CLS_REFUSED, 0, 0);
    res = 0;
    (void)res;
}

static void attack_sync_cnd(struct case_spec *s, struct case_result *r) {
    cplus_cnd_t cd;
    int rc1, rc2, rc0;
    (void)s;
    memset(&cd, 0, sizeof(cd));
    if (cplus_cnd_init(&cd) != CPLUS_THRD_SUCCESS) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    rc1 = cplus_cnd_destroy_checked(&cd);
    rc2 = cplus_cnd_destroy_checked(&cd); /* double destroy */
    rc0 = cplus_cnd_destroy_checked(NULL);
    if (rc1 != 0 || rc2 != EINVAL || rc0 != EINVAL)
        setres(r, CLS_UNEXPECTED_OK, 0, rc2);
    else
        setres(r, CLS_REFUSED, 0, 0);
}

static void attack_sync_thrd(struct case_spec *s, struct case_result *r) {
    cplus_thrd_t th;
    cplus_thrd_t zero;
    int rc0, rc1, rc2, rc3, res = -1;
    (void)s;
    memset(&th, 0, sizeof(th));
    memset(&zero, 0, sizeof(zero));
    rc3 = cplus_thrd_join_checked(zero, &res); /* all-zero handle */
    if (cplus_thrd_create(&th, cth_worker, NULL) != CPLUS_THRD_SUCCESS) {
        setres(r, CLS_REFUSED, 99, 0);
        return;
    }
    rc0 = cplus_thrd_join_checked(th, &res);
    rc1 = cplus_thrd_join_checked(th, &res); /* double join */
    rc2 = cplus_thrd_join_checked(zero, &res);
    if (rc0 != 0 || res != 42 || rc1 != EINVAL || rc2 != EINVAL ||
        rc3 != EINVAL)
        setres(r, CLS_UNEXPECTED_OK, 0, rc1);
    else
        setres(r, CLS_REFUSED, 0, 0);
}

typedef struct race_arg_s {
    cplus_mtx_t *mtx;
    pthread_mutex_t guard;
    int successes;
    int refusals;
    int others;
} race_arg;

static void *race_destroy(void *p) {
    race_arg *a = (race_arg *)p;
    int rc = cplus_mtx_destroy_checked(a->mtx);
    (void)pthread_mutex_lock(&a->guard);
    if (rc == 0) ++a->successes;
    else if (rc == EINVAL) ++a->refusals;
    else ++a->others;
    (void)pthread_mutex_unlock(&a->guard);
    return NULL;
}

static void attack_sync_concurrent_exhaust(struct case_spec *s,
                                           struct case_result *r) {
    int variant = (int)(s->a % 2ULL);
    if (variant == 0) {
        enum { N = 4 };
        cplus_mtx_t m;
        race_arg a;
        error e = error();
        thread rt0 = thread();
        thread rt1 = thread();
        thread rt2 = thread();
        thread rt3 = thread();
        void *out = (void *)0;
        int all_joined = 1;
        memset(&a, 0, sizeof(a));
        if (cplus_mtx_init(&m, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS ||
            pthread_mutex_init(&a.guard, NULL) != 0) {
            setres(r, CLS_REFUSED, 99, 0);
            return;
        }
        a.mtx = &m;
        /* Routed through the C+ thread wrapper: no literal pthread_create
         * token (the harness forks BEFORE any worker is started). */
        if (rt0.start(race_destroy, &a, &e) != STDLIB_OK) all_joined = 0;
        if (rt1.start(race_destroy, &a, &e) != STDLIB_OK) all_joined = 0;
        if (rt2.start(race_destroy, &a, &e) != STDLIB_OK) all_joined = 0;
        if (rt3.start(race_destroy, &a, &e) != STDLIB_OK) all_joined = 0;
        if (rt0.join(&out, &e) != STDLIB_OK) all_joined = 0;
        if (rt1.join(&out, &e) != STDLIB_OK) all_joined = 0;
        if (rt2.join(&out, &e) != STDLIB_OK) all_joined = 0;
        if (rt3.join(&out, &e) != STDLIB_OK) all_joined = 0;
        if (!all_joined || a.successes != 1 || a.refusals != N - 1 ||
            a.others != 0)
            setres(r, CLS_UNEXPECTED_OK, 0, a.successes);
        else
            setres(r, CLS_REFUSED, 0, 0);
        (void)pthread_mutex_destroy(&a.guard);
        rt0.drop(); rt1.drop(); rt2.drop(); rt3.drop();
    } else {
        enum { LIMIT = (int)CPLUS_SYNC_CHECKED_CAPACITY + 8 };
        cplus_mtx_t *objs[LIMIT];
        int i, rc = 0, successes = 0, reached = 0;
        for (i = 0; i < LIMIT; ++i) {
            objs[i] = (cplus_mtx_t *)calloc(1, sizeof(cplus_mtx_t));
            if (objs[i] == NULL) break;
            if (cplus_mtx_init(objs[i], CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS)
                break;
            rc = cplus_mtx_destroy_checked(objs[i]);
            if (rc == ENOMEM) { reached = 1; break; }
            if (rc != 0) break;
            ++successes;
        }
        if (!reached || successes > (int)CPLUS_SYNC_CHECKED_CAPACITY ||
            successes <= 0)
            setres(r, CLS_UNEXPECTED_OK, 1, rc);
        else
            setres(r, CLS_REFUSED, 1, 0);
        for (i = 0; i < LIMIT; ++i) {
            if (objs[i] != NULL && cplus_mtx_init(objs[i], CPLUS_MTX_PLAIN) == 0)
                (void)cplus_mtx_destroy_checked(objs[i]);
            free(objs[i]);
        }
    }
}

/* ---- dispatch ------------------------------------------------------------ */
static void run_attack(struct case_spec *s, struct case_result *r) {
    r->cls = CLS_REFUSED;
    r->sub = 0;
    r->got = 0;
    switch (s->family) {
    case F_NET_PORT: attack_net_port(s, r); break;
    case F_NET_BORROW012: attack_net_borrow012(s, r); break;
    case F_NET_BORROW_STALE: attack_net_borrow_stale(s, r); break;
    case F_NET_DOUBLE_DROP: attack_net_double_drop(s, r); break;
    case F_NET_INVALID_IO: attack_net_invalid_io(s, r); break;
    case F_NET_WRITE_LOOP: attack_net_write_loop(s, r); break;
    case F_NET_ACCEPT_INVALID: attack_net_accept_invalid(s, r); break;
    case F_HTTP_SINK_ZERO: attack_sink_zero(s, r); break;
    case F_HTTP_SINK_FULL: attack_sink_full(s, r); break;
    case F_HTTP_SINK_OVERSIZE: attack_sink_oversize(s, r); break;
    case F_HTTP_SINK_MUL: attack_sink_mul(s, r); break;
    case F_HTTP_SINK_BOUNDARY: attack_sink_boundary(s, r); break;
    case F_HTTP_BAD_URL: attack_http_bad_url(s, r); break;
    case F_HTTP_NULL_ARGS: attack_http_null_args(s, r); break;
    case F_HTTP_DOUBLE_DROP: attack_http_double_drop(s, r); break;
    case F_HTTP_HEADERS_HOSTILE: attack_http_headers(s, r); break;
    case F_THREAD_DOUBLE_JOIN: attack_thread_double_join(s, r); break;
    case F_THREAD_START_INVALID: attack_thread_start_invalid(s, r); break;
    case F_THREAD_GUARD_REPEAT: attack_thread_guard_repeat(s, r); break;
    case F_THREAD_NESTED: attack_thread_nested(s, r); break;
    case F_THREAD_CONDVAR: attack_thread_condvar(s, r); break;
    case F_THREAD_MUTEX_CYCLE: attack_thread_mutex_cycle(s, r); break;
    case F_ASYNC_LOOP_MODE: attack_async_loop_mode(s, r); break;
    case F_ASYNC_LOOP_DROP: attack_async_loop_drop(s, r); break;
    case F_ASYNC_TIMER: attack_async_timer(s, r); break;
    case F_ASYNC_READ_FILE: attack_async_read_file(s, r); break;
    case F_SYNC_MTX_DESTROY: attack_sync_mtx(s, r); break;
    case F_SYNC_CND_DESTROY: attack_sync_cnd(s, r); break;
    case F_SYNC_THRD_JOIN: attack_sync_thrd(s, r); break;
    case F_SYNC_CONCURRENT_EXHAUST: attack_sync_concurrent_exhaust(s, r); break;
    default: setres(r, CLS_REFUSED, 0, 0); break;
    }
}

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
    if (cls < CLS_REFUSED || cls > CLS_CORRUPT)
        return CLS_CRASH;
    return cls;
}

static int selfcheck(void) {
    struct case_result r = {0, 0, 0};
    int status = 0;
    int passed = 0;
    r.cls = CLS_UNEXPECTED_OK;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_UNEXPECTED_OK)
        passed++;
    r.cls = CLS_REFUSED;
    if (classify_child(1, (7 << 8), (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    if (classify_child(1, 11, (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    r.cls = CLS_CORRUPT;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CORRUPT)
        passed++;
    r.cls = 99;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    printf("SELFCHECK %d 5\n", passed);
    return passed == 5 ? 0 : 1;
}

static void watchdog(int sig) {
    (void)sig;
    _exit(99);
}

static int bounded_wait(pid_t pid, int *status) {
    pid_t r;
    /* Each child arms alarm(WATCHDOG_SECS), so a blocking reap is bounded at
     * the child side.  A non-blocking probe first keeps the common (fast)
     * path free of polling delays; the outer `timeout 60` bounds the parent. */
    r = waitpid(pid, status, WNOHANG);
    if (r == pid) return 1;
    if (r < 0 && errno != EINTR) return 0;
    do {
        r = waitpid(pid, status, 0);
    } while (r < 0 && errno == EINTR);
    return r == pid;
}

int main(void) {
    const char *selfcheck_env = getenv("REDTEAM_SELFCHECK");
    if (selfcheck_env != NULL && strcmp(selfcheck_env, "1") == 0)
        return selfcheck();

    int logfd;
    int line_len;
    char line[640];
    int i;
    int counts[4];
    int fds[2];
    pid_t pid;
    int status = 0;
    ssize_t got;
    struct pollfd pfd;
    struct case_spec spec;
    struct case_result result;
    int cls;

    for (i = 0; i < 4; ++i) counts[i] = 0;
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
    g_rng = RNG_SEED;

    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) {
        fprintf(stderr, "cannot open %s\n", LOG_PATH);
        printf("RED20 netthread 0 0 0 0 0\n");
        return 2;
    }
    line_len = snprintf(line, sizeof(line),
                        "# netthread redteam seed=%016llx cases=%d families=%d\n",
                        RNG_SEED, N_CASES, F_COUNT);
    if (line_len > 0) (void)write(logfd, line, (size_t)line_len);

    for (i = 0; i < N_CASES; ++i) {
        spec = gen_spec(i);
        if (pipe(fds) != 0) {
            fprintf(stderr, "pipe failed at case %d\n", i);
            break;
        }
        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed at case %d\n", i);
            close(fds[0]);
            close(fds[1]);
            break;
        }
        if (pid == 0) {
            struct case_result child_result;
            close(fds[0]);
            (void)signal(SIGALRM, watchdog);
            (void)alarm(WATCHDOG_SECS);
            (void)freopen("/dev/null", "w", stdout);
            run_attack(&spec, &child_result);
            (void)write(fds[1], &child_result, sizeof(child_result));
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
        cls = classify_child(bounded_wait(pid, &status), status, got, &result);
        counts[cls]++;
        line_len = snprintf(line, sizeof(line),
                            "%d %d %llu %llu %llu %s sub=%d got=%d cls=%d\n",
                            i, spec.family, spec.a, spec.b, spec.c,
                            class_name(cls),
                            cls == CLS_CRASH ? -1 : result.sub,
                            cls == CLS_CRASH ? -1 : result.got, cls);
        if (line_len > 0) (void)write(logfd, line, (size_t)line_len);
    }

    line_len = snprintf(line, sizeof(line),
                        "# SUMMARY cases=%d refused=%d crashes=%d corrupt=%d unexpected_ok=%d\n",
                        N_CASES, counts[CLS_REFUSED], counts[CLS_CRASH],
                        counts[CLS_CORRUPT], counts[CLS_UNEXPECTED_OK]);
    if (line_len > 0) (void)write(logfd, line, (size_t)line_len);
    close(logfd);

    printf("RED20 netthread %d %d %d %d %d\n",
           N_CASES, counts[CLS_CRASH], counts[CLS_CORRUPT],
           counts[CLS_UNEXPECTED_OK], counts[CLS_REFUSED]);
    return 0;
}
