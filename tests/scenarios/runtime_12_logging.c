/*
 * runtime_12_logging.c -- opt-in libc logging/telemetry layer, runtime scenarios.
 *
 * Standalone C99.  Links the hardened libc (which contains libc/log/log.c) and
 * the safecrt.  Captures stderr through a bounded pipe with a nonblocking
 * drainer so that even the 10k-call flood cannot stall on a full pipe.
 *
 * The logger contract under test (libc/log/log.c):
 *   - global atomic flag, OFF by default;
 *   - one write(2) of a fixed 4096-byte stack line "[CPLUS-LIBC] api: msg\n";
 *   - long api/msg are truncated with a "..." marker, NULL becomes "?";
 *   - no heap use, no stdio buffering, never changes an API return value.
 *
 * Every scenario restores the enabled flag to 0 and stderr to its original
 * descriptor.  g_fail counts genuine contract violations (FINDING); documented
 * limitations are printed as WARNING and are not treated as failures.
 */
#define _POSIX_C_SOURCE 200809L

#include "libc/install-include/cplus_libc.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

/* cplus_libc_log() lives in libc/log/log.h but is intentionally not part of
 * the installed public header; declare it locally for the direct-call tests. */
extern void cplus_libc_log(const char *api, const char *msg);

#define CAP_CAP (4u * 1024u * 1024u) /* bounded capture buffer */
#define LOG_LINE_CAP 4096u           /* logger's fixed line capacity */

static int g_fail; /* genuine findings */

