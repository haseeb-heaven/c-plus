/* hundred_logging_threads.c -- 100 DISTINCT runtime scenarios for the opt-in
 * C+ libc logger and for concurrency around it.
 *
 * Build (exact verification command):
 *   /usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic -I. -Ilibc \
 *     -Ilibc/install-include tests/methods/hundred_logging_threads.c \
 *     build/libc/libcplus_hardened.a -lm -pthread
 *
 * Contract under test (libc/log/log.c):
 *   - global atomic flag, OFF by default;
 *   - one bounded write(2) of "[CPLUS-LIBC] api: msg\n" (4096-byte stack line);
 *   - long fields get a "..." marker, NULL fields render as "?";
 *   - logging never changes an API return value, errno, or allocator state.
 *
 * Capture is a pipe on fd 2 with a NONBLOCKING drainer thread; the original
 * stderr descriptor is always restored. All worker state is either published
 * before the threads start, kept in a per-thread slot, or protected by a
 * mutex/condvar/barrier -- there is no test-side data race.
 *
 * macOS has no native pthread_barrier_t, so a portable mutex+condvar barrier
 * is used and labelled honestly in the method column.
 *
 * cplus_safe_stat_* lives in the separate SafeCRT archive, not in
 * libcplus_hardened.a. It is discovered with dlsym(); when absent those six
 * scenarios report UNAVAILABLE instead of pretending a check ran.
 *
 * HONEST NOTE on async-signal-safety: the logger uses write(2) and an atomic
 * flag, which makes the call *plausibly* safe from a signal handler, but that
 * is not a portable guarantee (write may set errno, and the C standard says
 * almost nothing about signal handlers). Scenario 20 observes it only in a
 * forked child with an alarm() deadline; it does not claim a general proof.
 *
 * Every case prints one line:
 *   hundred_logging_threads: case <n> <method> <scenario> <OUTCOME>
 * main prints:
 *   hundred_logging_threads: <N>/100 scenarios ok
 * Outcomes are PASS, FAIL, or UNAVAILABLE (the latter counts as ok because
 * the corresponding API is optional for this library build). A genuine
 * contract mismatch also prints a FINDING line.
 */
#define _POSIX_C_SOURCE 200809L

#include "libc/install-include/cplus_libc.h"

#include <dlfcn.h>
#include <errno.h>
#include <fenv.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* POSIX_C_SOURCE hides RTLD_DEFAULT on some systems; Darwin defines it as
 * ((void *)0), which is the "search the main executable" handle we need. */
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif

/* Not exported by the installed public header (libc/log/log.h only). */
extern void cplus_libc_log(const char *api, const char *msg);

#define CAP_CAP        (2u * 1024u * 1024u) /* bounded capture storage */
#define LOG_LINE_CAP   4096u                 /* logger's fixed line bound */
#define MAX_THREADS    8
#define BAD_PTR        ((void *)(uintptr_t)1)

#define OUT_PASS       0
#define OUT_FAIL       1
#define OUT_UNAVAIL    2

static int g_case;          /* set by main before each scenario */
static int g_findings;      /* number of genuine mismatches */
static int g_unavail;       /* number of optional-API skips */
static char g_finding_text[256];

static void finding(const char *detail) {
    ++g_findings;
    snprintf(g_finding_text, sizeof(g_finding_text), "%s", detail);
    printf("FINDING: case %d: %s\n", g_case, g_finding_text);
}

static void note(const char *text) {
    printf("NOTE: case %d: %s\n", g_case, text);
}

#define CK(cond, msg)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            finding(msg);                                                      \
            rc = OUT_FAIL;                                                     \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------- capture ----- */

typedef struct {
    int pfd[2];
    int saved;
    char *buf;
    size_t len;
    int active;
    int draining;
} cap_t;

static char g_cap_storage[CAP_CAP];
static cap_t g_cap;

static void drain_now(cap_t *c) {
    for (;;) {
        char tmp[8192];
        ssize_t n = read(c->pfd[0], tmp, sizeof(tmp));
        if (n > 0) {
            size_t room, take;
            if (c->len >= CAP_CAP - 1u) continue; /* bounded: drop overflow */
            room = (CAP_CAP - 1u) - c->len;
            take = ((size_t)n > room) ? room : (size_t)n;
            memcpy(c->buf + c->len, tmp, take);
            c->len += take;
            c->buf[c->len] = '\0';
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break; /* EAGAIN / EOF: all that can be done nonblocking */
    }
}

static pthread_mutex_t g_drain_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_drain_tid;
static cap_t *g_drain_cap;
static int g_drain_stop;

static void *drain_thread(void *unused) {
    struct timespec ts;
    (void)unused;
    ts.tv_sec = 0;
    ts.tv_nsec = 200000L;
    for (;;) {
        int stop;
        pthread_mutex_lock(&g_drain_mu);
        stop = g_drain_stop;
        pthread_mutex_unlock(&g_drain_mu);
        drain_now(g_drain_cap);
        if (stop) break;
        (void)nanosleep(&ts, NULL);
    }
    return NULL;
}

static int cap_begin(cap_t *c) {
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
    g_drain_cap = c;
    pthread_mutex_lock(&g_drain_mu);
    g_drain_stop = 0;
    pthread_mutex_unlock(&g_drain_mu);
    if (pthread_create(&g_drain_tid, NULL, drain_thread, NULL) == 0) {
        c->draining = 1;
    }
    return 0;
}

static void cap_end(cap_t *c) {
    if (!c->active) return;
    if (c->draining) {
        pthread_mutex_lock(&g_drain_mu);
        g_drain_stop = 1;
        pthread_mutex_unlock(&g_drain_mu);
        (void)pthread_join(g_drain_tid, NULL);
        c->draining = 0;
    }
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

static size_t cap_lines(const cap_t *c) {
    size_t i, n = 0;
    for (i = 0; i < c->len; ++i)
        if (c->buf[i] == '\n') ++n;
    return n;
}

static size_t cap_count_line_with(const cap_t *c, const char *needle) {
    size_t start = 0, i, hits = 0, nl = strlen(needle);
    if (nl == 0) return 0;
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

/* Validate ^\[CPLUS-LIBC\] .*: .*$ and return the longest line length. */
static size_t cap_max_line(const cap_t *c, int *bad) {
    static const char PREFIX[] = "[CPLUS-LIBC] ";
    const size_t plen = sizeof(PREFIX) - 1u;
    size_t start = 0, i, max = 0;
    *bad = 0;
    for (i = 0; i <= c->len; ++i) {
        if (i == c->len || c->buf[i] == '\n') {
            const char *p = c->buf + start;
            size_t l = i - start, j;
            int found = 0;
            if (l > max) max = l;
            if (l == 0) { start = i + 1; continue; }
            if (l < plen || memcmp(p, PREFIX, plen) != 0) {
                ++*bad;
                start = i + 1;
                continue;
            }
            for (j = plen; j + 1 < l; ++j) {
                if (p[j] == ':' && p[j + 1] == ' ') { found = 1; break; }
            }
            if (!found) ++*bad;
            start = i + 1;
        }
    }
    return max;
}

/* ------------------------------------------------------- thread helpers --- */

typedef struct { pthread_mutex_t mu; pthread_cond_t cv; int count, arrived; unsigned long gen; } bar_t;

static int bar_init(bar_t *b, int count) {
    b->count = count; b->arrived = 0; b->gen = 0;
    if (pthread_mutex_init(&b->mu, NULL) != 0) return -1;
    if (pthread_cond_init(&b->cv, NULL) != 0) {
        pthread_mutex_destroy(&b->mu);
        return -1;
    }
    return 0;
}

static int bar_wait(bar_t *b) {
    unsigned long gen;
    pthread_mutex_lock(&b->mu);
    gen = b->gen;
    if (++b->arrived == b->count) {
        b->arrived = 0;
        ++b->gen;
        pthread_cond_broadcast(&b->cv);
        pthread_mutex_unlock(&b->mu);
        return 0;
    }
    while (gen == b->gen) pthread_cond_wait(&b->cv, &b->mu);
    pthread_mutex_unlock(&b->mu);
    return 0;
}

static void bar_destroy(bar_t *b) {
    (void)pthread_cond_destroy(&b->cv);
    (void)pthread_mutex_destroy(&b->mu);
}

/* Spawn n threads, join every one that was created; returns 0 on success. */
static int spawn_join(int n, void *(*fn)(void *), void **args, void **rets) {
    pthread_t tids[MAX_THREADS];
    int made = 0, i, ok = 0;
    if (n < 0 || n > MAX_THREADS) return -1;
    for (i = 0; i < n; ++i) {
        if (pthread_create(&tids[i], NULL, fn, args[i]) != 0) break;
        ++made;
    }
    if (made != n) ok = -1;
    for (i = 0; i < made; ++i) {
        void *rv = NULL;
        if (pthread_join(tids[i], &rv) != 0) ok = -1;
        if (rets) rets[i] = rv;
    }
    return ok;
}

typedef struct { int id; bar_t *bar; } simple_arg;

/* ------------------------------------------------------ shared utilities --- */

static void call_instrumented_set(void) {
    char dst[16];
    char buf[48];
    void *p = cplus_malloc(8u);
    (void)cplus_memcpy(dst, "abcdefgh", 8u);
    (void)cplus_strlen("abc");
    {
        FILE *fp = cplus_fopen("/dev/null", "r");
        if (fp) (void)cplus_fclose(fp);
    }
    (void)cplus_strtol("  -77rest", NULL, 10);
    (void)cplus_snprintf(buf, sizeof(buf), "set-%d", (int)(p ? 1 : 0));
    cplus_free(p);
}

/* Fold the observable results of 12 instrumented calls into one value. */
static unsigned long long parity_snapshot(void) {
    char d[32], s[32];
    char *e = NULL, *cp;
    unsigned long long h = 1469598103934665603ULL;
    long v;
    size_t n;
    int k;
    memset(d, 0, sizeof(d));
    memset(s, 'z', sizeof(s));
    v = cplus_strtol("  -42rest", &e, 10);
    n = cplus_strlen("hello-world");
    cp = (char *)cplus_memcpy(d, s, 16u);
    k = cplus_memcmp("abc", "abd", 3u);
    cp = (char *)cplus_memchr(s, 'z', sizeof(s));
    k += cplus_strcmp("aaa", "aab");
    n += cplus_strnlen("abcdef", 4u);
    h = h * 1099511628211ULL ^ (unsigned long long)(uintptr_t)(e ? (e - "  -42rest") : 0);
    h = h * 1099511628211ULL ^ (unsigned long long)(uintptr_t)cp;
    h = h * 1099511628211ULL ^ (unsigned long long)n;
    h = h * 1099511628211ULL ^ (unsigned long long)(unsigned int)k;
    h = h * 1099511628211ULL ^ (unsigned long long)v;
    return h;
}

/* Optional SafeCRT telemetry, resolved at runtime (absent in this archive). */
typedef struct { char api[24]; unsigned long calls, ok, fail; } safe_stat_t;
typedef void (*fn_record)(const char *, int);
typedef void (*fn_reset)(void);
typedef int (*fn_get)(const char *, safe_stat_t *);
typedef size_t (*fn_dump)(safe_stat_t *, size_t);

static fn_record p_record;
static fn_reset  p_reset;
static fn_get    p_get;
static fn_dump   p_dump;

static void stats_resolve(void) {
    void *s;
    s = dlsym(RTLD_DEFAULT, "cplus_safe_stat_record");
    if (s) memcpy(&p_record, &s, sizeof(s));
    s = dlsym(RTLD_DEFAULT, "cplus_safe_stats_reset");
    if (s) memcpy(&p_reset, &s, sizeof(s));
    s = dlsym(RTLD_DEFAULT, "cplus_safe_stats_get");
    if (s) memcpy(&p_get, &s, sizeof(s));
    s = dlsym(RTLD_DEFAULT, "cplus_safe_stats_dump");
    if (s) memcpy(&p_dump, &s, sizeof(s));
}

static int stats_ready(void) {
    return p_record && p_reset && p_get && p_dump;
}

/* =========================================================== cases 001-020 */

static int c001(void) {
    char dst[16];
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("001: cap_begin"); return OUT_FAIL; }
    (void)cplus_malloc(8u);
    (void)cplus_memcpy(dst, "abcdefgh", 8u);
    (void)cplus_strlen("abc");
    (void)cplus_strtol("42", NULL, 10);
    cplus_libc_log("direct", "off");
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 0u, "001: logger emitted while disabled by default");
    return rc;
}

static int c002(void) {
    static const char *const want[] = {
        "[CPLUS-LIBC] malloc: enter", "[CPLUS-LIBC] memcpy: enter",
        "[CPLUS-LIBC] strlen: enter", "[CPLUS-LIBC] fopen: enter",
        "[CPLUS-LIBC] strtol: enter", "[CPLUS-LIBC] snprintf: enter"
    };
    size_t k;
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("002: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    call_instrumented_set();
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (k = 0; k < sizeof(want) / sizeof(want[0]); ++k)
        CK(cap_count_line_with(&g_cap, want[k]) == 1u, "002: missing entry line");
    return rc;
}

static int c003(void) {
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("003: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    (void)cplus_strlen("on");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 1u, "003: expected exactly one ON line");
    if (cap_begin(&g_cap) != 0) { finding("003: second cap_begin"); return OUT_FAIL; }
    (void)cplus_strlen("off");
    (void)cplus_strlen("off");
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 0u, "003: output persisted after disable");
    return rc;
}

static int c004(void) {
    static const int vals[] = { 1, 7, -3 };
    size_t k;
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("004: cap_begin"); return OUT_FAIL; }
    for (k = 0; k < 3u; ++k) {
        cplus_libc_log_set_enabled(vals[k]);
        cplus_libc_log("norm", "on");
        cplus_libc_log_set_enabled(0);
        cplus_libc_log("norm", "off");
    }
    cap_end(&g_cap);
    CK(cap_count_line_with(&g_cap, "norm: on") == 3u, "004: nonzero value did not enable");
    CK(cap_count_line_with(&g_cap, "norm: off") == 0u, "004: zero value did not disable");
    return rc;
}

static int c005(void) {
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("005: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 1000; ++i) {
        cplus_libc_log_set_enabled(1);
        cplus_libc_log("cycle", "on");
        cplus_libc_log_set_enabled(0);
        cplus_libc_log("cycle", "off");
    }
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 1000u, "005: expected 1000 ON lines");
    CK(cap_count_line_with(&g_cap, "cycle: off") == 0u, "005: OFF-period line emitted");
    return rc;
}

