/*
 * test_logging_telemetry.c -- standalone C99 proof of the opt-in logging
 * contract of libc/log/log.c (cplus_libc_log / cplus_libc_log_set_enabled).
 *
 * This is a TEST ONLY. It references only libc APIs that already exist in
 * the tree (no functions added by sibling agents):
 *   cplus_malloc, cplus_free, cplus_memcpy, cplus_strlen,
 *   cplus_fopen, cplus_fclose, cplus_strtol,
 *   cplus_alloc_live_count, cplus_libc_log, cplus_libc_log_set_enabled.
 *
 * Logging implementation under test:
 *   static atomic_int g_enabled = 0;                       (OFF by default)
 *   cplus_libc_log_set_enabled() stores 0/1 atomically.
 *   cplus_libc_log() returns immediately when disabled; otherwise it emits
 *   "[CPLUS-LIBC] <api>: <msg>\n" with a single fprintf(stderr, ...) call.
 *
 * What is actually PROVEN below (assertions):
 *   (1) DEFAULT-OFF: no "[CPLUS-LIBC]" lines at process start.
 *   (2) ENABLED: entry markers for malloc/memcpy/strlen/... appear; disabling
 *       again restores silence.
 *   (3) FAILURE PATHS: safe guarded/defined error paths keep identical return
 *       values with logging on vs off, and their entry markers are present.
 *   (4) NO SENSITIVE DATA: distinctive sentinel bytes never appear in logs.
 *   (5) CONCURRENT CALLS: 4 pthreads x N iterations, stable logging state,
 *       no crash/deadlock and correct return values.
 *   (6) NO UNCONTROLLED ALLOCATION: cplus_alloc_live_count() is unchanged by
 *       a burst of direct cplus_libc_log() calls when nothing else allocates.
 *   (7) Captured stderr is hard-capped at 64 KiB.
 *
 * What can only be DOCUMENTED (not proven by a black-box test):
 *   * Logging writes no error-detail lines: every instrumented API emits only
 *     an "enter" marker, so there is no status/error line to assert. This is
 *     stated in comments where failure paths are exercised.
 *   * "fprintf allocates nothing" cannot be observed directly; we rely on the
 *     source design (atomic flag + direct fprintf) plus assertion (6).
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

/* Local declarations, matching tests/libc/test_libc_basic.c style. */
extern void *cplus_malloc(size_t);
extern void cplus_free(void *);
extern void *cplus_memcpy(void *restrict, const void *restrict, size_t);
extern size_t cplus_strlen(const char *);
extern FILE *cplus_fopen(const char *, const char *);
extern int cplus_fclose(FILE *);
extern long cplus_strtol(const char *, char **, int);
extern size_t cplus_alloc_live_count(void);
extern void cplus_libc_log(const char *, const char *);
extern void cplus_libc_log_set_enabled(int);
extern int *__cplus_errno_location(void);

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#endif
#include <pthread.h>

#define LOG_PREFIX "[CPLUS-LIBC] "
#define CAP_LIMIT 65536u

static char cap_buf[CAP_LIMIT]; /* single reusable, hard-capped capture buffer */

/* ------------------------------------------------------------------ */
/* stderr capture helpers (POSIX only)                                 */
/* ------------------------------------------------------------------ */
#ifndef _WIN32
typedef struct {
    int saved; /* duplicates original stderr */
    int rd;    /* read end of pipe */
    int wr;    /* write end of pipe */
    int active;
} Capture;

static int cap_start(Capture *c)
{
    int fds[2];
    c->saved = -1;
    c->rd = -1;
    c->wr = -1;
    c->active = 0;
    if (pipe(fds) != 0) return -1;
    c->rd = fds[0];
    c->wr = fds[1];
    c->saved = dup(STDERR_FILENO);
    if (c->saved < 0) {
        close(c->rd);
        close(c->wr);
        c->rd = c->wr = -1;
        return -1;
    }
    if (dup2(c->wr, STDERR_FILENO) < 0) {
        dup2(c->saved, STDERR_FILENO);
        close(c->saved);
        close(c->rd);
        close(c->wr);
        c->saved = c->rd = c->wr = -1;
        return -1;
    }
    /* Non-blocking read end: cap_finish can never hang, even on overflow. */
    {
        int fl = fcntl(c->rd, F_GETFL, 0);
        if (fl >= 0) (void)fcntl(c->rd, F_SETFL, fl | O_NONBLOCK);
    }
    c->active = 1;
    return 0;
}