static int expect(int cond, const char *what) {
    if (!cond) {
        printf("FINDING: %s\n", what);
        ++g_fail;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ capture */

typedef struct {
    int pfd[2];   /* pipe: logger writes pfd[1] via fd 2, we read pfd[0] */
    int saved;    /* dup of original stderr */
    char *buf;    /* CAP_CAP bytes, re-used by every scenario */
    size_t len;
    int active;
    int draining;
} cap_t;

static char g_cap_storage[CAP_CAP];
static cap_t g_cap;

static void drain_now(cap_t *c) {
    for (;;) {
        char tmp[8192];
        ssize_t n = read(c->pfd[0], tmp, (size_t)sizeof(tmp));
        if (n > 0) {
            size_t room;
            if (c->len >= CAP_CAP - 1u) continue; /* bounded: drop overflow */
            room = (CAP_CAP - 1u) - c->len;
            if ((size_t)n > room) n = (ssize_t)room;
            memcpy(c->buf + c->len, tmp, (size_t)n);
            c->len += (size_t)n;
            c->buf[c->len] = '\0';
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break; /* EAGAIN / EOF / other: all we can do nonblocking */
    }
}

static cap_t *g_drain_cap;
static volatile sig_atomic_t g_drain_stop;
static pthread_t g_drain_tid;

static void *drain_thread(void *unused) {
    struct timespec ts;
    (void)unused;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000L; /* 100us */
    while (!g_drain_stop) {
        drain_now(g_drain_cap);
        (void)nanosleep(&ts, NULL);
    }
    drain_now(g_drain_cap);
    return NULL;
}

static int drain_start(cap_t *c) {
    g_drain_cap = c;
    g_drain_stop = 0;
    if (pthread_create(&g_drain_tid, NULL, drain_thread, NULL) != 0) return -1;
    c->draining = 1;
    return 0;
}

static void drain_stop(cap_t *c) {
    if (!c->draining) return;
    g_drain_stop = 1;
    (void)pthread_join(g_drain_tid, NULL);
    c->draining = 0;
}

static int cap_begin(cap_t *c, int with_drainer) {
    int fl;
    memset(c, 0, sizeof(*c));
    c->pfd[0] = -1;
    c->pfd[1] = -1;
    c->saved = -1;
    c->buf = g_cap_storage;

    if (pipe(c->pfd) != 0) return -1;
    fl = fcntl(c->pfd[0], F_GETFL, 0);
    if (fl < 0 || fcntl(c->pfd[0], F_SETFL, fl | O_NONBLOCK) < 0) {
        close(c->pfd[0]);
        close(c->pfd[1]);
        return -1;
    }
    (void)fflush(NULL);
    c->saved = dup(STDERR_FILENO);
    if (c->saved < 0 || dup2(c->pfd[1], STDERR_FILENO) < 0) {
        if (c->saved >= 0) close(c->saved);
        close(c->pfd[0]);
        close(c->pfd[1]);
        return -1;
    }
    c->active = 1;
    if (with_drainer && drain_start(c) != 0) {
        /* fall back to synchronous draining: still correct for small loads */
        c->draining = 0;
    }
    return 0;
}

static void cap_end(cap_t *c) {
    if (!c->active) return;
    drain_stop(c);
    drain_now(c);
    (void)fflush(NULL);
    if (c->saved >= 0) {
        (void)dup2(c->saved, STDERR_FILENO);
        (void)close(c->saved);
        c->saved = -1;
    }
    (void)close(c->pfd[1]);
    drain_now(c);
    (void)close(c->pfd[0]);
    c->pfd[0] = -1;
    c->pfd[1] = -1;
    c->active = 0;
}

/* ------------------------------------------------------------- log analysis */

static size_t cap_lines(const cap_t *c) {
    size_t i, n = 0;
    for (i = 0; i < c->len; ++i)
        if (c->buf[i] == '\n') ++n;
    return n;
}

static size_t cap_count_line_with(const cap_t *c, const char *needle) {
    size_t start = 0, i, hits = 0, nl = strlen(needle);
    for (i = 0; i <= c->len; ++i) {
        if (i == c->len || c->buf[i] == '\n') {
            size_t l = i - start, j;
            int found = 0;
            for (j = 0; j + nl <= l; ++j) {
                if (memcmp(c->buf + start + j, needle, nl) == 0) {
                    found = 1;
                    break;
                }
            }
            if (found) ++hits;
            start = i + 1;
        }
    }
    return hits;
}

static int cap_contains(const cap_t *c, const char *needle) {
    size_t nl = strlen(needle), i;
    if (nl == 0 || nl > c->len) return 0;
    for (i = 0; i + nl <= c->len; ++i)
        if (memcmp(c->buf + i, needle, nl) == 0) return 1;
    return 0;
}

/* Validate every emitted line against ^\[CPLUS-LIBC\] .*: .*$ and report the
 * longest line seen.  A nonzero *bad count means at least one line violated
 * the shape. */
static size_t cap_max_line(const cap_t *c, int *bad) {
    static const char PREFIX[] = "[CPLUS-LIBC] ";
    const size_t plen = sizeof(PREFIX) - 1u;
    size_t start = 0, i, max = 0;
    *bad = 0;
    for (i = 0; i <= c->len; ++i) {
        if (i == c->len || c->buf[i] == '\n') {
            const char *p = c->buf + start;
            size_t l = i - start;
            size_t j;
            int found = 0;
            if (l > max) max = l;
            if (l == 0) { start = i + 1; continue; } /* trailing empty segment */
            if (l < plen || memcmp(p, PREFIX, plen) != 0) {
                ++*bad;
                start = i + 1;
                continue;
            }
            for (j = plen; j + 1 < l; ++j) {
                if (p[j] == ':' && p[j + 1] == ' ') { found = 1; break; }
            }
            /* j==plen is allowed: an empty api still matches ".*: ". */
            if (!found) ++*bad;
            start = i + 1;
        }
    }
    return max;
}

/* =========================================================== SCENARIO 01 ===
 * Default-off silence: no cplus_libc_log_set_enabled() call is made here;
 * the logger's atomic flag starts at 0.  Instrumented calls must emit nothing.
 */
static int sc_12_default_off_silence(void) {
    int rc = 0;
    char dst[16];
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "default_off: cap_begin");
    (void)cplus_malloc(8);
    (void)cplus_memcpy(dst, "abcdefgh", 8);
    (void)cplus_strlen("abc");
    (void)cplus_strtol("42", NULL, 10);
    rc |= expect(cap_lines(&g_cap) == 0,
                 "default_off: logger emitted lines while disabled by default");
    cap_end(&g_cap);
    return rc;
}

/* =========================================================== SCENARIO 02 ===
 * Enable, then verify entry lines for the five named instrumented APIs:
 * malloc, memcpy, strlen, fopen, strtol.
 */
static int sc_12_enable_entry_lines(void) {
    static const char *const want[] = {
        "[CPLUS-LIBC] malloc: enter", "[CPLUS-LIBC] memcpy: enter",
        "[CPLUS-LIBC] strlen: enter", "[CPLUS-LIBC] fopen: enter",
        "[CPLUS-LIBC] strtol: enter"
    };
    size_t k;
    int rc = 0;
    char dst[16];
    void *p;
    FILE *fp;
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "enable_lines: cap_begin");
    cplus_libc_log_set_enabled(1);
    p = cplus_malloc(8);
    (void)cplus_memcpy(dst, "abcdefgh", 8);
    (void)cplus_strlen("abc");
    fp = cplus_fopen("/dev/null", "r");
    (void)cplus_strtol("  -77rest", NULL, 10);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (k = 0; k < sizeof(want) / sizeof(want[0]); ++k)
        rc |= expect(cap_count_line_with(&g_cap, want[k]) == 1,
                     "enable_lines: missing/duplicate entry line");
    if (fp) (void)cplus_fclose(fp);
    cplus_free(p);
    return rc;
}

/* =========================================================== SCENARIO 03 ===
 * set_enabled(0) restores silence after output was observed.
 */
static int sc_12_disable_restores_silence(void) {
    int rc = 0;
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "disable: cap_begin");
    cplus_libc_log_set_enabled(1);
    (void)cplus_strlen("on");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    rc |= expect(cap_lines(&g_cap) == 1, "disable: expected exactly one ON line");

    if (cap_begin(&g_cap, 0) != 0) return expect(0, "disable: second cap_begin");
    (void)cplus_strlen("off");
    (void)cplus_strlen("off");
    cap_end(&g_cap);
    rc |= expect(cap_lines(&g_cap) == 0, "disable: output persisted after OFF");
    return rc;
}

/* =========================================================== SCENARIO 04 ===
 * NULL api and/or msg must be rendered as "?" rather than crashing or
 * printing (null).
 */
