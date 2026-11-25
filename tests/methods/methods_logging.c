/*
 * methods_logging.c -- dedicated unit tests for the libc logging layer itself.
 *
 * This is the contract that every other "method" test relies on when it calls
 * cplus_libc_log_set_enabled(1/0) around an instrumented API.  It exercises
 * only APIs that already exist in libcplus_hardened.a:
 *
 *   cplus_libc_log, cplus_libc_log_set_enabled,
 *   cplus_malloc, cplus_free, cplus_memcpy, cplus_strlen,
 *   cplus_fopen, cplus_fclose, cplus_strtol, cplus_alloc_live_count.
 *
 * Implementation under test (libc/log/log.c):
 *   - OFF by default (static atomic_int g_enabled = 0).
 *   - set_enabled() stores 0/1 atomically.
 *   - log() returns immediately when disabled; otherwise it builds one line
 *     "[CPLUS-LIBC] <api>: <msg>\n" in a fixed 4096-byte stack buffer and
 *     emits it with a single write(2).  NULL api/msg become "?"; over-long
 *     input is truncated with a "..." marker; no heap, no stdio buffering.
 *
 * Capture strategy: a pipe on fd 2 plus a *drainer thread* using poll() and
 * a non-blocking read end into ONE reusable, hard-capped static 256 KiB
 * buffer.  No capture allocation is unbounded, and stderr is restored on
 * every normal and failure path before any finding is printed.
 *
 * Each scenario is named m_log_<n>_<slug> and is guarded by a METHOD banner
 * comment (search this file for "METHOD:").  Each prints FINDING: lines for
 * real defects and returns 1 (pass) or 0 (fail).  main always returns 0.
 *
 * HONESTY LIMIT: scenario 12 observes a forked SIGALRM-handler child.  A
 * clean run shows no observable deadlock/crash in that run; it does NOT prove
 * POSIX async-signal-safety (a single empirical sample cannot).  This is
 * stated explicitly in the output.
 */

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* Local declarations (test-only; matches sibling test style). */
extern void *cplus_malloc(size_t);
extern void cplus_free(void *);
extern void *cplus_memcpy(void *restrict, const void *restrict, size_t);
extern size_t cplus_strlen(const char *);
extern int cplus_strcmp(const char *, const char *);
extern int cplus_memcmp(const void *, const void *, size_t);
extern FILE *cplus_fopen(const char *, const char *);
extern int cplus_fclose(FILE *);
extern long cplus_strtol(const char *, char **, int);
extern int cplus_isalpha(int), cplus_isdigit(int);
extern int cplus_toupper(int), cplus_tolower(int);
extern int cplus_abs(int);
extern size_t cplus_alloc_live_count(void);
extern void cplus_libc_log(const char *, const char *);
extern void cplus_libc_log_set_enabled(int);

#define LOG_PREFIX "[CPLUS-LIBC] "
#define CAP_LIMIT (256u * 1024u) /* strict 256 KiB capture cap */
#define LONG_LEN 100000u

/* ------------------------------------------------------------------ */
/* Bounded, nonblocking stderr capture.                                */
/* ------------------------------------------------------------------ */
static char g_cap[CAP_LIMIT + 1]; /* one reusable, hard-capped buffer */

typedef struct {
    int saved;              /* duplicate of original stderr */
    int rd, wr;             /* pipe ends */
    int active;
    int drainer_started;
    pthread_t drainer;
    size_t n;               /* bytes actually captured (<= CAP_LIMIT) */
    int overflow;           /* output exceeded CAP_LIMIT (tail discarded) */
} Capture;

static void *capture_drainer(void *arg)
{
    Capture *c = (Capture *)arg;
    char scratch[4096];
    for (;;) {
        struct pollfd pfd;
        int pr;
        pfd.fd = c->rd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        pr = poll(&pfd, 1, 100);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue; /* bounded timeout; EOF ends the loop */
        for (;;) {
            ssize_t got;
            if (c->n < CAP_LIMIT) {
                got = read(c->rd, g_cap + c->n, (size_t)(CAP_LIMIT - c->n));
            } else {
                c->overflow = 1;
                got = read(c->rd, scratch, sizeof scratch); /* discard tail */
            }
            if (got > 0) {
                if (c->n < CAP_LIMIT) c->n += (size_t)got;
                continue;
            }
            if (got == 0) return NULL; /* EOF: writer closed */
            if (errno == EINTR) continue;
            break; /* EAGAIN/EWOULDBLOCK: wait for next poll */
        }
    }
    return NULL;
}