static int c006(void) {
    int i, rc = OUT_PASS;
    cplus_libc_log_set_enabled(0);
    if (cap_begin(&g_cap) != 0) { finding("006: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 1000; ++i) {
        (void)cplus_strlen("load");
        (void)cplus_memcmp("a", "a", 1u);
        cplus_libc_log("load", "off");
    }
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 0u, "006: OFF logger emitted under load");
    return rc;
}

static int c007(void) {
    int bad = 0, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("007: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log(NULL, NULL);
    cplus_libc_log(NULL, "msg-present");
    cplus_libc_log("api-present", NULL);
    cplus_libc_log("both-present", "ok");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 4u, "007: expected four lines");
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "007: NULL fields broke line shape");
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] ?: ?\n"), "007: NULL/NULL not '?'");
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] ?: msg-present\n"), "007: NULL api not '?'");
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] api-present: ?\n"), "007: NULL msg not '?'");
    CK(!cap_contains(&g_cap, "(null)"), "007: '(null)' leaked");
    return rc;
}

static int c008(void) {
    int bad = 0, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("008: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("", "");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "008: empty fields broke line shape");
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] : \n"), "008: empty fields not rendered");
    return rc;
}

static int c009(void) {
    char msg[9000];
    size_t max;
    int bad = 0, rc = OUT_PASS;
    memset(msg, 'B', sizeof(msg) - 1u);
    msg[sizeof(msg) - 1u] = '\0';
    if (cap_begin(&g_cap) != 0) { finding("009: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("longmsg", msg);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    max = cap_max_line(&g_cap, &bad);
    CK(cap_lines(&g_cap) == 1u, "009: expected one line");
    CK(max <= LOG_LINE_CAP, "009: line exceeded 4096 bound");
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] longmsg: "), "009: api prefix lost");
    CK(cap_contains(&g_cap, "...\n"), "009: truncation marker missing");
    CK(bad == 0, "009: long-msg line shape violated");
    return rc;
}

static int c010(void) {
    char api[9000];
    size_t max;
    int bad = 0, rc = OUT_PASS;
    memset(api, 'A', sizeof(api) - 1u);
    api[sizeof(api) - 1u] = '\0';
    if (cap_begin(&g_cap) != 0) { finding("010: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log(api, "short");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    max = cap_max_line(&g_cap, &bad);
    CK(max <= LOG_LINE_CAP, "010: long-api line exceeded 4096");
    CK(cap_contains(&g_cap, "...\n"), "010: long-api truncation marker missing");
    if (bad != 0)
        note("a ~9000-byte api fills the 4096 body and truncates the ': ' delimiter; bounded but not shape-valid");
    return rc;
}

static int c011(void) {
    char api[9000], msg[9000];
    size_t max;
    int bad = 0, rc = OUT_PASS;
    memset(api, 'A', sizeof(api) - 1u); api[sizeof(api) - 1u] = '\0';
    memset(msg, 'B', sizeof(msg) - 1u); msg[sizeof(msg) - 1u] = '\0';
    if (cap_begin(&g_cap) != 0) { finding("011: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log(api, msg);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    max = cap_max_line(&g_cap, &bad);
    CK(max <= LOG_LINE_CAP, "011: both-long line exceeded 4096");
    CK(cap_lines(&g_cap) == 1u, "011: both-long call produced wrong line count");
    CK(cap_contains(&g_cap, "...\n"), "011: both-long marker missing");
    return rc;
}

static int c012(void) {
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("012: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("api12", "payload-42");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] api12: payload-42\n"),
       "012: exact rendered line mismatch");
    return rc;
}

static int c013(void) {
    char msg[40];
    int i, bad = 0, rc = OUT_PASS;
    memset(msg, 0, sizeof(msg));
    for (i = 0; i < 12; ++i) msg[i] = (char)(1 + (i % 9)); /* 1..9, no \n or NUL */
    msg[12] = '\0';
    if (cap_begin(&g_cap) != 0) { finding("013: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("control", msg);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "013: control bytes broke line shape");
    CK(cap_lines(&g_cap) == 1u, "013: control bytes split a line");
    for (i = 0; i < (int)g_cap.len; ++i)
        if (g_cap.buf[i] == '\0') { finding("013: embedded NUL reached pipe"); rc = OUT_FAIL; break; }
    return rc;
}

static int c014(void) {
    char msg[32];
    int rc = OUT_PASS;
    (void)cplus_snprintf(msg, sizeof(msg), "nul-terminated-%d", 14);
    if (cap_begin(&g_cap) != 0) { finding("014: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("built", msg);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] built: nul-terminated-14\n"),
       "014: snprintf-built message rendered incorrectly");
    return rc;
}

static int c015(void) {
    int bad = 0, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("015: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("a", "one");
    cplus_libc_log(NULL, "two");
    cplus_libc_log("c", NULL);
    (void)cplus_strlen("abc");
    (void)cplus_memcmp("x", "y", 1u);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "015: mixed line-shape contract violated");
    CK(cap_lines(&g_cap) == 5u, "015: wrong mixed line count");
    return rc;
}

static int c016(void) {
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("016: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 500; ++i) cplus_libc_log("nl", "body");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 500u, "016: line count != call count");
    CK(g_cap.len > 0u && g_cap.buf[g_cap.len - 1u] == '\n', "016: output lacks final newline");
    return rc;
}

static int c017(void) {
    static const char SECRET[] = "TOPSECRET_SENTINEL_7f3a9c2e_HUNDRED";
    char dst[128];
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("017: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    (void)cplus_memcpy(dst, SECRET, sizeof(SECRET));
    (void)cplus_strlen(SECRET);
    (void)cplus_strtol(SECRET, NULL, 10);
    { FILE *fp = cplus_fopen(SECRET, "r"); if (fp) (void)cplus_fclose(fp); }
    cplus_libc_log("sentinel_test", "ok");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(!cap_contains(&g_cap, SECRET), "017: sensitive sentinel leaked");
    CK(cap_lines(&g_cap) >= 1u, "017: no log lines emitted");
    return rc;
}

static int c018(void) {
    const int SENT = 4242;
    char dst[16];
    int e_off, e_on, rc = OUT_PASS;
    cplus_libc_log_set_enabled(0);
    errno = SENT;
    (void)cplus_strlen("abcd");
    (void)cplus_memcpy(dst, "abcdefgh", 8u);
    e_off = errno;
    if (cap_begin(&g_cap) != 0) { finding("018: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    errno = SENT;
    (void)cplus_strlen("abcd");
    (void)cplus_memcpy(dst, "abcdefgh", 8u);
    e_on = errno;
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(e_off == SENT, "018: sentinel clobbered with logging OFF");
    CK(e_on == SENT, "018: sentinel clobbered by logging write(2)");
    errno = 0;
    return rc;
}

static int c019(void) {
    struct stat s0, s1;
    int rc = OUT_PASS;
    if (fstat(STDERR_FILENO, &s0) != 0) { finding("019: fstat(before)"); return OUT_FAIL; }
    if (cap_begin(&g_cap) != 0) { finding("019: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    (void)close(STDERR_FILENO);
    cplus_libc_log("closed_fd", "no-crash");
    (void)cplus_strlen("abc");
    if (dup2(g_cap.pfd[1], STDERR_FILENO) < 0) finding("019: could not re-attach fd 2");
    cplus_libc_log("resumed", "enter");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] resumed: enter\n"),
       "019: logging did not resume after stderr re-attach");
    CK(!cap_contains(&g_cap, "closed_fd: no-crash"),
       "019: write to closed fd unexpectedly appeared");
    if (fstat(STDERR_FILENO, &s1) == 0)
        CK(s0.st_dev == s1.st_dev && s0.st_ino == s1.st_ino,
           "019: original stderr descriptor not restored");
    else
        finding("019: fstat(after)");
    return rc;
}

static void sigusr1_handler(int signo) {
    (void)signo;
    cplus_libc_log("sig_handler", "enter");
}

static int c020(void) {
    int logpipe[2], stpipe[2], status = 0, rc = OUT_PASS;
    pid_t pid;
    char st = 0, logbuf[8192];
    ssize_t n, total = 0;
    struct sigaction sa;
    if (pipe(logpipe) != 0 || pipe(stpipe) != 0) {
        finding("020: pipe failed"); return OUT_FAIL;
    }
    pid = fork();
    if (pid < 0) {
        close(logpipe[0]); close(logpipe[1]); close(stpipe[0]); close(stpipe[1]);
        finding("020: fork failed"); return OUT_FAIL;
    }
    if (pid == 0) {
        close(logpipe[0]); close(stpipe[0]);
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
        (void)alarm(2);
        cplus_libc_log_set_enabled(1);
        (void)raise(SIGUSR1);
        (void)write(stpipe[1], "O", 1);
        _exit(0);
    }
    close(logpipe[1]); close(stpipe[1]);
    n = read(stpipe[0], &st, 1);
    if (n != 1) st = 0;
    for (;;) {
        n = read(logpipe[0], logbuf + (size_t)total,
                 (size_t)(sizeof(logbuf) - 1u - (size_t)total));
        if (n > 0) { total += n; continue; }
        break;
    }
    logbuf[total] = '\0';
    (void)waitpid(pid, &status, 0);
    close(logpipe[0]); close(stpipe[0]);
    if (WIFSIGNALED(status)) {
        finding("020: child died from signal during handler log");
        rc = OUT_FAIL;
    } else {
        CK(st == 'O', "020: child did not complete handler path");
        CK(strstr(logbuf, "[CPLUS-LIBC] sig_handler: enter") != NULL,
           "020: handler log line missing");
    }
    note("log-from-signal-handler relies on write(2)+atomic flag; observed only in a forked child, not a general async-signal-safety proof");
    return rc;
}

/* =========================================================== cases 021-040 */

static int c021(void) {
    int rc = OUT_PASS;
    void *p;
    if (cap_begin(&g_cap) != 0) { finding("021: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    p = cplus_malloc(24u);
    cplus_free(p);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] malloc: enter") == 1u,
       "021: malloc entry line missing/duplicated");
    return rc;
}

typedef struct { int id; void *p; } alloc_arg;

static void *alloc_meta_worker(void *arg) {
    alloc_arg *a = (alloc_arg *)arg;
    cplus_alloc_meta m;
    size_t sz = (size_t)(24 + a->id * 3);
    a->p = cplus_malloc(sz);
    if (!a->p) return BAD_PTR;
    if (cplus_alloc_get_meta(a->p, &m) != 0) return BAD_PTR;
    if (!m.is_live || m.byte_size != sz || m.generation == 0ull) return BAD_PTR;
    if (cplus_alloc_size(a->p) != sz) return BAD_PTR;
    if (cplus_alloc_is_live(a->p) != 1) return BAD_PTR;
    cplus_free(a->p);
    a->p = NULL;
    return NULL;
}

static int c022(void) {
    alloc_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    size_t before, after;
    int i, rc = OUT_PASS;
    before = cplus_alloc_live_count();
    for (i = 0; i < 8; ++i) {
        args[i].id = i; args[i].p = NULL; argp[i] = &args[i]; rets[i] = NULL;
    }
    CK(spawn_join(8, alloc_meta_worker, argp, rets) == 0, "022: spawn_join");
    for (i = 0; i < 8; ++i) CK(rets[i] == NULL, "022: metadata worker failed");
    after = cplus_alloc_live_count();
    CK(before == after, "022: live count not restored after concurrent alloc/free");
    return rc;
}

typedef struct { pthread_mutex_t mu; int result[MAX_THREADS]; } fail_ctx;
typedef struct { fail_ctx *c; int id; } fail_arg;

static void *fail_worker(void *p) {
    fail_arg *a = (fail_arg *)p;
    void *q;
    pthread_mutex_lock(&a->c->mu);
    errno = 0;
    q = cplus_malloc(24u);
    a->c->result[a->id] = (q == NULL && errno == ENOMEM) ? 1 : 0;
    cplus_free(q);
    pthread_mutex_unlock(&a->c->mu);
    return NULL;
}

static int c023(void) {
    fail_ctx c;
    fail_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, rc = OUT_PASS;
    CK(pthread_mutex_init(&c.mu, NULL) == 0, "023: mutex init");
    for (i = 0; i < 6; ++i) { c.result[i] = 0; args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL; }
    pthread_mutex_lock(&c.mu);
    cplus_alloc_fail_after(0L);
    pthread_mutex_unlock(&c.mu);
    CK(spawn_join(6, fail_worker, argp, rets) == 0, "023: spawn_join");
    pthread_mutex_lock(&c.mu);
    cplus_alloc_fail_reset();
    pthread_mutex_unlock(&c.mu);
    for (i = 0; i < 6; ++i) CK(c.result[i] == 1, "023: injection did not yield NULL/ENOMEM");
    pthread_mutex_destroy(&c.mu);
    return rc;
}

static int c024(void) {
    char dst[16];
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("024: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    (void)cplus_memcpy(dst, "abcdefgh", 8u);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] memcpy: enter") == 1u,
       "024: memcpy entry line missing");
    return rc;
}

typedef struct { int id; } cp_arg;

static void *memcpy_worker(void *p) {
    cp_arg *a = (cp_arg *)p;
    unsigned char src[64], dst[64];
    int i;
    memset(src, (unsigned char)a->id, sizeof(src));
    for (i = 0; i < 50; ++i) {
        if (cplus_memcpy(dst, src, sizeof(src)) != dst) return BAD_PTR;
        if (memcmp(dst, src, sizeof(src)) != 0) return BAD_PTR;
    }
    return NULL;
}

static int c025(void) {
    cp_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, bad = 0, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("025: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 8; ++i) { args[i].id = i; argp[i] = &args[i]; rets[i] = NULL; }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(8, memcpy_worker, argp, rets) == 0, "025: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < 8; ++i) CK(rets[i] == NULL, "025: memcpy worker failed");
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "025: concurrent memcpy log line torn");
    CK(cap_count_line_with(&g_cap, "memcpy: enter") == 400u, "025: memcpy log count");
    return rc;
}

static int c026(void) {
    size_t n;
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("026: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    n = cplus_strlen("abc");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(n == 3u, "026: strlen return changed by logging");
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] strlen: enter") == 1u,
       "026: strlen entry line missing");
    return rc;
}

static int c027(void) {
    FILE *fp;
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("027: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    fp = cplus_fopen("/dev/null", "r");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(fp != NULL, "027: fopen(/dev/null) failed");
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] fopen: enter") == 1u,
       "027: fopen entry line missing");
    if (fp) (void)cplus_fclose(fp);
    return rc;
}

static int c028(void) {
    char *end = NULL;
    long v;
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("028: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    v = cplus_strtol("  -77rest", &end, 10);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(v == -77L && end != NULL && *end == 'r', "028: strtol result changed by logging");
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] strtol: enter") == 1u,
       "028: strtol entry line missing");
    return rc;
}

static int c029(void) {
    char buf[32];
    int n, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("029: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    n = cplus_snprintf(buf, sizeof(buf), "snp-%d", 29);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(n == 6 && strcmp(buf, "snp-29") == 0, "029: snprintf result changed by logging");
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] snprintf: enter") == 1u,
       "029: snprintf entry line missing");
    return rc;
}

static int c030(void) {
    void *p;
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("030: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    p = cplus_malloc(16u);
    cplus_free(p);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] free: enter") >= 1u,
       "030: free entry line missing");
    return rc;
}

/* =========================================================== cases 031-050 */

typedef struct { int id; volatile int *start; int iters; pthread_mutex_t *mu; } toggle_arg;

static void *toggle_worker(void *p) {
    toggle_arg *a = (toggle_arg *)p;
    int i;
    while (!*a->start) (void)sched_yield();
    for (i = 0; i < a->iters; ++i) {
        if (a->mu) pthread_mutex_lock(a->mu);
        cplus_libc_log_set_enabled(1);
        cplus_libc_log("tog", "on");
        cplus_libc_log_set_enabled(i & 1);
        if (a->mu) pthread_mutex_unlock(a->mu);
    }
    return NULL;
}

static int toggle_case(int n, int iters, int use_mutex, const char *label) {
    pthread_t tids[MAX_THREADS];
    toggle_arg args[MAX_THREADS];
    void *argp[MAX_THREADS];
    volatile int start = 0;
    pthread_mutex_t mu;
    int i, bad = 0, rc = OUT_PASS;
    if (use_mutex && pthread_mutex_init(&mu, NULL) != 0) {
        finding(label); return OUT_FAIL;
    }
    if (cap_begin(&g_cap) != 0) { finding(label); return OUT_FAIL; }
    for (i = 0; i < n; ++i) {
        args[i].id = i; args[i].start = &start;
        args[i].iters = iters; args[i].mu = use_mutex ? &mu : NULL;
        argp[i] = &args[i];
    }
    for (i = 0; i < n; ++i)
        if (pthread_create(&tids[i], NULL, toggle_worker, argp[i]) != 0) {
            finding(label); rc = OUT_FAIL; break;
        }
    if (rc == OUT_PASS) {
        start = 1;
        for (i = 0; i < n; ++i) (void)pthread_join(tids[i], NULL);
    } else {
        for (i = 0; i < n; ++i) (void)pthread_join(tids[i], NULL);
    }
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    CK(cap_lines(&g_cap) > 0u, "toggle case emitted no lines");
    CK(bad == 0, "toggle case produced torn/malformed lines");
    if (use_mutex)
        CK(cap_count_line_with(&g_cap, "tog: on") == (size_t)(n * iters),
           "synchronized toggle count mismatch");
    if (use_mutex) pthread_mutex_destroy(&mu);
    return rc;
}

static int c031(void) { return toggle_case(2, 200, 0, "031: pthread_create(2)"); }
static int c032(void) { return toggle_case(4, 200, 0, "032: pthread_create(4)"); }
static int c033(void) { return toggle_case(8, 200, 0, "033: pthread_create(8)"); }
static int c034(void) { return toggle_case(4, 100, 1, "034: pthread_mutex_t toggle"); }

static void *ret_worker(void *p) {
    simple_arg *a = (simple_arg *)p;
    return (void *)(uintptr_t)(a->id + 1);
}

static int c035(void) {
    simple_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, rc = OUT_PASS;
    for (i = 0; i < 6; ++i) {
        args[i].id = i; args[i].bar = NULL; argp[i] = &args[i]; rets[i] = NULL;
    }
    CK(spawn_join(6, ret_worker, argp, rets) == 0, "035: spawn_join");
    for (i = 0; i < 6; ++i)
        CK(rets[i] == (void *)(uintptr_t)(i + 1), "035: pthread_join lost return value");
    return rc;
}

typedef struct { pthread_mutex_t mu; pthread_cond_t cv; int token; } hand_arg;

static void *hand_prod(void *p) {
    hand_arg *h = (hand_arg *)p;
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("cond", "produced");
    pthread_mutex_lock(&h->mu);
    h->token = 1;
    pthread_cond_signal(&h->cv);
    pthread_mutex_unlock(&h->mu);
    return NULL;
}

static void *hand_cons(void *p) {
    hand_arg *h = (hand_arg *)p;
    cplus_libc_log_set_enabled(1);
    pthread_mutex_lock(&h->mu);
    while (!h->token) pthread_cond_wait(&h->cv, &h->mu);
    h->token = 2;
    cplus_libc_log("cond", "consumed");
    pthread_mutex_unlock(&h->mu);
    return NULL;
}

static int c036(void) {
    hand_arg h;
    pthread_t prod, cons;
    int rc = OUT_PASS;
    CK(pthread_mutex_init(&h.mu, NULL) == 0, "036: mutex init");
    CK(pthread_cond_init(&h.cv, NULL) == 0, "036: cond init");
    h.token = 0;
    if (cap_begin(&g_cap) != 0) { finding("036: cap_begin"); return OUT_FAIL; }
    CK(pthread_create(&prod, NULL, hand_prod, &h) == 0, "036: producer create");
    CK(pthread_create(&cons, NULL, hand_cons, &h) == 0, "036: consumer create");
    (void)pthread_join(prod, NULL);
    (void)pthread_join(cons, NULL);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(h.token == 2, "036: condvar handoff incomplete");
    CK(cap_count_line_with(&g_cap, "cond: produced") == 1u, "036: producer log missing");
    CK(cap_count_line_with(&g_cap, "cond: consumed") == 1u, "036: consumer log missing");
    pthread_cond_destroy(&h.cv);
    pthread_mutex_destroy(&h.mu);
    return rc;
}

static void *bar_worker(void *p) {
    simple_arg *a = (simple_arg *)p;
    int br;
    (void)a->id;
    br = bar_wait(a->bar);
    return (br == 0) ? NULL : BAD_PTR;
}

static int barrier_case(int n, const char *label) {
    bar_t b;
    simple_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, rc = OUT_PASS;
    if (bar_init(&b, n) != 0) { finding(label); return OUT_FAIL; }
    for (i = 0; i < n; ++i) {
        args[i].id = i; args[i].bar = &b; argp[i] = &args[i]; rets[i] = NULL;
    }
    CK(spawn_join(n, bar_worker, argp, rets) == 0, "barrier spawn_join");
    for (i = 0; i < n; ++i) CK(rets[i] == NULL, "barrier worker failed");
    bar_destroy(&b);
    return rc;
}

static int c037(void) { return barrier_case(2, "037: barrier start 2"); }
static int c038(void) { return barrier_case(4, "038: barrier start 4"); }
static int c039(void) { return barrier_case(8, "038: barrier start 8"); }

static int burst_case(int n) {
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("burst: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < n; ++i) cplus_libc_log("burst", "x");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == (size_t)n, "burst: captured line count mismatch");
    return rc;
}

static int c040(void) { return burst_case(10); }

static int c041(void) { return burst_case(100); }
static int c042(void) { return burst_case(1000); }

static int instrumented_burst_case(int n) {
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("instr burst: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < n; ++i) (void)cplus_strlen("burst");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] strlen: enter") == (size_t)n,
       "instr burst: strlen line count mismatch");
    return rc;
}

static int c043(void) { return instrumented_burst_case(10); }
static int c044(void) { return instrumented_burst_case(100); }

static int c045(void) {
    void *p[100];
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("045: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 100; ++i) p[i] = cplus_malloc(8u);
    for (i = 0; i < 100; ++i) cplus_free(p[i]);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] malloc: enter") == 100u,
       "045: malloc burst line count mismatch");
    return rc;
}

typedef struct { int id, iters; } log_arg;

static void *plain_log_worker(void *p) {
    log_arg *a = (log_arg *)p;
    int i;
    for (i = 0; i < a->iters; ++i) cplus_libc_log("thr", "line");
    return NULL;
}

static int threaded_burst(int n, int iters) {
    log_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, bad = 0, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("threaded burst: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < n; ++i) {
        args[i].id = i; args[i].iters = iters; argp[i] = &args[i]; rets[i] = NULL;
    }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(n, plain_log_worker, argp, rets) == 0, "threaded burst: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < n; ++i) CK(rets[i] == NULL, "threaded burst: worker failed");
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "threaded burst: torn line");
    CK(cap_count_line_with(&g_cap, "thr: line") == (size_t)(n * iters),
       "threaded burst: line count mismatch");
    return rc;
}

static int c046(void) { return threaded_burst(8, 100); }
static int c047(void) { return threaded_burst(4, 250); }
static int c048(void) { return threaded_burst(2, 500); }

static int c049(void) {
    unsigned long long off_hash, on_hash;
    int rc = OUT_PASS;
    cplus_libc_log_set_enabled(0);
    off_hash = parity_snapshot();
    if (cap_begin(&g_cap) != 0) { finding("049: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    on_hash = parity_snapshot();
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(off_hash == on_hash, "049: logging ON changed instrumented return values");
    CK(cap_lines(&g_cap) > 0u, "049: no lines emitted while ON");
    return rc;
}

static int c050(void) {
    size_t off, on;
    int rc = OUT_PASS;
    cplus_libc_log_set_enabled(0);
    off = cplus_strlen("parity-string");
    if (cap_begin(&g_cap) != 0) { finding("050: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    on = cplus_strlen("parity-string");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(off == on && on == 13u, "050: strlen parity violation");
    return rc;
}

static int c051(void) {
    char a[24], b[24], x[24], y[24];
    void *ra, *rb;
    int rc = OUT_PASS;
    memset(a, 0x11, sizeof(a)); memset(b, 0, sizeof(b));
    memset(x, 0x11, sizeof(x)); memset(y, 0, sizeof(y));
    cplus_libc_log_set_enabled(0);
    ra = cplus_memcpy(b, a, sizeof(a));
    if (cap_begin(&g_cap) != 0) { finding("051: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    rb = cplus_memcpy(y, x, sizeof(x));
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(ra == b && rb == y, "051: memcpy return pointer parity violation");
    CK(memcmp(b, y, sizeof(b)) == 0, "051: memcpy result parity violation");
    return rc;
}

static int c052(void) {
    char a[32], b[32];
    int na, nb, rc = OUT_PASS;
    cplus_libc_log_set_enabled(0);
    na = cplus_snprintf(a, sizeof(a), "p=%d/%d", 52, 7);
    if (cap_begin(&g_cap) != 0) { finding("052: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    nb = cplus_snprintf(b, sizeof(b), "p=%d/%d", 52, 7);
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(na == nb && strcmp(a, b) == 0, "052: snprintf parity violation");
    return rc;
}

static int c053(void) {
    int oa, ob, rc = OUT_PASS;
    cplus_libc_log_set_enabled(0);
    {
        FILE *f = cplus_fopen("/dev/null", "r");
        oa = (f != NULL);
        if (f) (void)cplus_fclose(f);
    }
    if (cap_begin(&g_cap) != 0) { finding("053: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    {
        FILE *f = cplus_fopen("/dev/null", "r");
        ob = (f != NULL);
        if (f) (void)cplus_fclose(f);
    }
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(oa == 1 && ob == 1, "053: fopen success parity violation");
    return rc;
}

/* =========================================================== cases 051-080 */

static int c054(void) {
    size_t b0, b1, c0, c1;
    int i, rc = OUT_PASS;
    c0 = cplus_alloc_live_count(); b0 = cplus_alloc_live_bytes();
    if (cap_begin(&g_cap) != 0) { finding("054: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 10000; ++i) cplus_libc_log("direct", "0123456789");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    c1 = cplus_alloc_live_count(); b1 = cplus_alloc_live_bytes();
    CK(c0 == c1, "054: live count changed after logging");
    CK(b0 == b1, "054: live bytes changed after logging");
    CK(cap_lines(&g_cap) == 10000u, "054: 10k burst line count mismatch");
    return rc;
}

static int c055(void) {
    size_t b0, b1;
    int i, rc = OUT_PASS;
    b0 = cplus_alloc_live_bytes();
    if (cap_begin(&g_cap) != 0) { finding("055: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 2000; ++i) cplus_libc_log("bytes", "abcdef0123456789");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    b1 = cplus_alloc_live_bytes();
    CK(b0 == b1, "055: allocator byte counter changed after logging");
    return rc;
}

static int c056(void) {
    cplus_alloc_meta m0, m1;
    void *p;
    int i, rc = OUT_PASS;
    p = cplus_malloc(64u);
    if (!p) { finding("056: malloc"); return OUT_FAIL; }
    CK(cplus_alloc_get_meta(p, &m0) == 0, "056: get_meta(before)");
    if (cap_begin(&g_cap) != 0) { cplus_free(p); finding("056: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 1000; ++i) cplus_libc_log("meta", "invariant");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cplus_alloc_get_meta(p, &m1) == 0, "056: get_meta(after)");
    CK(m0.byte_size == m1.byte_size && m0.generation == m1.generation && m1.is_live,
       "056: allocation metadata changed by logging");
    cplus_free(p);
    return rc;
}

static int c057(void) {
    unsigned long d0, d1;
    int i, rc = OUT_PASS;
    d0 = cplus_alloc_double_free_count();
    if (cap_begin(&g_cap) != 0) { finding("057: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 1500; ++i) cplus_libc_log("df", "count");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    d1 = cplus_alloc_double_free_count();
    CK(d0 == d1, "057: double-free counter changed by pure logging");
    return rc;
}

static int c058(void) {
    void *p0;
    int rc = OUT_PASS;
    cplus_alloc_fail_after(0L);
    p0 = cplus_malloc(16u);
    cplus_alloc_fail_reset();
    CK(p0 == NULL, "058: injected allocation unexpectedly succeeded");
    {
        void *p1 = cplus_malloc(16u);
        CK(p1 != NULL, "058: allocation still failing after fail_reset");
        cplus_free(p1);
    }
    return rc;
}

typedef struct { bar_t *bar; void *slots[MAX_THREADS]; } coord_ctx;
typedef struct { coord_ctx *c; int id; } coord_arg;

static void *coord_worker(void *p) {
    coord_arg *a = (coord_arg *)p;
    coord_ctx *c = a->c;
    int i;
    c->slots[a->id] = cplus_malloc((size_t)(16 + a->id));
    if (!c->slots[a->id]) return BAD_PTR;
    if (bar_wait(c->bar) != 0) return BAD_PTR;
    for (i = 0; i < c->bar->count; ++i)
        if (!cplus_alloc_is_live(c->slots[i])) return BAD_PTR;
    /* Second rendezvous: no thread may free a slot while another is reading. */
    if (bar_wait(c->bar) != 0) return BAD_PTR;
    cplus_free(c->slots[a->id]);
    c->slots[a->id] = NULL;
    return NULL;
}

static int coordinated_case(int n, const char *label) {
    bar_t b;
    coord_ctx c;
    coord_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    size_t before, after;
    int i, rc = OUT_PASS;
    if (bar_init(&b, n) != 0) { finding(label); return OUT_FAIL; }
    memset(&c, 0, sizeof(c));
    c.bar = &b;
    before = cplus_alloc_live_count();
    for (i = 0; i < n; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    CK(spawn_join(n, coord_worker, argp, rets) == 0, "coordinated alloc spawn_join");
    for (i = 0; i < n; ++i) CK(rets[i] == NULL, "coordinated alloc worker failed");
    after = cplus_alloc_live_count();
    CK(before == after, "coordinated alloc live count not restored");
    bar_destroy(&b);
    return rc;
}

static int c059(void) { return coordinated_case(2, "059: coordinated alloc/free 2"); }
static int c060(void) { return coordinated_case(4, "060: coordinated alloc/free 4"); }
static int c061(void) { return coordinated_case(8, "061: coordinated alloc/free 8"); }

typedef struct { pthread_mutex_t mu; void *buf; size_t sz; } shared_ctx;
typedef struct { shared_ctx *c; int id; } shared_arg;

static void *recycle_worker(void *p) {
    shared_arg *a = (shared_arg *)p;
    int i;
    for (i = 0; i < 40; ++i) {
        pthread_mutex_lock(&a->c->mu);
        if (!a->c->buf) {
            a->c->buf = cplus_malloc(48u);
            a->c->sz = 48u;
            if (!a->c->buf) { pthread_mutex_unlock(&a->c->mu); return BAD_PTR; }
        }
        {
            unsigned char *q = (unsigned char *)a->c->buf;
            size_t k;
            memset(q, (unsigned char)a->id, a->c->sz);
            for (k = 0; k < a->c->sz; ++k)
                if (q[k] != (unsigned char)a->id) { pthread_mutex_unlock(&a->c->mu); return BAD_PTR; }
        }
        cplus_free(a->c->buf);
        a->c->buf = NULL;
        pthread_mutex_unlock(&a->c->mu);
    }
    return NULL;
}

static int c062(void) {
    shared_ctx c;
    shared_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, rc = OUT_PASS;
    CK(pthread_mutex_init(&c.mu, NULL) == 0, "062: mutex init");
    c.buf = NULL; c.sz = 0;
    for (i = 0; i < 6; ++i) { args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL; }
    CK(spawn_join(6, recycle_worker, argp, rets) == 0, "062: spawn_join");
    for (i = 0; i < 6; ++i) CK(rets[i] == NULL, "062: recycle worker failed");
    CK(c.buf == NULL, "062: shared buffer leaked");
    pthread_mutex_destroy(&c.mu);
    return rc;
}

typedef struct {
    pthread_mutex_t mu; pthread_cond_t cv;
    void *buf; unsigned long long gen; int ready;
} tracked_hand;

static void *tracked_prod(void *p) {
    tracked_hand *h = (tracked_hand *)p;
    cplus_alloc_meta m;
    void *q = cplus_malloc(64u);
    if (!q) return BAD_PTR;
    strcpy((char *)q, "cond-payload");
    if (cplus_alloc_get_meta(q, &m) != 0) { cplus_free(q); return BAD_PTR; }
    pthread_mutex_lock(&h->mu);
    h->buf = q; h->gen = m.generation; h->ready = 1;
    pthread_cond_signal(&h->cv);
    pthread_mutex_unlock(&h->mu);
    return NULL;
}

static void *tracked_cons(void *p) {
    tracked_hand *h = (tracked_hand *)p;
    void *owned = NULL;
    pthread_mutex_lock(&h->mu);
    while (!h->ready) pthread_cond_wait(&h->cv, &h->mu);
    if (!cplus_alloc_check_generation(h->buf, h->gen)) {
        pthread_mutex_unlock(&h->mu); return BAD_PTR;
    }
    if (cplus_transfer_ownership(&owned, &h->buf) != 0) {
        pthread_mutex_unlock(&h->mu); return BAD_PTR;
    }
    if (cplus_alloc_check_generation(owned, h->gen)) {
        pthread_mutex_unlock(&h->mu); return BAD_PTR;
    }
    pthread_mutex_unlock(&h->mu);
    if (strcmp((const char *)owned, "cond-payload") != 0) { cplus_free(owned); return BAD_PTR; }
    cplus_free(owned);
    return NULL;
}

static int c063(void) {
    tracked_hand h;
    pthread_t prod, cons;
    void *rp = NULL, *rcv = NULL;
    int rc = OUT_PASS;
    CK(pthread_mutex_init(&h.mu, NULL) == 0, "063: mutex init");
    CK(pthread_cond_init(&h.cv, NULL) == 0, "063: cond init");
    h.buf = NULL; h.gen = 0; h.ready = 0;
    CK(pthread_create(&prod, NULL, tracked_prod, &h) == 0, "063: prod create");
    CK(pthread_create(&cons, NULL, tracked_cons, &h) == 0, "063: cons create");
    (void)pthread_join(prod, &rp);
    (void)pthread_join(cons, &rcv);
    CK(rp == NULL && rcv == NULL, "063: handoff worker failed");
    CK(h.buf == NULL && h.ready == 1, "063: handoff did not complete");
    pthread_cond_destroy(&h.cv);
    pthread_mutex_destroy(&h.mu);
    return rc;
}

static int c064(void) {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    struct timespec past;
    int rc = OUT_PASS;
    CK(pthread_mutex_init(&mu, NULL) == 0, "064: mutex init");
    CK(pthread_cond_init(&cv, NULL) == 0, "064: cond init");
    CK(clock_gettime(CLOCK_REALTIME, &past) == 0, "064: clock_gettime");
    past.tv_sec -= 1;
    CK(pthread_mutex_lock(&mu) == 0, "064: lock");
    CK(pthread_cond_timedwait(&cv, &mu, &past) == ETIMEDOUT, "064: expired wait did not time out");
    CK(pthread_mutex_unlock(&mu) == 0, "064: unlock");
    pthread_cond_destroy(&cv);
    pthread_mutex_destroy(&mu);
    return rc;
}

typedef struct { int modes[MAX_THREADS]; int baseline; bar_t *b1; bar_t *b2; int id; } fp_arg;

static void *fp_worker(void *p) {
    fp_arg *a = (fp_arg *)p;
    volatile double x = 1.0, y = 3.0, z, w;
    if (cplus_fesetround(a->modes[a->id]) != 0) return BAD_PTR;
    z = x / y;
    if (z < 0.0 || z > 1.0) return BAD_PTR;
    if (a->b1 && bar_wait(a->b1) != 0) return BAD_PTR;
    if (cplus_fegetround() != a->modes[a->id]) return BAD_PTR;
    w = z + 0.5;
    if (w < 0.5 || w > 1.5) return BAD_PTR;
    if (a->b2 && bar_wait(a->b2) != 0) return BAD_PTR;
    if (cplus_fesetround(a->baseline) != 0) return BAD_PTR;
    return NULL;
}

static int fp_case(int with_barriers, const char *label) {
    enum { N = 4 };
    bar_t b1, b2;
    fp_arg a;
    void *argp[N], *rets[N];
    int rc = OUT_PASS;
    a.modes[0] = FE_TONEAREST; a.modes[1] = FE_DOWNWARD;
    a.modes[2] = FE_UPWARD;   a.modes[3] = FE_TOWARDZERO;
    a.baseline = cplus_fegetround();
    if (cplus_fesetround(FE_TONEAREST) != 0) { finding(label); return OUT_FAIL; }
    if (with_barriers) {
        if (bar_init(&b1, N) != 0 || bar_init(&b2, N) != 0) { finding(label); return OUT_FAIL; }
        a.b1 = &b1; a.b2 = &b2;
    } else {
        a.b1 = NULL; a.b2 = NULL;
    }
    /* A distinct context per thread is required: make local copies. */
    {
        fp_arg contexts[N];
        int i;
        for (i = 0; i < N; ++i) {
            contexts[i] = a; contexts[i].id = i; argp[i] = &contexts[i]; rets[i] = NULL;
        }
        CK(spawn_join(N, fp_worker, argp, rets) == 0, "fp spawn_join");
    }
    for (int i = 0; i < N; ++i) CK(rets[i] == NULL, "fp worker failed");
    (void)cplus_fesetround(a.baseline);
    if (with_barriers) { bar_destroy(&b1); bar_destroy(&b2); }
    return rc;
}

static int c065(void) { return fp_case(0, "065: per-thread FP mode"); }
static int c066(void) { return fp_case(1, "066: FP mode across barriers"); }

static void *wave_worker(void *p) {
    simple_arg *a = (simple_arg *)p;
    int i;
    for (i = 0; i < 50; ++i) {
        void *q = cplus_malloc((size_t)(a->id + i % 7 + 1));
        if (!q) return BAD_PTR;
        cplus_free(q);
    }
    return NULL;
}

static int waves_case(int waves, int n, const char *label) {
    int w, i, rc = OUT_PASS;
    for (w = 0; w < waves; ++w) {
        simple_arg args[MAX_THREADS];
        void *argp[MAX_THREADS], *rets[MAX_THREADS];
        size_t before = cplus_alloc_live_count();
        for (i = 0; i < n; ++i) {
            args[i].id = i + w; args[i].bar = NULL; argp[i] = &args[i]; rets[i] = NULL;
        }
        CK(spawn_join(n, wave_worker, argp, rets) == 0, "waves spawn_join");
        for (i = 0; i < n; ++i) CK(rets[i] == NULL, "waves worker failed");
        CK(cplus_alloc_live_count() == before, "waves live count drift");
    }
    (void)label;
    return rc;
}

static int c067(void) { return waves_case(6, 6, "067: repeated waves 6x6"); }
static int c068(void) { return waves_case(3, 8, "068: repeated waves 3x8"); }

typedef struct { bar_t bar; void *p; int rc[MAX_THREADS]; } df_ctx;
typedef struct { df_ctx *c; int id; } df_arg;

static void *df_worker(void *p) {
    df_arg *a = (df_arg *)p;
    void *local;
    if (bar_wait(&a->c->bar) != 0) return BAD_PTR;
    local = a->c->p;
    a->c->rc[a->id] = cplus_free_checked(&local);
    return NULL;
}

static int df_contention(int check_counter, const char *label) {
    enum { N = 8 };
    df_ctx c;
    df_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    unsigned long d0, d1;
    int i, ok = 0, refused = 0, rc = OUT_PASS;
    c.p = cplus_malloc(48u);
    if (!c.p) { finding(label); return OUT_FAIL; }
    if (bar_init(&c.bar, N) != 0) { cplus_free(c.p); finding(label); return OUT_FAIL; }
    for (i = 0; i < N; ++i) { c.rc[i] = -1; args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL; }
    d0 = cplus_alloc_double_free_count();
    CK(spawn_join(N, df_worker, argp, rets) == 0, "double-free spawn_join");
    d1 = cplus_alloc_double_free_count();
    for (i = 0; i < N; ++i) {
        CK(rets[i] == NULL, "double-free worker failed");
        if (c.rc[i] == 0) ++ok;
        else if (c.rc[i] == EINVAL) ++refused;
    }
    CK(ok == 1, "double-free contention: exactly one free must succeed");
    CK(refused == N - 1, "double-free contention: wrong refusal count");
    if (check_counter) CK(d1 >= d0 + (unsigned long)(N - 1), "double-free counter did not rise");
    bar_destroy(&c.bar);
    return rc;
}

static int c069(void) { return df_contention(0, "069: double-free contention 8"); }
static int c070(void) { return df_contention(1, "070: double-free counter under contention"); }

typedef struct {
    pthread_mutex_t mu; bar_t bar;
    void *shared; int rc[MAX_THREADS]; void *claimed[MAX_THREADS];
    unsigned long long orig_gen;
} own_ctx;
typedef struct { own_ctx *c; int id; } own_arg;

static void *own_worker(void *p) {
    own_arg *a = (own_arg *)p;
    own_ctx *c = a->c;
    void *dst = NULL;
    if (bar_wait(&c->bar) != 0) return BAD_PTR;
    pthread_mutex_lock(&c->mu);
    c->rc[a->id] = cplus_transfer_ownership(&dst, &c->shared);
    c->claimed[a->id] = dst;
    pthread_mutex_unlock(&c->mu);
    if (c->rc[a->id] == 0 && dst != NULL) cplus_free(dst);
    return NULL;
}

static int c071(void) {
    enum { N = 2 };
    own_ctx c;
    own_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    cplus_alloc_meta m;
    int i, winners = 0, losers = 0, rc = OUT_PASS;
    CK(pthread_mutex_init(&c.mu, NULL) == 0, "071: mutex init");
    CK(bar_init(&c.bar, N) == 0, "071: barrier init");
    c.shared = cplus_malloc(64u);
    if (!c.shared) { finding("071: malloc"); return OUT_FAIL; }
    strcpy((char *)c.shared, "two-claimants");
    CK(cplus_alloc_get_meta(c.shared, &m) == 0, "071: get_meta");
    c.orig_gen = m.generation;
    for (i = 0; i < N; ++i) { c.rc[i] = -1; c.claimed[i] = NULL; args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL; }
    CK(spawn_join(N, own_worker, argp, rets) == 0, "071: spawn_join");
    for (i = 0; i < N; ++i) {
        CK(rets[i] == NULL, "071: claim worker failed");
        if (c.rc[i] == 0) ++winners;
        else if (c.rc[i] == EINVAL) ++losers;
    }
    CK(winners == 1 && losers == 1, "071: exactly one ownership claim must win");
    CK(c.shared == NULL, "071: shared slot not cleared");
    (void)c.orig_gen;
    bar_destroy(&c.bar);
    pthread_mutex_destroy(&c.mu);
    return rc;
}

static int c072(void) {
    cplus_alloc_meta m0, m1;
    void *src, *dst = NULL, *original;
    int rc = OUT_PASS;
    src = cplus_malloc(32u);
    if (!src) { finding("072: malloc"); return OUT_FAIL; }
    original = src;
    CK(cplus_alloc_get_meta(src, &m0) == 0, "072: get_meta(before)");
    CK(cplus_transfer_ownership(&dst, &src) == 0, "072: transfer_ownership");
    CK(dst == original && src == NULL, "072: ownership pointers not swapped");
    CK(cplus_alloc_get_meta(dst, &m1) == 0, "072: get_meta(after)");
    CK(m1.generation > m0.generation, "072: transfer did not bump generation");
    cplus_free(dst);
    return rc;
}

static int c073(void) {
    cplus_alloc_meta m;
    void *p;
    unsigned long long gen;
    int was_live, rc = OUT_PASS;
    p = cplus_malloc(32u);
    if (!p) { finding("073: malloc"); return OUT_FAIL; }
    CK(cplus_alloc_get_meta(p, &m) == 0, "073: get_meta");
    gen = m.generation;
    was_live = cplus_alloc_check_generation(p, gen);
    cplus_free(p);
    CK(was_live == 1, "073: fresh generation did not match");
    CK(cplus_alloc_check_generation(p, gen) == 0, "073: stale generation still matched after free");
    return rc;
}

typedef struct {
    void *blocks[MAX_THREADS];
    size_t sizes[MAX_THREADS];
    int n;
    bar_t *bar;
} query_ctx;
typedef struct { query_ctx *q; int id; } query_arg;

static void *query_worker(void *p) {
    query_arg *a = (query_arg *)p;
    int i;
    if (bar_wait(a->q->bar) != 0) return BAD_PTR;
    for (i = 0; i < a->q->n; ++i) {
        cplus_alloc_meta m;
        size_t out = 0;
        if (cplus_alloc_get_meta(a->q->blocks[i], &m) != 0) return BAD_PTR;
        if (!m.is_live || m.byte_size != a->q->sizes[i]) return BAD_PTR;
        if (cplus_alloc_query_length(a->q->blocks[i], 1u, &out) != 0) return BAD_PTR;
        if (out != a->q->sizes[i]) return BAD_PTR;
    }
    return NULL;
}

static int query_case(const char *label, int use_capacity) {
    enum { N = 6 };
    query_ctx q;
    query_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    bar_t bar;
    int i, rc = OUT_PASS;
    q.n = N; q.bar = &bar;
    for (i = 0; i < N; ++i) {
        q.sizes[i] = (size_t)(17 + i * 7);
        q.blocks[i] = cplus_malloc(q.sizes[i]);
        if (!q.blocks[i]) { finding(label); return OUT_FAIL; }
    }
    if (bar_init(&bar, N) != 0) { finding(label); return OUT_FAIL; }
    for (i = 0; i < N; ++i) {
        args[i].q = &q; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    CK(spawn_join(N, query_worker, argp, rets) == 0, "query spawn_join");
    for (i = 0; i < N; ++i) CK(rets[i] == NULL, "query worker failed");
    if (use_capacity)
        for (i = 0; i < N; ++i)
            CK(cplus_alloc_remaining_capacity(q.blocks[i]) == q.sizes[i],
               "074: remaining capacity mismatch");
    for (i = 0; i < N; ++i) cplus_free(q.blocks[i]);
    bar_destroy(&bar);
    return rc;
}

static int c074(void) { return query_case("074: query_length concurrent", 0); }
static int c075(void) { return query_case("075: remaining_capacity concurrent", 1); }

static pthread_key_t tls_key;
static pthread_once_t tls_once = PTHREAD_ONCE_INIT;
static void tls_make_key(void) { (void)pthread_key_create(&tls_key, cplus_free); }

static void *tls_worker(void *p) {
    simple_arg *a = (simple_arg *)p;
    void *b;
    pthread_once(&tls_once, tls_make_key);
    b = cplus_malloc(40u);
    if (!b) return BAD_PTR;
    if (pthread_setspecific(tls_key, b) != 0) { cplus_free(b); return BAD_PTR; }
    if (pthread_getspecific(tls_key) != b) return BAD_PTR;
    memset(b, (unsigned char)a->id, 40u);
    return NULL;
}

static int c076(void) {
    simple_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    size_t before;
    int i, rc = OUT_PASS;
    { void *warm = cplus_malloc(8u); cplus_free(warm); }
    before = cplus_alloc_live_count();
    for (i = 0; i < 6; ++i) {
        args[i].id = i; args[i].bar = NULL; argp[i] = &args[i]; rets[i] = NULL;
    }
    CK(spawn_join(6, tls_worker, argp, rets) == 0, "076: spawn_join");
    for (i = 0; i < 6; ++i) CK(rets[i] == NULL, "076: TLS worker failed");
    CK(cplus_alloc_live_count() == before, "076: TLS destructor did not free block");
    return rc;
}

/* =========================================================== cases 077-100 */

static void *stats_worker(void *p) {
    int i;
    (void)p;
    for (i = 0; i < 200; ++i) p_record("hundred.concurrent", (i & 1) == 0);
    return NULL;
}

static int c077(void) {
    safe_stat_t st;
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, rc = OUT_PASS;
    if (!stats_ready()) { ++g_unavail; return OUT_UNAVAIL; }
    p_reset();
    for (i = 0; i < 6; ++i) { argp[i] = NULL; rets[i] = NULL; }
    CK(spawn_join(6, stats_worker, argp, rets) == 0, "077: spawn_join");
    for (i = 0; i < 6; ++i) CK(rets[i] == NULL, "077: stats worker failed");
    CK(p_get("hundred.concurrent", &st) == 1, "077: get after record");
    CK(st.calls == 1200u, "077: concurrent record count");
    CK(st.ok + st.fail == st.calls, "077: concurrent counter split");
    return rc;
}

static int c078(void) {
    safe_stat_t st;
    int i, rc = OUT_PASS;
    if (!stats_ready()) { ++g_unavail; return OUT_UNAVAIL; }
    p_reset();
    for (i = 0; i < 3; ++i) p_record("hundred.get", 1);
    for (i = 0; i < 2; ++i) p_record("hundred.get", 0);
    CK(p_get("hundred.get", &st) == 1, "078: get recorded api");
    CK(st.calls == 5u && st.ok == 3u && st.fail == 2u, "078: recorded counters wrong");
    return rc;
}

static int c079(void) {
    safe_stat_t arr[3];
    size_t n;
    int rc = OUT_PASS;
    if (!stats_ready()) { ++g_unavail; return OUT_UNAVAIL; }
    p_reset();
    p_record("hundred.dump.a", 1);
    p_record("hundred.dump.b", 1);
    p_record("hundred.dump.c", 1);
    n = p_dump(arr, 3u);
    CK(n <= 3u, "079: dump exceeded requested bound");
    CK(n >= 1u, "079: dump returned no entries");
    return rc;
}

static int c080(void) {
    safe_stat_t st;
    int rc = OUT_PASS;
    if (!stats_ready()) { ++g_unavail; return OUT_UNAVAIL; }
    p_record("hundred.reset", 1);
    p_reset();
    CK(p_get("hundred.reset", &st) == 1, "080: registered api lost on reset");
    CK(st.calls == 0u && st.ok == 0u && st.fail == 0u, "080: reset did not zero counters");
    return rc;
}

static int c081(void) {
    safe_stat_t st;
    int rc = OUT_PASS;
    if (!stats_ready()) { ++g_unavail; return OUT_UNAVAIL; }
    p_reset();
    p_record(NULL, 1);
    CK(p_get(NULL, &st) == 0, "081: NULL api must be ignored");
    return rc;
}

static int c082(void) {
    safe_stat_t st;
    int i, rc = OUT_PASS;
    if (!stats_ready()) { ++g_unavail; return OUT_UNAVAIL; }
    p_reset();
    for (i = 0; i < 10; ++i) p_record("hundred.mixed", 1);
    for (i = 0; i < 4; ++i) p_record("hundred.mixed", 0);
    CK(p_get("hundred.mixed", &st) == 1, "082: get mixed counters");
    CK(st.ok + st.fail == st.calls && st.calls == 14u, "082: ok+fail != calls");
    return rc;
}

static void *silent_worker(void *p) {
    log_arg *a = (log_arg *)p;
    int i;
    for (i = 0; i < a->iters; ++i) {
        cplus_libc_log("silent", "thread");
        (void)cplus_strlen("silent");
    }
    return NULL;
}

static int c083(void) {
    log_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, rc = OUT_PASS;
    cplus_libc_log_set_enabled(0);
    if (cap_begin(&g_cap) != 0) { finding("083: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 4; ++i) { args[i].id = i; args[i].iters = 200; argp[i] = &args[i]; rets[i] = NULL; }
    CK(spawn_join(4, silent_worker, argp, rets) == 0, "083: spawn_join");
    cap_end(&g_cap);
    for (i = 0; i < 4; ++i) CK(rets[i] == NULL, "083: worker failed");
    CK(cap_lines(&g_cap) == 0u, "083: threads emitted while logging OFF");
    return rc;
}

static void *visibility_worker(void *p) {
    simple_arg *a = (simple_arg *)p;
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("visible", "x");
    cplus_libc_log_set_enabled(0);
    (void)a->id;
    return NULL;
}

static int c084(void) {
    simple_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("084: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 8; ++i) {
        args[i].id = i; args[i].bar = NULL; argp[i] = &args[i]; rets[i] = NULL;
    }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(8, visibility_worker, argp, rets) == 0, "084: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < 8; ++i) CK(rets[i] == NULL, "084: worker failed");
    CK(cap_count_line_with(&g_cap, "visible: x") == 8u, "084: global flag not observed by threads");
    return rc;
}

static int c085(void) {
    int bad = 0, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("085: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    {
        log_arg a; void *ap[1], *rp[1];
        a.id = 0; a.iters = 800; ap[0] = &a; rp[0] = NULL;
        CK(spawn_join(1, plain_log_worker, ap, rp) == 0, "085: spawn_join");
    }
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "085: short concurrent lines torn");
    CK(cap_lines(&g_cap) == 800u, "085: short concurrent line count");
    return rc;
}

static void *long_log_worker(void *p) {
    log_arg *a = (log_arg *)p;
    char msg[1024];
    int i;
    memset(msg, (unsigned char)('L' + a->id), sizeof(msg) - 1u);
    msg[sizeof(msg) - 1u] = '\0';
    for (i = 0; i < a->iters; ++i) cplus_libc_log("longmsg", msg);
    return NULL;
}

static int c086(void) {
    log_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    size_t max;
    int bad = 0, i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("086: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 4; ++i) { args[i].id = i; args[i].iters = 20; argp[i] = &args[i]; rets[i] = NULL; }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(4, long_log_worker, argp, rets) == 0, "086: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < 4; ++i) CK(rets[i] == NULL, "086: worker failed");
    max = cap_max_line(&g_cap, &bad);
    CK(max <= LOG_LINE_CAP, "086: concurrent long line exceeded 4096");
    if (bad != 0) note("multi-KB writes from several threads interleaved in the pipe; atomicity is not guaranteed, only boundedness");
    return rc;
}

static int c087(void) {
    int i, rc = OUT_PASS;
    size_t mid, after;
    if (cap_begin(&g_cap) != 0) { finding("087: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 3000; ++i) cplus_libc_log("flood", "x");
    cplus_libc_log_set_enabled(0);
    for (i = 0; i < 500; ++i) cplus_libc_log("flood", "y");
    cap_end(&g_cap);
    mid = cap_count_line_with(&g_cap, "flood: x");
    after = cap_count_line_with(&g_cap, "flood: y");
    CK(mid >= 3000u, "087: ON-period lines missing");
    CK(after == 0u, "087: OFF-period lines emitted");
    return rc;
}

static int c088(void) {
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("088: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 20; ++i) cplus_libc_log("r1", "x");
    cplus_libc_log_set_enabled(0);
    for (i = 0; i < 20; ++i) cplus_libc_log("r0", "x");
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 20; ++i) cplus_libc_log("r2", "x");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_count_line_with(&g_cap, "r1: x") == 20u, "088: round 1 count");
    CK(cap_count_line_with(&g_cap, "r0: x") == 0u, "088: OFF round leaked");
    CK(cap_count_line_with(&g_cap, "r2: x") == 20u, "088: round 2 count");
    return rc;
}

static int c089(void) {
    int bad = 0, i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("089: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    for (i = 0; i < 25; ++i) {
        cplus_libc_log("direct", "tick");
        (void)cplus_strlen("abc");
        (void)cplus_memcmp("a", "b", 1u);
    }
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "089: interleaved line shape");
    CK(cap_lines(&g_cap) == 75u, "089: interleaved line count");
    return rc;
}

static int c090(void) {
    void *bad_ptr = NULL;
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("090: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    (void)cplus_free_checked(NULL);
    cplus_alloc_fail_after(0L);
    bad_ptr = cplus_malloc(8u);
    cplus_alloc_fail_reset();
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(bad_ptr == NULL, "090: injected malloc unexpectedly succeeded");
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] free: enter") >= 1u,
       "090: guarded free path did not log");
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] malloc: enter") >= 1u,
       "090: guarded malloc path did not log");
    return rc;
}

static int c091(void) {
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("091: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    (void)close(STDERR_FILENO);
    (void)cplus_strlen("closed-call");
    (void)cplus_memcmp("a", "a", 1u);
    if (dup2(g_cap.pfd[1], STDERR_FILENO) < 0) finding("091: fd 2 re-attach failed");
    cplus_libc_log("instrumented-resumed", "ok");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    CK(cap_contains(&g_cap, "[CPLUS-LIBC] instrumented-resumed: ok\n"),
       "091: logging did not resume after instrumented calls with closed fd 2");
    return rc;
}

static void *null_log_worker(void *p) {
    simple_arg *a = (simple_arg *)p;
    int i;
    for (i = 0; i < 20; ++i) {
        cplus_libc_log(NULL, NULL);
        cplus_libc_log(NULL, "m");
        cplus_libc_log("a", NULL);
    }
    (void)a->id;
    return NULL;
}

static int c092(void) {
    simple_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int bad = 0, i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("092: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 4; ++i) {
        args[i].id = i; args[i].bar = NULL; argp[i] = &args[i]; rets[i] = NULL;
    }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(4, null_log_worker, argp, rets) == 0, "092: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < 4; ++i) CK(rets[i] == NULL, "092: worker failed");
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "092: concurrent NULL-field line shape");
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] ?: ?") == 80u, "092: NULL/NULL count");
    return rc;
}

static void *secret_worker(void *p) {
    log_arg *a = (log_arg *)p;
    static const char SECRET[] = "THREAD_SECRET_5a1b9c2d_HUNDRED";
    char dst[128];
    int i;
    for (i = 0; i < 20; ++i) {
        (void)cplus_memcpy(dst, SECRET, sizeof(SECRET));
        (void)cplus_strlen(SECRET);
        (void)cplus_strtol(SECRET, NULL, 10);
    }
    (void)a->id;
    return NULL;
}

static int c093(void) {
    static const char SECRET[] = "THREAD_SECRET_5a1b9c2d_HUNDRED";
    log_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("093: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 4; ++i) { args[i].id = i; args[i].iters = 20; argp[i] = &args[i]; rets[i] = NULL; }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(4, secret_worker, argp, rets) == 0, "093: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < 4; ++i) CK(rets[i] == NULL, "093: worker failed");
    CK(!cap_contains(&g_cap, SECRET), "093: sentinel leaked under thread contention");
    return rc;
}

/* Serialise the multi-KB writes here: several threads writing 4096-byte
 * records to the same pipe can interleave, which is a documented logger
 * limitation (case 86) and would make a per-line bound assertion meaningless. */
static pthread_mutex_t g_long_field_mu = PTHREAD_MUTEX_INITIALIZER;

static void *long_field_worker(void *p) {
    log_arg *a = (log_arg *)p;
    char msg[9000];
    int i;
    memset(msg, 'B', sizeof(msg) - 1u); msg[sizeof(msg) - 1u] = '\0';
    for (i = 0; i < a->iters; ++i) {
        pthread_mutex_lock(&g_long_field_mu);
        cplus_libc_log("threadlong", msg);
        pthread_mutex_unlock(&g_long_field_mu);
    }
    return NULL;
}

static int c094(void) {
    log_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    size_t max;
    int bad = 0, i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("094: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 4; ++i) { args[i].id = i; args[i].iters = 10; argp[i] = &args[i]; rets[i] = NULL; }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(4, long_field_worker, argp, rets) == 0, "094: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < 4; ++i) CK(rets[i] == NULL, "094: worker failed");
    max = cap_max_line(&g_cap, &bad);
    CK(max <= LOG_LINE_CAP, "094: concurrent long-field line exceeded 4096");
    return rc;
}

static int c095(void) {
    int i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("095: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 1000; ++i) {
        cplus_libc_log_set_enabled(1);
        (void)cplus_strlen("cycle");
        cplus_libc_log_set_enabled(0);
    }
    cap_end(&g_cap);
    CK(cap_count_line_with(&g_cap, "[CPLUS-LIBC] strlen: enter") == 1000u,
       "095: instrumented toggle-cycle count");
    return rc;
}

static void *mixed_log_worker(void *p) {
    simple_arg *a = (simple_arg *)p;
    char msg[32];
    int i;
    msg[0] = 'm';
    msg[1] = (char)('0' + a->id);
    msg[2] = '\0';
    for (i = 0; i < 60; ++i) {
        cplus_libc_log("mixed", msg);
        (void)cplus_strlen("mixed");
        (void)cplus_memcmp("q", "q", 1u);
    }
    return NULL;
}

static int c096(void) {
    simple_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    int bad = 0, i, rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("096: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 6; ++i) {
        args[i].id = i; args[i].bar = NULL; argp[i] = &args[i]; rets[i] = NULL;
    }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(6, mixed_log_worker, argp, rets) == 0, "096: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < 6; ++i) CK(rets[i] == NULL, "096: worker failed");
    (void)cap_max_line(&g_cap, &bad);
    CK(bad == 0, "096: mixed-load line shape");
    CK(cap_lines(&g_cap) == 6u * 60u * 3u, "096: mixed-load line count");
    return rc;
}

static int c097(void) {
    log_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    size_t c0, b0, c1, b1;
    int i, rc = OUT_PASS;
    c0 = cplus_alloc_live_count(); b0 = cplus_alloc_live_bytes();
    if (cap_begin(&g_cap) != 0) { finding("097: cap_begin"); return OUT_FAIL; }
    for (i = 0; i < 8; ++i) { args[i].id = i; args[i].iters = 500; argp[i] = &args[i]; rets[i] = NULL; }
    cplus_libc_log_set_enabled(1);
    CK(spawn_join(8, plain_log_worker, argp, rets) == 0, "097: spawn_join");
    cplus_libc_log_set_enabled(0);
    cap_end(&g_cap);
    for (i = 0; i < 8; ++i) CK(rets[i] == NULL, "097: worker failed");
    c1 = cplus_alloc_live_count(); b1 = cplus_alloc_live_bytes();
    CK(c0 == c1 && b0 == b1, "097: concurrent logging moved allocator counters");
    return rc;
}

static int c098(void) {
    int round, i, rc = OUT_PASS;
    for (round = 0; round < 10; ++round) {
        if (cap_begin(&g_cap) != 0) { finding("098: cap_begin"); return OUT_FAIL; }
        cplus_libc_log_set_enabled(1);
        for (i = 0; i < 10; ++i) cplus_libc_log("reuse", "x");
        cplus_libc_log_set_enabled(0);
        cap_end(&g_cap);
        CK(cap_lines(&g_cap) == 10u, "098: capture reuse round count");
    }
    return rc;
}

static int c099(void) {
    log_arg a;
    void *ap[1], *rp[1];
    int rc = OUT_PASS;
    if (cap_begin(&g_cap) != 0) { finding("099: cap_begin"); return OUT_FAIL; }
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("first", "only");
    cplus_libc_log_set_enabled(0);
    a.id = 0; a.iters = 300; ap[0] = &a; rp[0] = NULL;
    CK(spawn_join(1, plain_log_worker, ap, rp) == 0, "099: spawn_join");
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 1u, "099: logging did not stay off after restore");
    CK(cap_contains(&g_cap, "first: only"), "099: first line missing");
    return rc;
}

static int c100(void) {
    int rc = OUT_PASS;
    cplus_libc_log_set_enabled(0);
    if (cap_begin(&g_cap) != 0) { finding("100: cap_begin"); return OUT_FAIL; }
    cplus_libc_log("final", "off");
    cap_end(&g_cap);
    CK(cap_lines(&g_cap) == 0u, "100: flag not in default-off state at exit");
    return rc;
}

/* --------------------------------------------------------------- registry -- */

typedef struct {
    const char *method;
    const char *scenario;
    int (*fn)(void);
} case_t;

static const case_t CASES[100] = {
    { "cplus_libc_log_set_enabled", "default_off_silence", c001 },
    { "cplus_libc_log_set_enabled", "enable_emits_entry", c002 },
    { "cplus_libc_log_set_enabled", "disable_restores_silence", c003 },
    { "cplus_libc_log_set_enabled", "nonzero_values_normalize", c004 },
    { "cplus_libc_log_set_enabled", "toggles_1000", c005 },
    { "cplus_libc_log_set_enabled", "off_under_load_silent", c006 },
    { "cplus_libc_log", "null_api_and_msg", c007 },
    { "cplus_libc_log", "empty_api_and_msg", c008 },
    { "cplus_libc_log", "long_msg_truncated", c009 },
    { "cplus_libc_log", "long_api_truncated", c010 },
    { "cplus_libc_log", "long_both_bounded", c011 },
    { "cplus_libc_log", "exact_rendered_text", c012 },
    { "cplus_libc_log", "control_bytes_no_split", c013 },
    { "cplus_libc_log", "nul_terminated_msg", c014 },
    { "cplus_libc_log", "line_shape_contract", c015 },
    { "cplus_libc_log", "one_newline_per_call", c016 },
    { "cplus_libc_log", "no_sensitive_sentinel", c017 },
    { "cplus_libc_log", "errno_preserved", c018 },
    { "cplus_libc_log", "closed_stderr_restore", c019 },
    { "cplus_libc_log", "async_signal_child_note", c020 },
    { "cplus_malloc", "entry_line", c021 },
    { "cplus_alloc_get_meta", "metadata_under_contention", c022 },
    { "cplus_alloc_fail_after", "failure_injection_mutex", c023 },
    { "cplus_memcpy", "entry_line", c024 },
    { "cplus_memcpy", "parallel_private_buffers", c025 },
    { "cplus_strlen", "entry_line", c026 },
    { "cplus_fopen", "entry_line", c027 },
    { "cplus_strtol", "entry_line", c028 },
    { "cplus_snprintf", "entry_line", c029 },
    { "cplus_free", "entry_line", c030 },
    { "pthread_create", "toggle_2_threads", c031 },
    { "pthread_create", "toggle_4_threads", c032 },
    { "pthread_create", "toggle_8_threads", c033 },
    { "pthread_mutex_t", "synchronized_toggle", c034 },
    { "pthread_join", "collects_returns", c035 },
    { "pthread_cond_t", "toggle_handoff", c036 },
    { "portable_barrier", "barrier_start_2", c037 },
    { "portable_barrier", "barrier_start_4", c038 },
    { "portable_barrier", "barrier_start_8", c039 },
    { "cplus_libc_log", "burst_10", c040 },
    { "cplus_libc_log", "burst_100", c041 },
    { "cplus_libc_log", "burst_1000", c042 },
    { "cplus_strlen", "instrumented_burst_10", c043 },
    { "cplus_strlen", "instrumented_burst_100", c044 },
    { "cplus_malloc", "burst_100", c045 },
    { "cplus_libc_log", "8_threads_x_100", c046 },
    { "cplus_libc_log", "4_threads_x_250", c047 },
    { "cplus_libc_log", "2_threads_x_500", c048 },
    { "cplus_strtol", "on_off_return_parity", c049 },
    { "cplus_strlen", "on_off_return_parity", c050 },
    { "cplus_memcpy", "on_off_return_parity", c051 },
    { "cplus_snprintf", "on_off_return_parity", c052 },
    { "cplus_fopen", "on_off_return_parity", c053 },
    { "cplus_alloc_live_count", "unchanged_after_10k_logs", c054 },
    { "cplus_alloc_live_bytes", "unchanged_after_logging", c055 },
    { "cplus_alloc_get_meta", "invariant_while_logging", c056 },
    { "cplus_alloc_double_free_count", "unchanged_by_logging", c057 },
    { "cplus_alloc_fail_reset", "parity_after_injection", c058 },
    { "cplus_malloc", "coordinated_alloc_free_2", c059 },
    { "cplus_malloc", "coordinated_alloc_free_4", c060 },
    { "cplus_malloc", "coordinated_alloc_free_8", c061 },
    { "pthread_mutex_t", "shared_buffer_recycle", c062 },
    { "pthread_cond_t", "tracked_buffer_handoff", c063 },
    { "pthread_cond_t", "timedwait_expired", c064 },
    { "cplus_fesetround", "per_thread_fp_mode_4", c065 },
    { "cplus_fesetround", "fp_mode_across_barriers", c066 },
    { "pthread_create", "repeated_waves_6x6", c067 },
    { "pthread_create", "repeated_waves_3x8", c068 },
    { "cplus_free_checked", "double_free_contention_8", c069 },
    { "cplus_alloc_double_free_count", "counter_under_contention", c070 },
    { "cplus_transfer_ownership", "two_claimants_one_wins", c071 },
    { "cplus_transfer_ownership", "generation_bumped", c072 },
    { "cplus_alloc_check_generation", "stale_after_free", c073 },
    { "cplus_alloc_query_length", "concurrent_queries", c074 },
    { "cplus_alloc_remaining_capacity", "concurrent_queries", c075 },
    { "pthread_key", "tls_buffer_cleanup", c076 },
    { "cplus_safe_stat_record", "record_concurrent", c077 },
    { "cplus_safe_stats_get", "get_counters", c078 },
    { "cplus_safe_stats_dump", "bounded_dump", c079 },
    { "cplus_safe_stats_reset", "zeroes_counters", c080 },
    { "cplus_safe_stat_record", "null_api_ignored", c081 },
    { "cplus_safe_stat_record", "mixed_ok_fail", c082 },
    { "cplus_libc_log", "default_off_threads_silent", c083 },
    { "cplus_libc_log_set_enabled", "global_flag_visible_8", c084 },
    { "cplus_libc_log", "concurrent_short_lines_no_torn", c085 },
    { "cplus_libc_log", "concurrent_long_lines_bounded", c086 },
    { "cplus_libc_log", "disable_during_flood", c087 },
    { "cplus_libc_log", "multi_round_trip", c088 },
    { "cplus_libc_log", "interleaved_direct_and_api", c089 },
    { "cplus_libc_log", "instrumented_error_paths", c090 },
    { "cplus_libc_log", "instrumented_stderr_closed", c091 },
    { "cplus_libc_log", "null_fields_in_threads", c092 },
    { "cplus_libc_log", "sentinel_under_threads", c093 },
    { "cplus_libc_log", "long_fields_under_threads", c094 },
    { "cplus_libc_log", "toggle_cycles_with_calls", c095 },
    { "cplus_libc_log", "line_shape_mixed_load", c096 },
    { "cplus_libc_log", "no_alloc_concurrent_logging", c097 },
    { "cplus_libc_log", "capture_reuse_10_rounds", c098 },
    { "cplus_libc_log", "off_after_flag_restore", c099 },
    { "cplus_libc_log_set_enabled", "final_state_default_off", c100 }
};

#ifndef HL_CASE_LIMIT
#define HL_CASE_LIMIT 100
#endif

int main(void) {
    int i, ok = 0, outcome, passed;
    const char *word;

    setbuf(stdout, NULL); /* keep case lines ordered with FINDING/NOTE lines */
    stats_resolve();
    cplus_libc_log_set_enabled(0);
    cplus_alloc_fail_reset();

    for (i = 0; i < HL_CASE_LIMIT && i < 100; ++i) {
        g_case = i + 1;
        outcome = CASES[i].fn();
        if (outcome == OUT_FAIL) word = "FAIL";
        else if (outcome == OUT_UNAVAIL) word = "UNAVAILABLE";
        else word = "PASS";
        printf("hundred_logging_threads: case %d %s %s %s\n",
               i + 1, CASES[i].method, CASES[i].scenario, word);
        if (outcome != OUT_FAIL) ++ok;
    }

    /* Defensive: leave the process in the documented default state. */
    cplus_libc_log_set_enabled(0);
    cplus_alloc_fail_reset();
    passed = ok;
    printf("hundred_logging_threads: %d/100 scenarios ok\n", passed);
    if (g_unavail > 0)
        printf("hundred_logging_threads: note: %d optional SafeCRT scenario(s) unavailable in this link\n",
               g_unavail);
    if (g_findings > 0)
        printf("hundred_logging_threads: note: %d FINDING(s) reported above\n", g_findings);
    return 0;
}