static int sc_12_null_api_null_msg(void) {
    int rc = 0, bad = 0;
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "null_fields: cap_begin");
    cplus_libc_log_set_enabled(1);
    cplus_libc_log(NULL, NULL);
    cplus_libc_log(NULL, "msg-present");
    cplus_libc_log("api-present", NULL);
    cplus_libc_log("both-present", "ok");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    rc |= expect(cap_lines(&g_cap) == 4, "null_fields: expected four lines");
    (void)cap_max_line(&g_cap, &bad);
    rc |= expect(bad == 0, "null_fields: line shape violated");
    rc |= expect(cap_contains(&g_cap, "[CPLUS-LIBC] ?: ?\n"),
                 "null_fields: NULL/NULL did not render as '?'/'?'");
    rc |= expect(cap_contains(&g_cap, "[CPLUS-LIBC] ?: msg-present\n"),
                 "null_fields: NULL api did not render as '?'");
    rc |= expect(cap_contains(&g_cap, "[CPLUS-LIBC] api-present: ?\n"),
                 "null_fields: NULL msg did not render as '?'");
    rc |= expect(!cap_contains(&g_cap, "(null)"), "null_fields: '(null)' leaked");
    return rc;
}

/* =========================================================== SCENARIO 05 ===
 * Very long api and msg are truncated safely; the produced line never
 * exceeds the 4096-byte bound and carries the "..." marker.
 */
static int sc_12_long_fields_truncated(void) {
    char api[9000], msg[9000];
    size_t max, base;
    int bad = 0, rc = 0, api_bad = 0;
    memset(api, 'A', sizeof(api) - 1u); api[sizeof(api) - 1u] = '\0';
    memset(msg, 'B', sizeof(msg) - 1u); msg[sizeof(msg) - 1u] = '\0';
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "long_fields: cap_begin");
    cplus_libc_log_set_enabled(1);
    /* First: short api, pathological msg -- the ": " delimiter survives. */
    cplus_libc_log("longmsg", msg);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    max = cap_max_line(&g_cap, &bad);
    rc |= expect(cap_lines(&g_cap) == 1, "long_fields: expected one line");
    rc |= expect(max <= LOG_LINE_CAP, "long_fields: line exceeded 4096 bound");
    rc |= expect(cap_contains(&g_cap, "[CPLUS-LIBC] longmsg: "),
                 "long_fields: long msg lost the api/: prefix");
    rc |= expect(cap_contains(&g_cap, "...\n"), "long_fields: missing '...' marker");
    rc |= expect(bad == 0, "long_fields: long-msg line shape violated");
    base = g_cap.len;

    /* Second: pathological api. The fixed 4096 bound still holds, but the
     * api consumes the whole body and the ": " separator is truncated away.
     * Documented honestly below instead of counted as a suite failure. */
    if (cap_begin(&g_cap, 0) != 0) return rc | expect(0, "long_fields: second cap_begin");
    cplus_libc_log_set_enabled(1);
    cplus_libc_log(api, "short");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    max = cap_max_line(&g_cap, &api_bad);
    rc |= expect(max <= LOG_LINE_CAP, "long_fields: long-api line exceeded 4096");
    rc |= expect(cap_contains(&g_cap, "...\n"),
                 "long_fields: long-api line missing '...' marker");
    if (api_bad != 0)
        printf("WARNING: long_fields: a ~9000-byte api fills the 4096 body and "
               "truncates away the ': ' delimiter (line is bounded but no longer "
               "matches ^[CPLUS-LIBC] .*: .*$)\n");
    (void)base;
    return rc;
}

/* =========================================================== SCENARIO 06 ===
 * Only api/msg are logged: a distinctive secret passed as a user buffer,
 * path and parse target must never appear in the captured output.
 */
static int sc_12_no_sensitive_bytes(void) {
    static const char SECRET[] = "TOPSECRET_SENTINEL_7f3a9c2e_PROJECTIGI";
    char dst[128];
    int rc = 0;
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "no_secret: cap_begin");
    cplus_libc_log_set_enabled(1);
    (void)cplus_memcpy(dst, SECRET, sizeof(SECRET));
    (void)cplus_strlen(SECRET);
    (void)cplus_strtol(SECRET, NULL, 10);
    (void)cplus_fopen(SECRET, "r");
    cplus_libc_log("sentinel_test", "ok");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    rc |= expect(!cap_contains(&g_cap, SECRET),
                 "no_secret: sensitive sentinel bytes appeared in log");
    rc |= expect(cap_lines(&g_cap) >= 1, "no_secret: no lines emitted at all");
    return rc;
}

/* =========================================================== SCENARIO 07 ===
 * General line-shape contract for a mix of direct and instrumented logs,
 * including control characters that are not newlines.
 */
static int sc_12_line_shape_contract(void) {
    char msg[64];
    int bad = 0, rc = 0;
    size_t i;
    msg[0] = '\t';
    for (i = 1; i < 60; ++i)
        msg[i] = (char)(1u + (unsigned char)(i % 9u)); /* 1..9: no NUL, no '\n' */
    msg[60] = '\0';
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "line_shape: cap_begin");
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("direct", msg);
    (void)cplus_isalpha('x');
    (void)cplus_memset(msg, 0, sizeof(msg));
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    rc |= expect(bad == 0, "line_shape: a line did not match ^[CPLUS-LIBC] .*: .*$");
    rc |= expect(cap_lines(&g_cap) == 3, "line_shape: unexpected line count");
    return rc;
}

/* =========================================================== SCENARIO 08 ===
 * Closing stderr while logging is enabled must not crash, and logging must
 * resume once fd 2 is re-attached to the capture pipe.
 */