/* Returns 0 on success. On any failure stderr is left as it was found. */
static int cap_start(Capture *c)
{
    int fds[2];
    int fl;
    memset(c, 0, sizeof *c);
    c->saved = c->rd = c->wr = -1;
    if (pipe(fds) != 0) return -1;
    c->rd = fds[0];
    c->wr = fds[1];
    c->saved = dup(STDERR_FILENO);
    if (c->saved < 0) goto fail;
    if (dup2(c->wr, STDERR_FILENO) < 0) goto fail;
    fl = fcntl(c->rd, F_GETFL, 0);
    if (fl >= 0) (void)fcntl(c->rd, F_SETFL, fl | O_NONBLOCK);
    c->active = 1;
    g_cap[0] = '\0';
    if (pthread_create(&c->drainer, NULL, capture_drainer, c) == 0)
        c->drainer_started = 1;
    return 0;
fail:
    if (c->saved >= 0) {
        (void)dup2(c->saved, STDERR_FILENO);
        close(c->saved);
    }
    if (c->rd >= 0) close(c->rd);
    if (c->wr >= 0) close(c->wr);
    c->saved = c->rd = c->wr = -1;
    return -1;
}

/* Restores stderr FIRST, then drains and joins. Always safe to call. */
static size_t cap_finish(Capture *c)
{
    size_t n;
    if (c->active) {
        (void)dup2(c->saved, STDERR_FILENO);
        close(c->saved);
        close(c->wr); /* last writer -> drainer sees EOF */
        c->saved = -1;
        c->wr = -1;
        c->active = 0;
    }
    if (c->drainer_started) {
        (void)pthread_join(c->drainer, NULL);
        c->drainer_started = 0;
    }
    n = c->n;
    if (c->rd >= 0) { close(c->rd); c->rd = -1; }
    if (n > CAP_LIMIT) n = CAP_LIMIT;
    g_cap[n] = '\0';
    return n;
}

/* Sink stderr to /dev/null for high-volume / signal scenarios. */
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
        (void)dup2(saved, STDERR_FILENO);
        close(saved);
    }
}

/* Count occurrences of the log prefix in captured bytes. */
static size_t count_prefixes(const char *buf, size_t n)
{
    const size_t plen = sizeof(LOG_PREFIX) - 1;
    size_t k, c = 0;
    for (k = 0; k + plen <= n; ++k)
        if (memcmp(buf + k, LOG_PREFIX, plen) == 0) ++c;
    return c;
}

/* Validate every line matches ^\[CPLUS-LIBC\] .*: .*$ ; return line count. */
static int validate_lines(const char *buf, size_t n)
{
    const size_t plen = sizeof(LOG_PREFIX) - 1;
    size_t i = 0, lines = 0, prefixes;
    if (n == 0) return -1;
    if (buf[n - 1] != '\n') return -2; /* partial trailing line */
    while (i < n) {
        size_t j = i;
        while (j < n && buf[j] != '\n') ++j;
        if (j == i) return -3; /* blank line */
        if (j - i < plen || memcmp(buf + i, LOG_PREFIX, plen) != 0) return -4;
        if (strstr(buf + i + plen, ": ") == NULL) return -5;
        ++lines;
        i = j + 1;
    }
    prefixes = count_prefixes(buf, n);
    if (prefixes != lines) return -6; /* merged/nested lines */
    return (int)lines;
}