/* Restores stderr on every path, then drains at most bufsz-1 bytes. */
static size_t cap_finish(Capture *c, char *buf, size_t bufsz)
{
    size_t total = 0;
    if (c->active) {
        fflush(stderr);
        (void)dup2(c->saved, STDERR_FILENO); /* restore BEFORE any assert */
        close(c->saved);
        close(c->wr); /* writer closed => EOF after drain */
        c->saved = -1;
        c->wr = -1;
        c->active = 0;
    }
    if (bufsz == 0) {
        if (c->rd >= 0) { close(c->rd); c->rd = -1; }
        return 0;
    }
    for (;;) {
        ssize_t n;
        if (total >= bufsz - 1) break; /* hard cap: never grow unbounded */
        n = read(c->rd, buf + total, bufsz - 1 - total);
        if (n > 0) { total += (size_t)n; continue; }
        if (n == 0) break;                       /* EOF */
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        break;
    }
    buf[total] = '\0';
    if (c->rd >= 0) { close(c->rd); c->rd = -1; }
    return total;
}

/* Sink stderr to /dev/null for high-volume bursts (avoids pipe deadlock). */
static int sink_stderr(int *saved)
{
    int fd;
    *saved = dup(STDERR_FILENO);
    if (*saved < 0) return -1;
    fd = open("/dev/null", O_WRONLY);
    if (fd < 0) { close(*saved); *saved = -1; return -1; }
    if (dup2(fd, STDERR_FILENO) < 0) {
        close(fd);
        (void)dup2(*saved, STDERR_FILENO);
        close(*saved);
        *saved = -1;
        return -1;
    }
    close(fd);
    return 0;
}

static void unsink_stderr(int saved)
{
    if (saved >= 0) {
        fflush(stderr);
        (void)dup2(saved, STDERR_FILENO);
        close(saved);
    }
}
#endif /* !_WIN32 */

/* ------------------------------------------------------------------ */
/* Bodies shared by the on/off comparisons                             */
/* ------------------------------------------------------------------ */
static void exercise_apis(void)
{
    const char src[] = "hello-logging-world";
    char dst[64];
    char *p = (char *)cplus_malloc(32);
    assert(p != NULL);
    (void)cplus_memcpy(dst, src, cplus_strlen(src) + 1);
    assert(cplus_strlen(dst) == cplus_strlen(src));
    cplus_free(p);
    {
        FILE *fp = cplus_fopen("/definitely/not/a/real/path/igi_log_probe", "r");
        if (fp != NULL) (void)cplus_fclose(fp);
    }
    {
        char *end = NULL;
        long v = cplus_strtol("12345", &end, 10);
        assert(v == 12345);
        assert(end != NULL && *end == '\0');
    }
}

typedef struct {
    int fopen_null_is_null;    /* guarded NULL path */
    int fopen_null_errno;      /* EINVAL set by the guard */
    int fopen_missing_is_null; /* host-delegated failure */
    long strtol_no_conversion; /* defined "no digits" result */
    char *strtol_end;          /* endptr identity */
} FailResults;

static void do_failure_calls(FailResults *r)
{
    const char text[] = "not-a-number";
    FILE *a;
    FILE *b;

    /* NOTE: cplus_strlen(NULL) and cplus_memcpy(..., SIZE_MAX) are UB and are
     * deliberately NOT invoked. The invalid-base strtol path (base 1) is also
     * UB per ISO C, so we use the defined "no digits converted" result of a
     * valid base instead. cplus_fopen is guarded for NULL, hence safe. */
    /* stdio.c uses the platform errno (not __cplus_errno_location). */
    errno = 0;
    a = cplus_fopen(NULL, "r");
    r->fopen_null_is_null = (a == NULL);
    r->fopen_null_errno = errno;

    b = cplus_fopen("/nonexistent/igi_log_telemetry_probe/no_such_file", "r");
    r->fopen_missing_is_null = (b == NULL);
    if (b != NULL) (void)cplus_fclose(b);

    r->strtol_end = NULL;
    r->strtol_no_conversion = cplus_strtol(text, &r->strtol_end, 10);
}

typedef struct { int rc; } ThreadArg;