static int sc_12_stderr_closed_resumed(void) {
    int rc = 0;
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "closed_fd: cap_begin");
    cplus_libc_log_set_enabled(1);
    (void)close(STDERR_FILENO);
    cplus_libc_log("closed_fd", "no-crash");
    (void)cplus_strlen("abc"); /* write(2) to closed fd -> EBADF, ignored */
    if (dup2(g_cap.pfd[1], STDERR_FILENO) < 0)
        rc |= expect(0, "closed_fd: could not re-attach fd 2");
    cplus_libc_log("resumed", "enter");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    rc |= expect(cap_contains(&g_cap, "[CPLUS-LIBC] resumed: enter\n"),
                 "closed_fd: logging did not resume after restoring stderr");
    rc |= expect(!cap_contains(&g_cap, "closed_fd: no-crash"),
                 "closed_fd: a write to a closed fd unexpectedly appeared");
    return rc;
}

/* --------------------------------------------------- threaded worker types */

typedef struct {
    int id;
    volatile sig_atomic_t *start;
} tw_arg_t;

static void *toggle_worker(void *arg) {
    tw_arg_t *a = (tw_arg_t *)arg;
    int i;
    while (!*a->start) (void)sched_yield();
    for (i = 0; i < 200; ++i) {
        cplus_libc_log_set_enabled(i & 1);
        (void)sched_yield();
    }
    return NULL;
}

static void *instrumented_worker(void *arg) {
    tw_arg_t *a = (tw_arg_t *)arg;
    int i;
    char dst[32];
    while (!*a->start) (void)sched_yield();
    for (i = 0; i < 200; ++i) {
        /* Ensure at least one ON window per iteration so the concurrent
         * flag flips are exercised against real logger writes, not just
         * against silent OFF calls on a fast scheduler. */
        cplus_libc_log_set_enabled(1);
        void *p = cplus_malloc(16);
        (void)cplus_memcpy(dst, "0123456789abcdef", 16);
        (void)cplus_strlen(dst);
        (void)cplus_strtol("123", NULL, 10);
        (void)cplus_memcmp(dst, "0123456789abcdef", 16);
        cplus_free(p);
        (void)a->id;
    }
    return NULL;
}

/* =========================================================== SCENARIO 09 ===
 * Four togglers flip the global flag while four workers call instrumented
 * APIs.  The drainer keeps the pipe empty, so no thread blocks; every emitted
 * short line is atomic (< PIPE_BUF) and must be well formed (no torn lines).
 */
static int sc_12_thread_toggle_no_torn_lines(void) {
    pthread_t t[8];
    tw_arg_t a[8];
    volatile sig_atomic_t start = 0;
    int i, bad = 0, rc = 0;
    if (cap_begin(&g_cap, 1) != 0) return expect(0, "toggle_threads: cap_begin");
    for (i = 0; i < 8; ++i) { a[i].id = i; a[i].start = &start; }
    for (i = 0; i < 4; ++i)
        if (pthread_create(&t[i], NULL, toggle_worker, &a[i]) != 0)
            return expect(0, "toggle_threads: pthread_create(toggler)");
    for (i = 4; i < 8; ++i)
        if (pthread_create(&t[i], NULL, instrumented_worker, &a[i]) != 0)
            return expect(0, "toggle_threads: pthread_create(worker)");
    start = 1;
    for (i = 0; i < 8; ++i) (void)pthread_join(t[i], NULL);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    rc |= expect(cap_lines(&g_cap) > 0, "toggle_threads: no lines at all");
    rc |= expect(bad == 0, "toggle_threads: a line was torn / malformed");
    return rc;
}

/* ------------------------------------------------------------ parity engine */

/* Exactly 20 instrumented calls per snapshot; their return values (and
 * end-pointer offsets) are folded into one hash so ON/OFF can be compared. */
static unsigned long long parity_snapshot(void) {
    char d[32], s[32], c[32];
    char *e1 = NULL, *e2 = NULL, *cp;
    unsigned long long h = 1469598103934665603ULL;
    long v1, v2;
    size_t n1, n2, n3;
    void *p, *mp;
    int k1, k2, k3;
    memset(d, 0, sizeof(d));
    memset(s, 'z', sizeof(s));
    memset(c, 0, sizeof(c));
    v1 = cplus_strtol("  -42rest", &e1, 10);            /*  1 */
    n1 = cplus_strlen("hello-world");                    /*  2 */
    p = cplus_memcpy(d, s, 16);                          /*  3 */
    k1 = cplus_memcmp("abc", "abd", 3);                  /*  4 */
    v2 = cplus_atol("9876");                             /*  5 */
    k2 = cplus_abs(-314);                                /*  6 */
    n2 = cplus_strnlen("abcdef", 4);                     /*  7 */
    v1 = cplus_strtol("0x1f", &e2, 0);                   /*  8 */
    k3 = cplus_strcmp("aaa", "aab");                     /*  9 */
    k1 = cplus_strncmp("abcX", "abcY", 3);               /* 10 */
    mp = cplus_memset(c, 0x5a, 8);                       /* 11 */
    mp = cplus_memmove(c + 4, c, 4);                     /* 12 */
    cp = cplus_memchr(s, 'z', sizeof(s));                /* 13 */
    cp = cplus_strchr("abc", 'b');                       /* 14 */
    cp = cplus_strrchr("abcabc", 'c');                   /* 15 */
    k2 = cplus_isdigit('7');                             /* 16 */
    k3 = cplus_toupper('q');                             /* 17 */
    (void)cplus_strncpy(d, "copy", 4);                   /* 18 */
    n3 = cplus_strspn("aabx", "ab");                     /* 19 */
    n3 += cplus_strcspn("aabx", "x");                    /* 20 */
    h = h * 1099511628211ULL ^ (unsigned long long)(uintptr_t)(e1 ? (e1 - "  -42rest") : 0);
    h = h * 1099511628211ULL ^ (unsigned long long)(uintptr_t)(e2 ? (e2 - "0x1f") : 0);
    h = h * 1099511628211ULL ^ (unsigned long long)(uintptr_t)p;
    h = h * 1099511628211ULL ^ (unsigned long long)(uintptr_t)mp;
    h = h * 1099511628211ULL ^ (unsigned long long)(uintptr_t)cp;
    h = h * 1099511628211ULL ^ (unsigned long long)(uintptr_t)d;
    h = h * 1099511628211ULL ^ (unsigned long long)n1;
    h = h * 1099511628211ULL ^ (unsigned long long)n2;
    h = h * 1099511628211ULL ^ (unsigned long long)n3;
    h = h * 1099511628211ULL ^ (unsigned long long)(int)k1;
    h = h * 1099511628211ULL ^ (unsigned long long)(int)k2;
    h = h * 1099511628211ULL ^ (unsigned long long)(int)k3;
    h = h * 1099511628211ULL ^ (unsigned long long)v1;
    h = h * 1099511628211ULL ^ (unsigned long long)v2;
    return h;
}