/* ------------------------------------------------------------------ */
/* Shared API exercise bodies.                                         */
/* ------------------------------------------------------------------ */
static void exercise_apis(void)
{
    const char src[] = "hello-logging-world";
    char dst[64];
    char *p = (char *)cplus_malloc(32);
    if (p == NULL) return;
    (void)cplus_memcpy(dst, src, cplus_strlen(src) + 1);
    (void)cplus_strlen(dst);
    cplus_free(p);
    {
        FILE *fp = cplus_fopen("/definitely/not/a/real/path/igi_log_probe", "r");
        if (fp != NULL) (void)cplus_fclose(fp);
    }
    {
        char *end = NULL;
        long v = cplus_strtol("12345", &end, 10);
        (void)v;
    }
}

typedef struct {
    int fopen_null_is_null;
    int fopen_null_errno;
    int fopen_missing_is_null;
    long strtol_nodigits;
    char *strtol_end;
} FailResults;

static const char g_nodigits[] = "not-a-number";

static void do_failure_calls(FailResults *r)
{
    FILE *a, *b;
    /* Only defined / guarded error paths are used:
     *   cplus_fopen(NULL,...) is guarded and sets EINVAL;
     *   cplus_strtol("not-a-number",...,10) has the defined "no conversion"
     *     result 0 with endptr == input.  We never pass NULL to strlen or
     *     SIZE_MAX to memcpy (both UB). */
    errno = 0;
    a = cplus_fopen(NULL, "r");
    r->fopen_null_is_null = (a == NULL);
    r->fopen_null_errno = errno;

    b = cplus_fopen("/nonexistent/igi_log_probe_dir/no_such_file", "r");
    r->fopen_missing_is_null = (b == NULL);
    if (b != NULL) (void)cplus_fclose(b);

    r->strtol_end = NULL;
    r->strtol_nodigits = cplus_strtol(g_nodigits, &r->strtol_end, 10);
}

/* 20 deterministic instrumented calls (scenario 9). */
static void run_20(long long out[20])
{
    out[0]  = cplus_isalpha('A');
    out[1]  = cplus_isalpha('7');
    out[2]  = cplus_isdigit('7');
    out[3]  = cplus_isdigit('x');
    out[4]  = cplus_toupper('a');
    out[5]  = cplus_tolower('A');
    out[6]  = (long long)cplus_abs(-42);
    out[7]  = (long long)cplus_abs(17);
    out[8]  = (long long)cplus_strlen("abc");
    out[9]  = (long long)cplus_strlen("");
    out[10] = cplus_strcmp("abc", "abd");
    out[11] = cplus_strcmp("abc", "abc");
    out[12] = cplus_memcmp("abc", "abc", 3);
    out[13] = cplus_memcmp("abc", "abd", 3);
    out[14] = cplus_strtol("12345", NULL, 10);
    out[15] = cplus_strtol("-7", NULL, 10);
    out[16] = cplus_strtol("0", NULL, 10);
    out[17] = (long long)cplus_strlen("logging");
    out[18] = cplus_isalpha('z');
    out[19] = cplus_isdigit('0');
}

/* ------------------------------------------------------------------ */
/* Scenario 8: coordinated toggle + instrumented workers.              */
/* ------------------------------------------------------------------ */
typedef struct { int rc; int iters; } WorkArg;

static void *toggle_worker(void *arg)
{
    WorkArg *a = (WorkArg *)arg;
    int i;
    for (i = 0; i < a->iters; ++i) {
        cplus_libc_log_set_enabled(1);
        (void)sched_yield(); /* open a window for the instrumented workers */
        cplus_libc_log_set_enabled(0);
        (void)sched_yield();
    }
    return NULL;
}