static void *worker(void *arg)
{
    ThreadArg *t = (ThreadArg *)arg;
    const char src[] = "concurrent-logging-probe"; /* ends in 'e', starts 'c' */
    const size_t len = sizeof src - 1;
    int i;
    t->rc = 0;
    for (i = 0; i < 500; ++i) {
        char *p = (char *)cplus_malloc(64);
        if (p == NULL) { t->rc = 1; return NULL; }
        (void)cplus_memcpy(p, src, len + 1);
        if (cplus_strlen(p) != len) { t->rc = 2; cplus_free(p); return NULL; }
        if (p[0] != 'c' || p[len - 1] != 'e') {
            t->rc = 3;
            cplus_free(p);
            return NULL;
        }
        cplus_free(p);
    }
    return NULL;
}

int main(void)
{
    static const char sentinel[] = "TOPSECRET_SENTINEL_9F3C_0xDEADBEEF_CPLUS";
    FailResults on, off;
    size_t n;

    /* ---- (1) DEFAULT-OFF: no set_enabled() call at all: true default. ---- */
#ifndef _WIN32
    {
        Capture c;
        if (cap_start(&c) != 0) {
            printf("note: could not capture stderr; skipping default-off check\n");
        } else {
            exercise_apis();
            n = cap_finish(&c, cap_buf, sizeof cap_buf);
            assert(n < CAP_LIMIT);                 /* output is capped/small */
            assert(strstr(cap_buf, LOG_PREFIX) == NULL);
        }
    }
#else
    printf("note: stderr redirection unavailable on Windows; "
           "skipping capture-based checks\n");
#endif

    /* ---- (2) ENABLED, then disabled again. ---- */
    cplus_libc_log_set_enabled(1);
#ifndef _WIN32
    {
        Capture c;
        if (cap_start(&c) == 0) {
            exercise_apis();
            n = cap_finish(&c, cap_buf, sizeof cap_buf);
            assert(n < CAP_LIMIT);
            assert(strstr(cap_buf, LOG_PREFIX "malloc:") != NULL);
            assert(strstr(cap_buf, LOG_PREFIX "memcpy:") != NULL);
            assert(strstr(cap_buf, LOG_PREFIX "strlen:") != NULL);
            assert(strstr(cap_buf, LOG_PREFIX "free:") != NULL);
            assert(strstr(cap_buf, LOG_PREFIX "fopen:") != NULL);
            assert(strstr(cap_buf, LOG_PREFIX "strtol:") != NULL);
        }
    }
#endif
    cplus_libc_log_set_enabled(0);
#ifndef _WIN32
    {
        Capture c;
        if (cap_start(&c) == 0) {
            exercise_apis();
            n = cap_finish(&c, cap_buf, sizeof cap_buf);
            assert(n < CAP_LIMIT);
            assert(strstr(cap_buf, LOG_PREFIX) == NULL); /* silence restored */
        }
    }
#endif

    /* ---- (3) FAILURE PATHS: identical return values on vs off. ----
     * The implementation logs only an "enter" marker at API entry; it emits
     * NO error/status detail line (e.g. no "fopen failed" message). Therefore
     * we assert the entry markers and the unchanged return values, and do NOT
     * assert the presence of any error-detail line -- none exists. */
    cplus_libc_log_set_enabled(1);
#ifndef _WIN32
    {
        Capture c;
        if (cap_start(&c) == 0) {
            do_failure_calls(&on);
            n = cap_finish(&c, cap_buf, sizeof cap_buf);
            assert(n < CAP_LIMIT);
            assert(strstr(cap_buf, LOG_PREFIX "fopen:") != NULL);
            assert(strstr(cap_buf, LOG_PREFIX "strtol:") != NULL);
        } else {
            do_failure_calls(&on);
        }
    }
#else
    do_failure_calls(&on);
#endif
    cplus_libc_log_set_enabled(0);
#ifndef _WIN32
    {
        Capture c;
        if (cap_start(&c) == 0) {
            do_failure_calls(&off);
            n = cap_finish(&c, cap_buf, sizeof cap_buf);
            assert(n < CAP_LIMIT);
            assert(strstr(cap_buf, LOG_PREFIX) == NULL);
        } else {
            do_failure_calls(&off);
        }
    }
#else
    do_failure_calls(&off);
#endif
    assert(on.fopen_null_is_null == off.fopen_null_is_null);
    assert(on.fopen_null_is_null == 1);
    assert(on.fopen_null_errno == off.fopen_null_errno);
    assert(on.fopen_null_errno == EINVAL);
    assert(on.fopen_missing_is_null == off.fopen_missing_is_null);
    assert(on.fopen_missing_is_null == 1);
    assert(on.strtol_no_conversion == off.strtol_no_conversion);
    assert(on.strtol_no_conversion == 0);
    assert(on.strtol_end == off.strtol_end);

    /* ---- (4) NO SENSITIVE DATA: user buffers must never be dumped. ---- */
    cplus_libc_log_set_enabled(1);
#ifndef _WIN32
    {
        Capture c;
        if (cap_start(&c) == 0) {
            char d[128];
            (void)cplus_memcpy(d, sentinel, sizeof sentinel);
            assert(memcmp(d, sentinel, sizeof sentinel) == 0);
            assert(cplus_strlen(sentinel) == sizeof sentinel - 1);
            n = cap_finish(&c, cap_buf, sizeof cap_buf);
            assert(n < CAP_LIMIT);
            assert(strstr(cap_buf, LOG_PREFIX "memcpy:") != NULL);
            assert(strstr(cap_buf, sentinel) == NULL);
            assert(strstr(cap_buf, "TOPSECRET") == NULL);
            assert(strstr(cap_buf, "DEADBEEF") == NULL);
        } else {
            /* Without capture, still verify the copy itself is correct. */
            char d[128];
            (void)cplus_memcpy(d, sentinel, sizeof sentinel);
            assert(memcmp(d, sentinel, sizeof sentinel) == 0);
        }
    }
#else
    {
        char d[128];
        (void)cplus_memcpy(d, sentinel, sizeof sentinel);
        assert(memcmp(d, sentinel, sizeof sentinel) == 0);
    }
#endif
    cplus_libc_log_set_enabled(0);

    /* ---- (5) CONCURRENT CALLS: logging state held stable during the run. ---- */
    {
        pthread_t threads[4];
        ThreadArg args[4];
        int created[4];
        int all_created = 1;
        int all_joined = 1;
        int all_ok = 1;
        int i;
        int saved = -1;
        int have_sink = 0;
#ifndef _WIN32
        cplus_libc_log_set_enabled(1);
        have_sink = (sink_stderr(&saved) == 0);
        if (!have_sink) cplus_libc_log_set_enabled(0); /* avoid output flood */
#else
        cplus_libc_log_set_enabled(0);
#endif
        for (i = 0; i < 4; ++i) {
            args[i].rc = -1;
            created[i] = (pthread_create(&threads[i], NULL, worker, &args[i]) == 0);
            if (!created[i]) all_created = 0;
        }
        for (i = 0; i < 4; ++i) {
            void *rv = NULL;
            if (!created[i]) continue;
            if (pthread_join(threads[i], &rv) != 0) all_joined = 0; /* no deadlock */
            if (rv != NULL || args[i].rc != 0) all_ok = 0; /* values uncorrupted */
        }
        /* Restore stderr BEFORE asserting, so a failure is observable. */
        if (have_sink) unsink_stderr(saved);
        cplus_libc_log_set_enabled(0);
        assert(all_created);
        assert(all_joined);
        assert(all_ok);
    }

    /* ---- (6) NO UNCONTROLLED ALLOCATION IN LOGGING. ----
     * Design (from source): g_enabled is one atomic_int; cplus_libc_log()
     * reads it and, when enabled, issues a single fprintf(stderr, ...). It
     * never calls malloc/calloc/realloc and stores no user data. Enabling it
     * must therefore leave live-allocation counts untouched. We cannot prove
     * "fprintf allocates nothing" directly, but we prove the observable
     * consequence: between the two live-count reads below the ONLY call is
     * cplus_libc_log(), so any hidden allocation would change the count. */
    {
        int saved = -1;
        int have_sink = 0;
        size_t before;
        size_t after;
        int i;
#ifndef _WIN32
        cplus_libc_log_set_enabled(1);
        have_sink = (sink_stderr(&saved) == 0);
        if (!have_sink) cplus_libc_log_set_enabled(0);
#else
        cplus_libc_log_set_enabled(0);
#endif
        cplus_libc_log("warmup", "prime stderr internals before measuring");
        before = cplus_alloc_live_count();
        for (i = 0; i < 1000; ++i) {
            cplus_libc_log("burst", "no-allocation-probe");
        }
        after = cplus_alloc_live_count();
        assert(before == after);
        if (have_sink) unsink_stderr(saved);
        cplus_libc_log_set_enabled(0);
    }

    puts("logging_telemetry: ok");
    return 0;
}