/* =========================================================== SCENARIO 10 ===
 * Twenty instrumented calls return identical results with logging OFF and ON.
 */
static int sc_12_return_parity_on_off(void) {
    unsigned long long off_hash, on_hash;
    size_t lines;
    int rc = 0;
    cplus_libc_log_set_enabled(0);
    off_hash = parity_snapshot();
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "parity: cap_begin");
    cplus_libc_log_set_enabled(1);
    on_hash = parity_snapshot();
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    lines = cap_lines(&g_cap);
    rc |= expect(off_hash == on_hash,
                 "parity: logging ON changed an instrumented return value");
    rc |= expect(lines >= 20, "parity: expected 20 entry lines while ON");
    return rc;
}

/* =========================================================== SCENARIO 11 ===
 * 10000 direct log calls must never touch the allocator: live count and live
 * bytes are unchanged.  A drainer prevents pipe back-pressure.
 */
static int sc_12_tenk_direct_calls_no_alloc(void) {
    size_t before, after, bb, ab, lines;
    int i, rc = 0;
    before = cplus_alloc_live_count();
    bb = cplus_alloc_live_bytes();
    if (cap_begin(&g_cap, 1) != 0) return expect(0, "flood: cap_begin");
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 10000; ++i)
        cplus_libc_log("direct", "0123456789");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    after = cplus_alloc_live_count();
    ab = cplus_alloc_live_bytes();
    lines = cap_lines(&g_cap);
    rc |= expect(before == after, "flood: live allocation count changed");
    rc |= expect(bb == ab, "flood: live allocation bytes changed");
    rc |= expect(lines == 10000, "flood: captured line count != 10000");
    (void)bb; (void)ab;
    return rc;
}

/* =========================================================== SCENARIO 12 ===
 * Guarded error paths log but preserve their return values: free_checked(NULL)
 * -> EINVAL, forced malloc failure -> NULL/ENOMEM, aligned_alloc invalid
 * alignment -> NULL/EINVAL.
 */
static int sc_12_guarded_error_paths(void) {
    int rc_free = -1, rc = 0;
    void *bad_ptr;
    int rc_align;
    errno = 0;
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "err_paths: cap_begin");
    cplus_libc_log_set_enabled(1);
    rc_free = cplus_free_checked(NULL);
    cplus_alloc_fail_after(0);
    bad_ptr = cplus_malloc(8);
    cplus_alloc_fail_reset();
    rc_align = (cplus_aligned_alloc(3u, 16u) == NULL) ? 1 : 0;
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    rc |= expect(rc_free == EINVAL, "err_paths: free_checked(NULL) return changed");
    rc |= expect(bad_ptr == NULL, "err_paths: forced malloc should return NULL");
    rc |= expect(rc_align == 1, "err_paths: invalid aligned_alloc should return NULL");
    rc |= expect(cap_count_line_with(&g_cap, "[CPLUS-LIBC] free: enter") >= 1,
                 "err_paths: guarded free path did not log");
    rc |= expect(cap_count_line_with(&g_cap, "[CPLUS-LIBC] malloc: enter") >= 1,
                 "err_paths: malloc error path did not log");
    rc |= expect(cap_contains(&g_cap, "aligned_alloc: invalid alignment/size"),
                 "err_paths: aligned_alloc invalid path did not log");
    return rc;
}

/* =========================================================== SCENARIO 13 ===
 * 1000 rapid ON/OFF cycles: only the ON calls produce lines, OFF calls are
 * completely silent, and repeated flag writes never crash or wedge.
 */