static void *instr_worker(void *arg)
{
    WorkArg *a = (WorkArg *)arg;
    int i;
    a->rc = 0;
    for (i = 0; i < a->iters; ++i) {
        char *p = (char *)cplus_malloc(32);
        if (p == NULL) { a->rc = 1; break; }
        (void)cplus_memcpy(p, "toggle-probe", 12);
        (void)cplus_strlen(p);
        if (p[0] != 't') { a->rc = 2; cplus_free(p); break; }
        cplus_free(p);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Scenario 12: forked SIGALRM-handler reentrancy probe.               */
/* ------------------------------------------------------------------ */
static volatile sig_atomic_t g_handler_count = 0;
static int g_report_w = -1;

static void sig_handler(int sig)
{
    (void)sig;
    ++g_handler_count;
    cplus_libc_log("sig-handler", "reentrant-enter");
}

static void report_and_exit(int code)
{
    if (g_report_w >= 0) {
        (void)write(g_report_w, &code, sizeof code);
        (void)write(g_report_w, (const void *)&g_handler_count,
                    sizeof g_handler_count);
    }
    _exit(code);
}

/* ================================================================== */
/* m_log_1 .. m_log_12                                                 */
/* ================================================================== */

/* METHOD: cplus_libc_log_set_enabled default OFF -> no output from instrumented calls */
static int m_log_1_default_off(void)
{
    int ok = 1;
    Capture c;
    size_t n = 0;
    if (cap_start(&c) == 0) {
        exercise_apis(); /* before any set_enabled() call: true default */
        n = cap_finish(&c);
        if (n >= CAP_LIMIT || count_prefixes(g_cap, n) != 0) {
            printf("FINDING: m_log_1 default logging emitted output "
                   "(n=%lu)\n", (unsigned long)n);
            ok = 0;
        }
    } else {
        printf("note: m_log_1 capture unavailable; default-off not observed\n");
    }
    return ok;
}

/* METHOD: enable -> [CPLUS-LIBC] <api>: <msg> lines present for malloc/memcpy/strlen/fopen/strtol */
static int m_log_2_enable_lines(void)
{
    int ok = 1;
    Capture c;
    static const char *const markers[] = {
        LOG_PREFIX "malloc:", LOG_PREFIX "memcpy:", LOG_PREFIX "strlen:",
        LOG_PREFIX "fopen:", LOG_PREFIX "strtol:"
    };
    size_t i;
    cplus_libc_log_set_enabled(1);
    if (cap_start(&c) == 0) {
        size_t n;
        exercise_apis();
        n = cap_finish(&c);
        if (n >= CAP_LIMIT) {
            printf("FINDING: m_log_2 capture overflowed\n");
            ok = 0;
        }
        for (i = 0; i < sizeof markers / sizeof markers[0]; ++i) {
            if (strstr(g_cap, markers[i]) == NULL) {
                printf("FINDING: m_log_2 missing marker '%s'\n", markers[i]);
                ok = 0;
            }
        }
    } else {
        printf("note: m_log_2 capture unavailable; markers not observed\n");
    }
    cplus_libc_log_set_enabled(0);
    return ok;
}

/* METHOD: disable -> silence restored */
static int m_log_3_disable_silence(void)
{
    int ok = 1;
    Capture c;
    cplus_libc_log_set_enabled(0);
    if (cap_start(&c) == 0) {
        size_t n;
        exercise_apis();
        n = cap_finish(&c);
        if (n >= CAP_LIMIT || count_prefixes(g_cap, n) != 0) {
            printf("FINDING: m_log_3 output after disable (n=%lu)\n",
                   (unsigned long)n);
            ok = 0;
        }
    } else {
        printf("note: m_log_3 capture unavailable; silence not observed\n");
    }
    cplus_libc_log_set_enabled(0);
    return ok;
}

/* METHOD: NULL api / NULL msg / both -> "?" substitution, no crash */
static int m_log_4_null_substitution(void)
{
    int ok = 1;
    Capture c;
    cplus_libc_log_set_enabled(1);
    if (cap_start(&c) == 0) {
        size_t n;
        cplus_libc_log(NULL, NULL);
        cplus_libc_log(NULL, "m");
        cplus_libc_log("a", NULL);
        n = cap_finish(&c);
        if (n == 0 ||
            strstr(g_cap, LOG_PREFIX "?: ?\n") == NULL ||
            strstr(g_cap, LOG_PREFIX "?: m\n") == NULL ||
            strstr(g_cap, LOG_PREFIX "a: ?\n") == NULL) {
            printf("FINDING: m_log_4 NULL substitution wrong (n=%lu, "
                   "head=%.60s)\n", (unsigned long)n, g_cap);
            ok = 0;
        }
    } else {
        printf("note: m_log_4 capture unavailable; calling NULL variants "
               "directly\n");
        cplus_libc_log(NULL, NULL);
        cplus_libc_log(NULL, "m");
        cplus_libc_log("a", NULL);
    }
    cplus_libc_log_set_enabled(0); /* reaching here proves no crash */
    return ok;
}

/* METHOD: very long api/msg -> bounded 4096-byte line with "..." marker, no overflow */
static int m_log_5_long_bounded(void)
{
    static char api[LONG_LEN + 1];
    static char msg[LONG_LEN + 1];
    int ok = 1;
    size_t i, n = 0;
    for (i = 0; i < LONG_LEN; ++i) { api[i] = 'A'; msg[i] = 'B'; }
    api[LONG_LEN] = '\0';
    msg[LONG_LEN] = '\0';
    cplus_libc_log_set_enabled(1);
    {
        Capture c;
        if (cap_start(&c) == 0) {
            cplus_libc_log(api, msg);
            n = cap_finish(&c);
            if (n == 0 || n > 4096u) {
                printf("FINDING: m_log_5 line not bounded to 4096 "
                       "(n=%lu)\n", (unsigned long)n);
                ok = 0;
            }
            if (n >= 4u && memcmp(g_cap + n - 4, "...\n", 4) != 0) {
                printf("FINDING: m_log_5 missing '...' truncation marker\n");
                ok = 0;
            }
            if (n > 0 && g_cap[n - 1] != '\n') {
                printf("FINDING: m_log_5 line not newline-terminated\n");
                ok = 0;
            }
            if (c.overflow) {
                printf("FINDING: m_log_5 capture overflowed 256 KiB\n");
                ok = 0;
            }
        } else {
            printf("note: m_log_5 capture unavailable; length not observed\n");
        }
    }
    cplus_libc_log_set_enabled(0);
    return ok;
}

/* METHOD: no sensitive sentinel bytes in captured log */
static int m_log_6_no_sensitive_data(void)
{
    static const char sentinel[] = "TOPSECRET_SENTINEL_9F3C_0xDEADBEEF_CPLUS";
    int ok = 1;
    char d[128];
    cplus_libc_log_set_enabled(1);
    {
        Capture c;
        if (cap_start(&c) == 0) {
            size_t n;
            (void)cplus_memcpy(d, sentinel, sizeof sentinel);
            (void)cplus_strlen(sentinel);
            n = cap_finish(&c);
            if (memcmp(d, sentinel, sizeof sentinel) != 0) {
                printf("FINDING: m_log_6 user copy was corrupted\n");
                ok = 0;
            }
            if (strstr(g_cap, LOG_PREFIX "memcpy:") == NULL ||
                strstr(g_cap, LOG_PREFIX "strlen:") == NULL) {
                printf("FINDING: m_log_6 expected entry markers absent\n");
                ok = 0;
            }
            if (strstr(g_cap, sentinel) != NULL ||
                strstr(g_cap, "TOPSECRET") != NULL ||
                strstr(g_cap, "DEADBEEF") != NULL) {
                printf("FINDING: m_log_6 sensitive sentinel leaked into log "
                       "(n=%lu)\n", (unsigned long)n);
                ok = 0;
            }
        } else {
            printf("note: m_log_6 capture unavailable; leak not observed\n");
        }
    }
    cplus_libc_log_set_enabled(0);
    return ok;
}

/* METHOD: stderr closed then restored -> logging resumes, no crash */
static int m_log_7_closed_stderr(void)
{
    int ok = 1;
    int saved = dup(STDERR_FILENO);
    if (saved < 0) {
        printf("note: m_log_7 could not save stderr; skipped\n");
        return 1;
    }
    (void)close(STDERR_FILENO);
    cplus_libc_log_set_enabled(1);
    /* write(2) to a closed fd fails harmlessly; the logger ignores errors. */
    {
        char b[16];
        (void)cplus_memcpy(b, "closedfd", 8);
        (void)cplus_strlen(b);
        cplus_libc_log("direct", "stderr-closed");
    }
    cplus_libc_log_set_enabled(0);
    (void)dup2(saved, STDERR_FILENO); /* restore fd 2 */
    close(saved);

    /* After restore, logging must emit bytes again. write(2)-based logging
     * is immune to a stale stdio error flag, unlike fprintf-based logging. */
    cplus_libc_log_set_enabled(1);
    {
        Capture c;
        if (cap_start(&c) == 0) {
            size_t n;
            cplus_libc_log("post-restore", "probe");
            n = cap_finish(&c);
            if (n == 0 || strstr(g_cap, LOG_PREFIX "post-restore:") == NULL) {
                printf("FINDING: m_log_7 logging did not resume after "
                       "stderr restore (n=%lu)\n", (unsigned long)n);
                ok = 0;
            }
        } else {
            printf("note: m_log_7 capture unavailable; resume not observed\n");
        }
    }
    cplus_libc_log_set_enabled(0); /* reaching here proves no crash */
    return ok;
}

/* METHOD: 4 threads toggling + 4 instrumented workers -> no deadlock, well-formed lines */
static int m_log_8_concurrency(void)
{
    int ok = 1;
    Capture c;
    pthread_t togglers[4], workers[4];
    WorkArg ta[4], wa[4];
    int created[8], i, all_created = 1, all_joined = 1;
    cplus_libc_log_set_enabled(0); /* threads drive their own state */
    if (cap_start(&c) != 0) {
        printf("note: m_log_8 capture unavailable; line check skipped\n");
        cplus_libc_log_set_enabled(0);
        return 1;
    }
    for (i = 0; i < 4; ++i) {
        ta[i].iters = 2000; ta[i].rc = 0;
        wa[i].iters = 200; wa[i].rc = -1;
    }
    for (i = 0; i < 4; ++i)
        created[i] = (pthread_create(&togglers[i], NULL, toggle_worker,
                                     &ta[i]) == 0);
    for (i = 0; i < 4; ++i)
        created[4 + i] = (pthread_create(&workers[i], NULL, instr_worker,
                                          &wa[i]) == 0);
    for (i = 0; i < 8; ++i) if (!created[i]) all_created = 0;
    for (i = 0; i < 4; ++i) {
        if (created[i] && pthread_join(togglers[i], NULL) != 0) all_joined = 0;
        if (created[4 + i] && pthread_join(workers[i], NULL) != 0)
            all_joined = 0;
        if (created[4 + i] && wa[i].rc != 0) all_joined = 0; /* values sane */
    }
    {
        size_t n = cap_finish(&c);
        int vr = validate_lines(g_cap, n);
        if (!all_created || !all_joined) {
            printf("FINDING: m_log_8 thread failure (created=%d joined=%d)\n",
                   all_created, all_joined);
            ok = 0;
        }
        if (c.overflow) {
            printf("FINDING: m_log_8 capture overflowed 256 KiB\n");
            ok = 0;
        }
        if (vr < 0) {
            printf("FINDING: m_log_8 torn/malformed output (code=%d, "
                   "n=%lu)\n", vr, (unsigned long)n);
            ok = 0;
        } else {
            printf("note: m_log_8 %d well-formed lines, no deadlock\n", vr);
        }
    }
    cplus_libc_log_set_enabled(0);
    return ok;
}

/* METHOD: 20 API calls ON vs OFF -> identical return values */
static int m_log_9_returns_identical(void)
{
    long long on[20], off[20];
    int i, ok = 1;
    int saved = -1;
    int have_sink;
    cplus_libc_log_set_enabled(0);
    run_20(off);
    have_sink = (sink_stderr(&saved) == 0);
    cplus_libc_log_set_enabled(1);
    run_20(on);
    cplus_libc_log_set_enabled(0);
    if (have_sink) unsink_stderr(saved);
    for (i = 0; i < 20; ++i) {
        if (on[i] != off[i]) {
            printf("FINDING: m_log_9 return changed at call %d "
                   "(on=%lld off=%lld)\n", i, on[i], off[i]);
            ok = 0;
        }
    }
    return ok;
}

/* METHOD: 10000 direct cplus_libc_log calls -> live allocation count unchanged */
static int m_log_10_no_allocation(void)
{
    size_t before, after;
    int i, ok = 1;
    int saved = -1;
    int have_sink = (sink_stderr(&saved) == 0);
    if (!have_sink) cplus_libc_log_set_enabled(0);
    else cplus_libc_log_set_enabled(1);
    cplus_libc_log("warmup", "prime-stderr-state");
    before = cplus_alloc_live_count();
    for (i = 0; i < 10000; ++i)
        cplus_libc_log("alloc-probe", "enter");
    after = cplus_alloc_live_count();
    if (before != after) {
        printf("FINDING: m_log_10 allocation in logging path: %lu -> %lu "
               "live\n", (unsigned long)before, (unsigned long)after);
        ok = 0;
    }
    if (have_sink) unsink_stderr(saved);
    cplus_libc_log_set_enabled(0);
    return ok;
}

/* METHOD: error paths of guarded APIs log without changing errno/return */
static int m_log_11_error_paths(void)
{
    FailResults on, off;
    int ok = 1;

    cplus_libc_log_set_enabled(1);
    {
        Capture c;
        if (cap_start(&c) == 0) {
            size_t n;
            do_failure_calls(&on);
            n = cap_finish(&c);
            if (strstr(g_cap, LOG_PREFIX "fopen:") == NULL ||
                strstr(g_cap, LOG_PREFIX "strtol:") == NULL) {
                printf("FINDING: m_log_11 error-path entry markers absent "
                       "(n=%lu)\n", (unsigned long)n);
                ok = 0;
            }
        } else {
            printf("note: m_log_11 capture unavailable; markers not observed\n");
            do_failure_calls(&on);
        }
    }
    cplus_libc_log_set_enabled(0);
    do_failure_calls(&off);

    if (!on.fopen_null_is_null || !off.fopen_null_is_null ||
        on.fopen_null_is_null != off.fopen_null_is_null) {
        printf("FINDING: m_log_11 fopen(NULL) return changed\n");
        ok = 0;
    }
    if (on.fopen_null_errno != EINVAL || off.fopen_null_errno != EINVAL ||
        on.fopen_null_errno != off.fopen_null_errno) {
        printf("FINDING: m_log_11 fopen(NULL) errno changed "
               "(on=%d off=%d)\n", on.fopen_null_errno, off.fopen_null_errno);
        ok = 0;
    }
    if (!on.fopen_missing_is_null || !off.fopen_missing_is_null ||
        on.fopen_missing_is_null != off.fopen_missing_is_null) {
        printf("FINDING: m_log_11 fopen(missing) return changed\n");
        ok = 0;
    }
    if (on.strtol_nodigits != off.strtol_nodigits ||
        on.strtol_nodigits != 0 || on.strtol_end != off.strtol_end ||
        on.strtol_end != g_nodigits) {
        printf("FINDING: m_log_11 strtol(no-digits) result changed\n");
        ok = 0;
    }
    return ok;
}

/* METHOD: log call from a forked signal-handler child -> report honestly if not provably safe */
static int m_log_12_signal_child(void)
{
    int ok = 1;
    int fds[2];
    pid_t pid;
    printf("note: m_log_12 async-signal-safety is NOT provable by one run; "
           "a clean child only shows no observed deadlock/crash\n");
    if (pipe(fds) != 0) {
        printf("note: m_log_12 pipe failed; reentrancy check skipped\n");
        return 1;
    }
    pid = fork();
    if (pid == 0) {
        /* ---- child ---- */
        struct sigaction sa, old;
        struct itimerval it;
        int saved = -1;
        int have_sink;
        int i;
        close(fds[0]);
        g_report_w = fds[1];
        g_handler_count = 0;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = sig_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        cplus_libc_log_set_enabled(1);
        have_sink = (sink_stderr(&saved) == 0); /* avoid signal flood */
        if (sigaction(SIGALRM, &sa, &old) != 0) {
            report_and_exit(95); /* handler install failed */
        }
        memset(&it, 0, sizeof it);
        it.it_interval.tv_sec = 0;
        it.it_interval.tv_usec = 1000; /* 1 ms repeating */
        it.it_value.tv_sec = 0;
        it.it_value.tv_usec = 1000;
        (void)setitimer(ITIMER_REAL, &it, NULL);
        for (i = 0; i < 20000; ++i) {
            char b[16];
            (void)cplus_memcpy(b, "reentrant", 9);
            if (b[0] != 'r') break;
        }
        /* Teardown without a SIGALRM race. */
        {
            sigset_t blockset, oldmask;
            sigemptyset(&blockset);
            sigaddset(&blockset, SIGALRM);
            (void)pthread_sigmask(SIG_BLOCK, &blockset, &oldmask);
            memset(&it, 0, sizeof it);
            (void)setitimer(ITIMER_REAL, &it, NULL);
            (void)signal(SIGALRM, SIG_IGN); /* drop pending */
            (void)sigaction(SIGALRM, &old, NULL);
            (void)pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
        }
        if (have_sink) unsink_stderr(saved);
        cplus_libc_log_set_enabled(0);
        report_and_exit(0);
    } else if (pid > 0) {
        /* ---- parent: bounded wait ---- */
        int status = 0, waited = 0, killed = 0;
        int child_code = -1, handler_count = -1;
        ssize_t got, got2;
        int k;
        close(fds[1]);
        for (k = 0; k < 75; ++k) { /* 75 * 20 ms = 1.5 s ceiling */
            pid_t w = waitpid(pid, &status, WNOHANG);
            struct timespec ts;
            if (w == pid) { waited = 1; break; }
            if (w < 0 && errno == ECHILD) { waited = 1; break; }
            ts.tv_sec = 0;
            ts.tv_nsec = 20000000L;
            (void)nanosleep(&ts, NULL);
        }
        if (!waited) {
            (void)kill(pid, SIGKILL);
            (void)waitpid(pid, &status, 0);
            killed = 1;
        }
        got = read(fds[0], &child_code, sizeof child_code);
        got2 = read(fds[0], &handler_count, sizeof handler_count);
        if (got != (ssize_t)sizeof child_code) child_code = -1;
        if (got2 != (ssize_t)sizeof handler_count) handler_count = -1;
        close(fds[0]);

        if (killed) {
            printf("FINDING: m_log_12 reentrant log hung (child killed "
                   "at 1.5s ceiling)\n");
            ok = 0;
        } else if (WIFSIGNALED(status)) {
            printf("FINDING: m_log_12 reentrant log crashed with signal %d\n",
                   WTERMSIG(status));
            ok = 0;
        } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            printf("FINDING: m_log_12 reentrant log failed setup/deadlock "
                   "(exit %d)\n", WEXITSTATUS(status));
            ok = 0;
        } else if (handler_count <= 0) {
            printf("note: m_log_12 signal timing inconclusive "
                   "(handler count=%d)\n", handler_count);
        } else {
            printf("note: m_log_12 handler fired %d time(s); no deadlock "
                   "or crash observed in this run\n", handler_count);
        }
    } else {
        close(fds[0]);
        close(fds[1]);
        printf("note: m_log_12 fork failed; reentrancy check skipped\n");
    }
    cplus_libc_log_set_enabled(0);
    return ok;
}

int main(void)
{
    int pass = 0;
    int total = 0;
    (void)setvbuf(stdout, NULL, _IONBF, 0);

#define RUN(fn) do { ++total; if ((fn)()) ++pass; } while (0)
    RUN(m_log_1_default_off);
    RUN(m_log_2_enable_lines);
    RUN(m_log_3_disable_silence);
    RUN(m_log_4_null_substitution);
    RUN(m_log_5_long_bounded);
    RUN(m_log_6_no_sensitive_data);
    RUN(m_log_7_closed_stderr);
    RUN(m_log_8_concurrency);
    RUN(m_log_9_returns_identical);
    RUN(m_log_10_no_allocation);
    RUN(m_log_11_error_paths);
    RUN(m_log_12_signal_child);
#undef RUN

    cplus_libc_log_set_enabled(0);
    printf("methods_logging: %d/%d checks ok\n", pass, total);
    return 0;
}