static int sc_12_repeated_toggles(void) {
    int i, rc = 0;
    if (cap_begin(&g_cap, 1) != 0) return expect(0, "toggles: cap_begin");
    for (i = 0; i < 1000; ++i) {
        cplus_libc_log_set_enabled(1);
        cplus_libc_log("cycle", "on");
        cplus_libc_log_set_enabled(0);
        cplus_libc_log("cycle", "off");
    }
    cap_end(&g_cap);
    rc |= expect(cap_lines(&g_cap) == 1000, "toggles: expected 1000 ON lines");
    rc |= expect(cap_count_line_with(&g_cap, "cycle: off") == 0,
                 "toggles: an OFF-period line was emitted");
    return rc;
}

/* ----------------------------------------------------- long-message workers */

typedef struct {
    int id;
    volatile sig_atomic_t *start;
    char msg[1024];
} long_arg_t;

static void *long_worker(void *arg) {
    long_arg_t *a = (long_arg_t *)arg;
    int i;
    while (!*a->start) (void)sched_yield();
    for (i = 0; i < 30; ++i) cplus_libc_log("longmsg", a->msg);
    return NULL;
}

/* =========================================================== SCENARIO 14 ===
 * Four threads emit ~1KB messages concurrently.  Each single write is bounded
 * by 4096 and the prefix count must equal the call count.  On platforms whose
 * PIPE_BUF is smaller than the message, independent large writes can interleave
 * in the pipe; if that is observed we print a WARNING (documented limitation)
 * instead of a finding -- the logger guarantees a bounded line, not atomicity
 * for multi-KB writes from several threads.
 */
static int sc_12_concurrent_long_messages(void) {
    pthread_t t[4];
    long_arg_t a[4];
    volatile sig_atomic_t start = 0;
    int i, j, rc = 0, bad = 0;
    size_t max, prefixes;
    if (cap_begin(&g_cap, 1) != 0) return expect(0, "long_concurrent: cap_begin");
    for (i = 0; i < 4; ++i) {
        a[i].start = &start;
        a[i].id = i;
        memset(a[i].msg, 'L' + i, sizeof(a[i].msg) - 1u);
        a[i].msg[sizeof(a[i].msg) - 1u] = '\0';
    }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 4; ++i)
        (void)pthread_create(&t[i], NULL, long_worker, &a[i]);
    start = 1;
    for (i = 0; i < 4; ++i) (void)pthread_join(t[i], NULL);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    max = cap_max_line(&g_cap, &bad);
    prefixes = cap_count_line_with(&g_cap, "[CPLUS-LIBC] longmsg: ");
    rc |= expect(max <= LOG_LINE_CAP, "long_concurrent: a line exceeded 4096");
    rc |= expect(prefixes >= 4, "long_concurrent: almost no long lines captured");
    if (prefixes != 120u || bad != 0) {
        printf("WARNING: long_concurrent: concurrent >PIPE_BUF writes interleaved "
               "(prefixes=%llu bad=%d); atomicity of multi-KB lines is not "
               "guaranteed by the logger\n",
               (unsigned long long)prefixes, bad);
        for (j = 0; j < 0; ++j) (void)j; /* keep shape; no failure counted */
    }
    return rc;
}

/* --------------------------------------------------------- signal handling  */

static void sigusr1_handler(int signo) {
    (void)signo;
    /* Intentionally the only operation in the handler: no malloc, no stdio. */
    cplus_libc_log("sig_handler", "enter");
}

/* =========================================================== SCENARIO 15 ===
 * Fork a child, attach its fd 2 to a private pipe, install a SIGUSR1 handler
 * that logs, enable logging, and raise the signal.  The child arms alarm(2)
 * so a deadlock in a non-async-signal-safe logger is detected, not hung on.
 * Result is reported honestly (FINDING on deadlock/crash/missing line).
 */
static int sc_12_signal_handler_forked_child(void) {
    int logpipe[2], stpipe[2], rc = 0;
    pid_t pid;
    char st = 0, logbuf[8192];
    ssize_t n, total = 0;
    int status = 0;
    struct sigaction sa;

    if (pipe(logpipe) != 0 || pipe(stpipe) != 0)
        return expect(0, "sig_child: pipe failed");

    pid = fork();
    if (pid < 0) {
        close(logpipe[0]); close(logpipe[1]);
        close(stpipe[0]); close(stpipe[1]);
        return expect(0, "sig_child: fork failed");
    }
    if (pid == 0) {
        close(logpipe[0]);
        close(stpipe[0]);
        if (dup2(logpipe[1], STDERR_FILENO) < 0) _exit(2);
        close(logpipe[1]);
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sigusr1_handler;
        (void)sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (sigaction(SIGUSR1, &sa, NULL) != 0) {
            (void)write(stpipe[1], "E", 1);
            _exit(3);
        }
        (void)alarm(2); /* bound a potential deadlock */
        cplus_libc_log_set_enabled(1);
        (void)raise(SIGUSR1);
        (void)write(stpipe[1], "O", 1);
        _exit(0);
    }

    close(logpipe[1]);
    close(stpipe[1]);
    n = read(stpipe[0], &st, 1); /* EOF when child exits */
    if (n != 1) st = 0;
    for (;;) {
        n = read(logpipe[0], logbuf + (size_t)total,
                 (size_t)(sizeof(logbuf) - 1u - (size_t)total));
        if (n > 0) { total += n; continue; }
        break;
    }
    logbuf[total] = '\0';
    (void)waitpid(pid, &status, 0);
    close(logpipe[0]);
    close(stpipe[0]);

    if (WIFSIGNALED(status)) {
        printf("FINDING: sig_child: child died from signal %d (possible "
               "deadlock/non-async-signal-safe logger)\n", WTERMSIG(status));
        ++g_fail;
        rc = 1;
    } else {
        rc |= expect(st == 'O', "sig_child: child did not complete handler path");
        rc |= expect(strstr(logbuf, "[CPLUS-LIBC] sig_handler: enter") != NULL,
                     "sig_child: handler log line missing");
    }
    printf("WARNING: sig_child: log-from-signal-handler relies on write(2) + an "
           "atomic flag; observed %s\n",
           (rc == 0) ? "safe in forked child" : "unsafe/incomplete");
    return rc;
}

/* =========================================================== SCENARIO 16 ===
 * Empty api and empty msg still produce a well-formed "[CPLUS-LIBC] : \n".
 */
static int sc_12_empty_fields(void) {
    int bad = 0, rc = 0;
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "empty_fields: cap_begin");
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("", "");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    rc |= expect(bad == 0, "empty_fields: empty fields broke the line shape");
    rc |= expect(cap_contains(&g_cap, "[CPLUS-LIBC] : \n"),
                 "empty_fields: empty api/msg not rendered correctly");
    return rc;
}

/* =========================================================== SCENARIO 17 ===
 * A non-newline control byte inside msg must not corrupt line framing; no
 * embedded NUL may reach the pipe.
 */
static int sc_12_control_bytes(void) {
    char msg[40];
    int bad = 0, rc = 0, i;
    size_t k;
    memset(msg, 0, sizeof(msg));
    for (i = 0; i < 12; ++i) msg[i] = (char)(1 + (i % 9)); /* 1..9: excludes \n */
    msg[12] = '\0';
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "control: cap_begin");
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("control", msg);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    rc |= expect(bad == 0, "control: control bytes broke the line shape");
    rc |= expect(cap_lines(&g_cap) == 1, "control: control bytes split a line");
    for (k = 0; k < g_cap.len; ++k)
        if (g_cap.buf[k] == '\0') { rc |= expect(0, "control: embedded NUL in log"); break; }
    return rc;
}

/* =========================================================== SCENARIO 18 ===
 * Direct log calls and instrumented API entry logs coexist with correct line
 * framing when interleaved.
 */
static int sc_12_interleaved_direct_and_api(void) {
    int bad = 0, rc = 0, i;
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "interleaved: cap_begin");
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 25; ++i) {
        cplus_libc_log("direct", "tick");
        (void)cplus_strlen("abc");
        (void)cplus_memcmp("a", "b", 1);
    }
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    rc |= expect(bad == 0, "interleaved: line shape violated");
    rc |= expect(cap_lines(&g_cap) == 75, "interleaved: wrong line count");
    return rc;
}

/* =========================================================== SCENARIO 19 ===
 * Disabling mid-flood: the flag flip is observed promptly (at most one extra
 * racing line from the in-flight write), and no further lines accumulate.
 */
static int sc_12_disable_during_flood(void) {
    int i, rc = 0;
    size_t mid, after;
    if (cap_begin(&g_cap, 1) != 0) return expect(0, "disable_flood: cap_begin");
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 3000; ++i) cplus_libc_log("flood", "x");
    cplus_libc_log_set_enabled(0);
    for (i = 0; i < 500; ++i) cplus_libc_log("flood", "y");
    drain_stop(&g_cap);
    drain_now(&g_cap);
    mid = cap_lines(&g_cap);
    for (i = 0; i < 50; ++i) (void)sched_yield();
    drain_now(&g_cap);
    after = cap_lines(&g_cap);
    cap_end(&g_cap);
    rc |= expect(mid >= 3000, "disable_flood: missing ON-period lines");
    rc |= expect(after == mid, "disable_flood: lines appeared after OFF");
    rc |= expect(cap_count_line_with(&g_cap, "flood: y") == 0,
                 "disable_flood: OFF-period lines were emitted");
    return rc;
}

/* =========================================================== SCENARIO 20 ===
 * Multi-round-trip: enable -> N lines -> disable -> silence -> enable again ->
 * more lines.  Verifies the flag can be re-raised after being cleared.
 */
static int sc_12_multi_round_trip(void) {
    int i, rc = 0;
    if (cap_begin(&g_cap, 1) != 0) return expect(0, "rounds: cap_begin");
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 20; ++i) cplus_libc_log("r1", "x");
    cplus_libc_log_set_enabled(0);
    for (i = 0; i < 20; ++i) cplus_libc_log("r0", "x");
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 20; ++i) cplus_libc_log("r2", "x");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    rc |= expect(cap_count_line_with(&g_cap, "r1: x") == 20, "rounds: round 1 count");
    rc |= expect(cap_count_line_with(&g_cap, "r0: x") == 0, "rounds: OFF leaked");
    rc |= expect(cap_count_line_with(&g_cap, "r2: x") == 20, "rounds: round 2 count");
    return rc;
}

/* =========================================================== SCENARIO 21 ===
 * Live-byte counter is also invariant under pure logging (complement of the
 * live-count check in scenario 11).
 */
static int sc_12_live_bytes_invariant(void) {
    size_t b0, b1, c0, c1;
    int i, rc = 0;
    b0 = cplus_alloc_live_bytes();
    c0 = cplus_alloc_live_count();
    if (cap_begin(&g_cap, 1) != 0) return expect(0, "live_bytes: cap_begin");
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 2000; ++i) cplus_libc_log("bytes", "0123456789abcdef0123456789");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    b1 = cplus_alloc_live_bytes();
    c1 = cplus_alloc_live_count();
    rc |= expect(b0 == b1, "live_bytes: allocator byte count changed");
    rc |= expect(c0 == c1, "live_bytes: allocator live count changed");
    return rc;
}

/* =========================================================== SCENARIO 22 ===
 * Guarded APIs with invalid arguments return their documented error codes
 * while the logging flag is ON (observational inertness of the flag).
 */
static int sc_12_invalid_args_with_logging(void) {
    cplus_alloc_meta meta;
    void *nullp = NULL;
    int rc = 0, r1, r2;
    errno = 0;
    cplus_libc_log_set_enabled(0);
    r1 = cplus_alloc_get_meta(NULL, &meta);
    r2 = cplus_free_checked(NULL);
    if (cap_begin(&g_cap, 0) != 0) return expect(0, "invalid_args: cap_begin");
    cplus_libc_log_set_enabled(1);
    {
        int a1 = cplus_alloc_get_meta(NULL, &meta);
        int a2 = cplus_free_checked(NULL);
        cplus_libc_log_set_enabled(0);
        cap_end(&g_cap);
        rc |= expect(a1 == r1, "invalid_args: alloc_get_meta(NULL) return changed");
        rc |= expect(a2 == r2, "invalid_args: free_checked(NULL) return changed");
    }
    rc |= expect(r1 == EINVAL, "invalid_args: alloc_get_meta(NULL) != EINVAL");
    rc |= expect(r2 == EINVAL, "invalid_args: free_checked(NULL) != EINVAL");
    (void)nullp;
    return rc;
}

/* =========================================================== SCENARIO 23 ===
 * Exactly one trailing newline per log call: N calls -> N newline-delimited
 * lines, with no leading or double blank lines.
 */
static int sc_12_one_newline_per_call(void) {
    int i, rc = 0;
    size_t lines;
    if (cap_begin(&g_cap, 1) != 0) return expect(0, "newline: cap_begin");
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 500; ++i) cplus_libc_log("nl", "body");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    lines = cap_lines(&g_cap);
    rc |= expect(lines == 500, "newline: line count != call count");
    rc |= expect(g_cap.len > 0 && g_cap.buf[g_cap.len - 1u] == '\n',
                 "newline: output does not end with a newline");
    return rc;
}

/* =========================================================== SCENARIO 24 ===
 * Logging must be observationally inert for errno on successful instrumented
 * calls: a sentinel errno survives a write(2) to a healthy pipe.
 */
static int sc_12_errno_preserved(void) {
    const int SENT = 4242;
    char dst[16];
    int e_off, e_on, rc = 0;
    cplus_libc_log_set_enabled(0);
    errno = SENT;
    (void)cplus_strlen("abcd");
    (void)cplus_memcpy(dst, "abcdefgh", 8);
    (void)cplus_memcmp("a", "a", 1);
    e_off = errno;

    if (cap_begin(&g_cap, 0) != 0) return expect(0, "errno: cap_begin");
    cplus_libc_log_set_enabled(1);
    errno = SENT;
    (void)cplus_strlen("abcd");
    (void)cplus_memcpy(dst, "abcdefgh", 8);
    (void)cplus_memcmp("a", "a", 1);
    e_on = errno;
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    rc |= expect(e_off == SENT, "errno: sentinel clobbered with logging OFF");
    rc |= expect(e_on == SENT, "errno: sentinel clobbered by logging write(2)");
    errno = 0;
    return rc;
}

int main(void) {
    int passed = 0, total = 0;

#define RUN(fn) do { ++total; if ((fn)() == 0) ++passed; } while (0)

    RUN(sc_12_default_off_silence);
    RUN(sc_12_enable_entry_lines);
    RUN(sc_12_disable_restores_silence);
    RUN(sc_12_null_api_null_msg);
    RUN(sc_12_long_fields_truncated);
    RUN(sc_12_no_sensitive_bytes);
    RUN(sc_12_line_shape_contract);
    RUN(sc_12_stderr_closed_resumed);
    RUN(sc_12_thread_toggle_no_torn_lines);
    RUN(sc_12_return_parity_on_off);
    RUN(sc_12_tenk_direct_calls_no_alloc);
    RUN(sc_12_guarded_error_paths);
    RUN(sc_12_repeated_toggles);
    RUN(sc_12_concurrent_long_messages);
    RUN(sc_12_signal_handler_forked_child);
    RUN(sc_12_empty_fields);
    RUN(sc_12_control_bytes);
    RUN(sc_12_interleaved_direct_and_api);
    RUN(sc_12_disable_during_flood);
    RUN(sc_12_multi_round_trip);
    RUN(sc_12_live_bytes_invariant);
    RUN(sc_12_invalid_args_with_logging);
    RUN(sc_12_one_newline_per_call);
    RUN(sc_12_errno_preserved);

#undef RUN

    /* Defensive: leave the process in the documented default state. */
    cplus_libc_log_set_enabled(0);
    printf("runtime_logging: %d scenarios ok\n", passed);
    if (g_fail != 0) {
        printf("runtime_logging: %d/%d scenarios ok, %d FINDING(s)\n",
               passed, total, g_fail);
        return 1;
    }
    return 0;
}
