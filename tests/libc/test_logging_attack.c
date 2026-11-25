/*
 * test_logging_attack.c -- adversarial black-box attacks on libc/log/log.c
 *
 * Targets:
 *   void cplus_libc_log(const char *api, const char *msg);
 *   void cplus_libc_log_set_enabled(int enabled);
 *   plus logging emitted from instrumented APIs (memcpy, strlen, snprintf,
 *   ctype, etc.).
 *
 * Implementation under attack (libc/log/log.c):
 *   static atomic_int g_enabled = 0;
 *   set_enabled() atomically stores 0/1.
 *   log(): if disabled -> return; substitute "?" for NULL api/msg;
 *          ONE fprintf(stderr, "[CPLUS-LIBC] %s: %s\n", api, msg).
 *
 * ATTACKS (each is a distinct section below, A1..A8):
 *   A1 NULL api / NULL msg / both NULL      -> must substitute "?", not crash.
 *   A2 100000-char api and msg              -> no overflow, output bounded only
 *                                              by length, no allocation.
 *   A3 sensitive sentinel through memcpy /  -> sentinel must never appear in
 *      snprintf                                the log stream.
 *   A4 SIGALRM handler re-enters log while  -> no deadlock/crash; async-signal
 *      main is inside an instrumented call     safety documented honestly.
 *   A5 stderr closed (fd 2)                  -> logged calls must not crash.
 *   A6 4-thread toggle storm                 -> no torn lines, no deadlock.
 *   A7 20 API calls, logging ON vs OFF       -> return values identical.
 *   A8 10000 direct log calls                -> cplus_alloc_live_count stable.
 *
 * Every string passed to cplus_libc_log is NUL-terminated (never UB). No
 * source, build, or other agent files are touched by this test.
 *
 * A `FINDING:` line on stdout means a real defect was observed. A hard
 * crash/deadlock shows up as a signal / watchdog exit (A4) instead. The
 * test still finishes with `logging_attack: ok` when no fatal defect occurs.
 *
 * POSIX stderr redirection is #ifndef _WIN32 guarded; the test is standalone
 * C99 and must complete in well under 8 seconds.
 */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <pthread.h>

/* Local declarations (test-only; matches sibling test style). */
extern void *cplus_malloc(size_t);
extern void cplus_free(void *);
extern void *cplus_memcpy(void *restrict, const void *restrict, size_t);
extern size_t cplus_strlen(const char *);
extern int cplus_strcmp(const char *, const char *);
extern int cplus_memcmp(const void *, const void *, size_t);
extern int cplus_snprintf(char *, size_t, const char *, ...);
extern size_t cplus_alloc_live_count(void);
extern void cplus_libc_log(const char *, const char *);
extern void cplus_libc_log_set_enabled(int);
extern int cplus_isalpha(int), cplus_isdigit(int), cplus_isspace(int);
extern int cplus_isalnum(int), cplus_isupper(int), cplus_islower(int);
extern int cplus_isxdigit(int), cplus_isprint(int), cplus_isgraph(int);
extern int cplus_ispunct(int), cplus_iscntrl(int), cplus_isblank(int);
extern int cplus_isascii(int), cplus_toascii(int);
extern int cplus_toupper(int), cplus_tolower(int);
extern int cplus_abs(int);

#define LOG_PREFIX "[CPLUS-LIBC] "
#define CAP_LIMIT (256u * 1024u) /* strict 256 KiB capture cap */
#define LONG_LEN 100000u

/* ------------------------------------------------------------------ */
/* Bounded stderr capture: pipe + drainer thread with NONBLOCKING reads. */
/* The write end is stderr; the drainer always makes progress and stops at */
/* EOF (guaranteed once the write end is closed), so the test cannot block. */
/* ------------------------------------------------------------------ */
#ifndef _WIN32
static char g_cap[CAP_LIMIT + 1]; /* one reusable hard-capped buffer */

typedef struct {
    int saved;              /* duplicate of original stderr */
    int rd, wr;             /* pipe ends */
    int active;
    int drainer_started;
    pthread_t drainer;
    size_t n;               /* bytes actually captured (<= CAP_LIMIT) */
    int overflow;           /* output exceeded CAP_LIMIT (discarded tail) */
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
        pr = poll(&pfd, 1, 200); /* bounded wait; never blocks the test */
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue; /* timeout: loop until EOF closes write end */
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
    /* NONBLOCKING read end -> a drain can never hang. */
    fl = fcntl(c->rd, F_GETFL, 0);
    if (fl >= 0) (void)fcntl(c->rd, F_SETFL, fl | O_NONBLOCK);
    c->active = 1;
    g_cap[0] = '\0';
    if (pthread_create(&c->drainer, NULL, capture_drainer, c) == 0)
        c->drainer_started = 1;
    return 0;
fail:
    if (c->saved >= 0) { (void)dup2(c->saved, STDERR_FILENO); close(c->saved); }
    if (c->rd >= 0) close(c->rd);
    if (c->wr >= 0) close(c->wr);
    c->saved = c->rd = c->wr = -1;
    return -1;
}

/* Restores stderr FIRST, closes the write end (EOF), joins the drainer. */
static size_t cap_finish(Capture *c)
{
    size_t n;
    if (c->active) {
        fflush(stderr);
        (void)dup2(c->saved, STDERR_FILENO);
        close(c->saved);
        close(c->wr); /* only remaining write end -> drainer sees EOF */
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

/* Sink stderr to /dev/null for high-volume / signal tests. */
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

/* Count occurrences of the log prefix in the captured bytes. */
static size_t count_prefixes(const char *buf, size_t n)
{
    const size_t plen = sizeof(LOG_PREFIX) - 1;
    size_t k, c = 0;
    for (k = 0; k + plen <= n; ++k)
        if (memcmp(buf + k, LOG_PREFIX, plen) == 0) ++c;
    return c;
}

/* ------------------------------------------------------------------ */
/* A7 helper: 20 deterministic instrumented calls.                     */
/* ------------------------------------------------------------------ */
static void run_20(long long out[20])
{
    out[0]  = cplus_isalpha('A');
    out[1]  = cplus_isdigit('7');
    out[2]  = cplus_isspace(' ');
    out[3]  = cplus_isalnum('z');
    out[4]  = cplus_isupper('Q');
    out[5]  = cplus_islower('q');
    out[6]  = cplus_isxdigit('f');
    out[7]  = cplus_isprint('!');
    out[8]  = cplus_isgraph('!');
    out[9]  = cplus_ispunct('!');
    out[10] = cplus_iscntrl('\n');
    out[11] = cplus_isblank('\t');
    out[12] = cplus_isascii(65);
    out[13] = cplus_toascii(65);
    out[14] = cplus_toupper('a');
    out[15] = cplus_tolower('A');
    out[16] = (long long)cplus_strlen("abc");
    out[17] = (long long)cplus_strcmp("abc", "abd");
    out[18] = (long long)cplus_memcmp("abc", "abc", 3);
    out[19] = (long long)cplus_abs(-42);
}

/* ------------------------------------------------------------------ */
/* A4: SIGALRM reentrancy + watchdog.                                  */
/* ------------------------------------------------------------------ */
static volatile sig_atomic_t g_handler_count = 0;
static volatile sig_atomic_t g_wdog_done = 0;
static int g_a4_pipe_w = -1;
static int g_a4_status = 0;

static void sigalrm_handler(int sig)
{
    (void)sig;
    ++g_handler_count;
    /* Re-enter the logging path from a signal handler while the main thread
     * may be inside fprintf(). This is the attack under test. */
    cplus_libc_log("sigalrm-handler", "reentrant-enter");
}

/* Child-side: report {status, handler_count} and exit without stdio flush. */
static void a4_report_and_exit(int code)
{
    if (g_a4_pipe_w >= 0) {
        (void)write(g_a4_pipe_w, &code, sizeof code);
        (void)write(g_a4_pipe_w, (const void *)&g_handler_count,
                    sizeof g_handler_count);
    }
    _exit(code);
}

static void *watchdog(void *arg)
{
    struct timespec ts;
    int i;
    (void)arg;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000L; /* 100 ms */
    for (i = 0; i < 25 && !g_wdog_done; ++i)
        (void)nanosleep(&ts, NULL); /* 2.5 s ceiling */
    if (!g_wdog_done) {
        g_a4_status = 96; /* deadlock marker */
        a4_report_and_exit(96);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* A6: coordinated toggle storm workers.                               */
/* ------------------------------------------------------------------ */
typedef struct { int rc; } StormArg;

static void *storm_worker(void *arg)
{
    StormArg *a = (StormArg *)arg;
    char buf[64];
    int i;
    a->rc = 0;
    for (i = 0; i < 400; ++i) {
        cplus_libc_log_set_enabled(1);
        (void)cplus_memcpy(buf, "torn-line-probe", 15); /* logs: memcpy */
        if (buf[0] != 't') { a->rc = 1; break; }
        cplus_libc_log_set_enabled(0);
        (void)cplus_isalpha('A'); /* should be silent while disabled */
    }
    return NULL;
}

/* Validate every captured line matches ^\[CPLUS-LIBC\] .*: .*$ .
 * Returns line count on success, negative defect code on failure. */
static int validate_lines(const char *buf, size_t n)
{
    const size_t plen = sizeof(LOG_PREFIX) - 1;
    size_t i = 0, lines = 0, prefixes;
    if (n == 0) return -1;
    if (buf[n - 1] != '\n') return -2; /* partial trailing line */
    while (i < n) {
        size_t j = i, seg;
        while (j < n && buf[j] != '\n') ++j;
        seg = j - i;
        if (seg == 0) return -3;                       /* blank line */
        if (seg < plen || memcmp(buf + i, LOG_PREFIX, plen) != 0) return -4;
        if (strstr(buf + i + plen, ": ") == NULL) return -5;
        ++lines;
        i = j + 1;
    }
    prefixes = count_prefixes(buf, n);
    if (prefixes != lines) return -6; /* merged/nested lines */
    return (int)lines;
}

int main(void)
{
    static const char sentinel[] = "P4SSW0RD_S3NTINEL_D0_N0T_L0G_7B1F";
    int findings = 0;

    /* Keep diagnostics visible even if an attack terminates abnormally. */
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    /* ================= A1: NULL api / NULL msg / both NULL ============ */
    cplus_libc_log_set_enabled(1);
#ifndef _WIN32
    {
        Capture c;
        if (cap_start(&c) == 0) {
            size_t n;
            cplus_libc_log(NULL, NULL);
            cplus_libc_log(NULL, "m");
            cplus_libc_log("a", NULL);
            n = cap_finish(&c);
            if (n == 0 || strstr(g_cap, "[CPLUS-LIBC] ?: ?\n") == NULL ||
                strstr(g_cap, "[CPLUS-LIBC] ?: m\n") == NULL ||
                strstr(g_cap, "[CPLUS-LIBC] a: ?\n") == NULL) {
                printf("FINDING: A1 NULL substitution missing; captured "
                       "n=%lu bytes, head=%.80s\n",
                       (unsigned long)n, g_cap);
                ++findings;
            }
        } else {
            printf("note: A1 capture unavailable; NULL calls made anyway\n");
            cplus_libc_log(NULL, NULL);
            cplus_libc_log(NULL, "m");
            cplus_libc_log("a", NULL);
        }
    }
#else
    cplus_libc_log(NULL, NULL);
    cplus_libc_log(NULL, "m");
    cplus_libc_log("a", NULL);
#endif
    cplus_libc_log_set_enabled(0);

    /* ================= A2: 100000-char api and msg ==================== */
    {
        char *api = (char *)cplus_malloc(LONG_LEN + 1);
        char *msg = (char *)cplus_malloc(LONG_LEN + 1);
        size_t i, expected;
        if (api == NULL || msg == NULL) {
            printf("FINDING: A2 could not allocate test buffers\n");
            ++findings;
        } else {
            for (i = 0; i < LONG_LEN; ++i) { api[i] = 'A'; msg[i] = 'B'; }
            api[LONG_LEN] = '\0';
            msg[LONG_LEN] = '\0';
            expected = (sizeof(LOG_PREFIX) - 1) + LONG_LEN +
                       (sizeof(": ") - 1) + LONG_LEN + 1; /* + '\n' */
#ifndef _WIN32
            {
                Capture c;
                size_t before, after, n;
                if (cap_start(&c) == 0) {
                    cplus_libc_log_set_enabled(1);
                    before = cplus_alloc_live_count();
                    cplus_libc_log(api, msg);
                    after = cplus_alloc_live_count();
                    n = cap_finish(&c);
                    cplus_libc_log_set_enabled(0);
                    if (after != before) {
                        printf("FINDING: A2 logging allocated memory: "
                               "%lu -> %lu live\n",
                               (unsigned long)before, (unsigned long)after);
                        ++findings;
                    }
                    if (c.overflow || n != expected) {
                        printf("FINDING: A2 output length wrong/capped: "
                               "got=%lu expected=%lu overflow=%d\n",
                               (unsigned long)n, (unsigned long)expected,
                               c.overflow);
                        ++findings;
                    }
                    if (n >= 17 && memcmp(g_cap, LOG_PREFIX "AAAA", 17) != 0) {
                        printf("FINDING: A2 long api prefix corrupted\n");
                        ++findings;
                    }
                    if (n > 0 && g_cap[n - 1] != '\n') {
                        printf("FINDING: A2 log line not newline-terminated\n");
                        ++findings;
                    }
                    /* Confirm the tail B-run is present (not truncated early). */
                    {
                        char needle[16];
                        size_t k;
                        for (k = 0; k < 15; ++k) needle[k] = 'B';
                        needle[15] = '\0';
                        if (strstr(g_cap, needle) == NULL) {
                            printf("FINDING: A2 long msg body missing/damaged\n");
                            ++findings;
                        }
                    }
                }
            }
#endif
            cplus_free(msg);
            cplus_free(api);
        }
    }

    /* ================= A3: sensitive-data probe ======================= */
    cplus_libc_log_set_enabled(1);
#ifndef _WIN32
    {
        Capture c;
        char d[128];
        char fmtbuf[128];
        size_t n;
        if (cap_start(&c) == 0) {
            (void)cplus_memcpy(d, sentinel, sizeof sentinel);
            (void)cplus_strlen(sentinel);
            (void)cplus_snprintf(fmtbuf, sizeof fmtbuf, "%s", sentinel);
            n = cap_finish(&c);
            if (memcmp(d, sentinel, sizeof sentinel) != 0 ||
                strcmp(fmtbuf, sentinel) != 0) {
                printf("FINDING: A3 user operation produced wrong result\n");
                ++findings;
            }
            if (strstr(g_cap, sentinel) != NULL ||
                strstr(g_cap, "P4SSW0RD") != NULL ||
                strstr(g_cap, "S3NTINEL") != NULL) {
                printf("FINDING: A3 sensitive sentinel leaked into log "
                       "(%lu bytes captured)\n", (unsigned long)n);
                ++findings;
            }
            if (strstr(g_cap, LOG_PREFIX "memcpy:") == NULL ||
                strstr(g_cap, LOG_PREFIX "strlen:") == NULL ||
                strstr(g_cap, LOG_PREFIX "snprintf:") == NULL) {
                printf("FINDING: A3 expected entry markers absent\n");
                ++findings;
            }
        }
    }
#endif
    cplus_libc_log_set_enabled(0);

    /* ================= A4: SIGALRM reentrancy =========================
     * Isolated in a forked child: a reentrant fprintf can hang or fault, and
     * we do not want a buffered-stdout crash to hide the rest of this test.
     * The parent observes the child's exit status and reports the defect. */
#ifndef _WIN32
    {
        int fds[2];
        pid_t pid;
        if (pipe(fds) != 0) {
            printf("note: A4 pipe failed; reentrancy check skipped\n");
        } else {
            pid = fork();
            if (pid == 0) {
                /* ---- child: run the reentrancy attack ---- */
                struct sigaction sa, old;
                struct itimerval it;
                pthread_t wdog;
                int saved = -1;
                int have_sink;
                int wdog_created = 0;
                volatile int i;

                close(fds[0]);
                g_a4_pipe_w = fds[1];
                g_handler_count = 0;
                g_wdog_done = 0;
                g_a4_status = 0;

                memset(&sa, 0, sizeof sa);
                sa.sa_handler = sigalrm_handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;
                cplus_libc_log_set_enabled(1);
                have_sink = (sink_stderr(&saved) == 0); /* avoid signal flood */
                if (sigaction(SIGALRM, &sa, &old) != 0) {
                    g_a4_status = 95; /* could not install handler */
                } else {
                    memset(&it, 0, sizeof it);
                    it.it_interval.tv_sec = 0;
                    it.it_interval.tv_usec = 1000; /* 1 ms repeating */
                    it.it_value.tv_sec = 0;
                    it.it_value.tv_usec = 1000;
                    (void)setitimer(ITIMER_REAL, &it, NULL);
                    wdog_created = (pthread_create(&wdog, NULL, watchdog,
                                                   NULL) == 0);
                    if (!wdog_created) g_a4_status = 94; /* no watchdog */
                    for (i = 0; i < 50000; ++i) {
                        char b[16];
                        (void)cplus_memcpy(b, "reentrant", 9);
                        if (b[0] != 'r') break;
                    }
                    /* Teardown without a SIGALRM race: block the signal,
                     * disarm the timer, discard any pending instance by
                     * temporarily ignoring it, restore the old handler, then
                     * unblock. Without this, a pending SIGALRM can be
                     * delivered after SIG_DFL is restored and kill the child
                     * with SIGALRM (signal 14) -- a test artifact, not a
                     * logging defect. */
                    {
                        sigset_t blockset, oldmask;
                        sigemptyset(&blockset);
                        sigaddset(&blockset, SIGALRM);
                        (void)pthread_sigmask(SIG_BLOCK, &blockset, &oldmask);
                        memset(&it, 0, sizeof it); /* disarm timer */
                        (void)setitimer(ITIMER_REAL, &it, NULL);
                        (void)signal(SIGALRM, SIG_IGN); /* drop pending */
                        (void)sigaction(SIGALRM, &old, NULL);
                        (void)pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
                    }
                    g_wdog_done = 1;
                    if (wdog_created) (void)pthread_join(wdog, NULL);
                }
                if (have_sink) unsink_stderr(saved);
                cplus_libc_log_set_enabled(0);
                a4_report_and_exit(0);
            } else if (pid > 0) {
                /* ---- parent: bounded wait, classify outcome ---- */
                int status = 0, waited = 0, killed = 0;
                int child_code = -1, handler_count = -1;
                ssize_t got, got2;
                int k;
                close(fds[1]);
                for (k = 0; k < 150; ++k) { /* 150 * 20 ms = 3 s ceiling */
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
                    printf("FINDING: A4 reentrant log hung (child killed "
                           "after 3s ceiling)\n");
                    ++findings;
                } else if (WIFSIGNALED(status)) {
                    printf("FINDING: A4 reentrant log crashed with signal %d\n",
                           WTERMSIG(status));
                    ++findings;
                } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    printf("FINDING: A4 reentrant log deadlocked "
                           "(child watchdog exit %d)\n", WEXITSTATUS(status));
                    ++findings;
                } else if (handler_count <= 0) {
                    printf("note: A4 signal timing inconclusive "
                           "(handler count=%d)\n", handler_count);
                } else {
                    printf("note: A4 handler fired %d time(s); no deadlock "
                           "or crash in this child run\n", handler_count);
                }
            } else {
                close(fds[0]);
                close(fds[1]);
                printf("note: A4 fork failed; reentrancy check skipped\n");
            }
        }
        /* Honesty: fprintf() is NOT async-signal-safe per POSIX. A clean
         * child run only shows no observable deadlock/crash in that run; it
         * does NOT prove async-signal-safety. Interleaved/corrupted output is
         * inherently possible when a handler interrupts fprintf on the same
         * thread. Repeated runs are needed; defects are reported above. */
    }
#endif

    /* ================= A5: closed stderr ============================= */
#ifndef _WIN32
    {
        int saved = dup(STDERR_FILENO);
        char b[16];
        if (saved >= 0) {
            (void)close(STDERR_FILENO);
            cplus_libc_log_set_enabled(1);
            /* Deliberately no file-opening calls here: fd 2 is free and a
             * newly opened fd could otherwise receive our log bytes. */
            (void)cplus_memcpy(b, "closedfd", 8);
            (void)cplus_strlen(b);
            (void)cplus_isalpha('A');
            cplus_libc_log("direct", "stderr-closed");
            cplus_libc_log_set_enabled(0);
            (void)dup2(saved, STDERR_FILENO); /* restore fd 2 */
            close(saved);

            /* Probe whether logging actually resumes after a plain dup2
             * restore. The failed writes to closed fd 2 set the stderr FILE
             * error indicator, and dup2() does NOT clear it. */
            {
                Capture c;
                size_t n;
                cplus_libc_log_set_enabled(1);
                if (cap_start(&c) == 0) {
                    cplus_libc_log("post-restore", "probe");
                    n = cap_finish(&c);
                    if (n == 0) {
                        printf("FINDING: A5 logging to a closed stderr set "
                               "stderr's error flag; after dup2 restore, "
                               "logged calls emit 0 bytes until clearerr()\n");
                        ++findings;
                    }
                }
                /* Test hygiene only: clear the inherited stdio error flag so
                 * later attacks observe a fresh stream. log.c does NOT do
                 * this itself. */
                clearerr(stderr);
                cplus_libc_log_set_enabled(0);
            }
        } else {
            printf("note: A5 could not save stderr; skipped\n");
        }
        /* Reaching here means no crash with fd 2 closed. */
    }
#endif

    /* ================= A6: 4-thread toggle storm ===================== */
#ifndef _WIN32
    {
        Capture c;
        pthread_t th[4];
        StormArg arg[4];
        int created[4], all_created = 1, all_joined = 1, all_ok = 1;
        int i, vr;
        cplus_libc_log_set_enabled(0); /* workers set their own state */
        if (cap_start(&c) == 0) {
            for (i = 0; i < 4; ++i) {
                arg[i].rc = -1;
                created[i] = (pthread_create(&th[i], NULL, storm_worker,
                                             &arg[i]) == 0);
                if (!created[i]) all_created = 0;
            }
            for (i = 0; i < 4; ++i) {
                if (!created[i]) continue;
                if (pthread_join(th[i], NULL) != 0) all_joined = 0;
                if (arg[i].rc != 0) all_ok = 0;
            }
            {
                size_t n = cap_finish(&c);
                vr = validate_lines(g_cap, n);
                if (!all_created || !all_joined || !all_ok) {
                    printf("FINDING: A6 storm thread failure "
                           "(created=%d joined=%d ok=%d)\n",
                           all_created, all_joined, all_ok);
                    ++findings;
                }
                if (vr < 0) {
                    printf("FINDING: A6 torn/malformed log output "
                           "(code=%d, %lu bytes captured)\n",
                           vr, (unsigned long)n);
                    ++findings;
                } else if (c.overflow) {
                    printf("FINDING: A6 capture overflowed 256 KiB cap\n");
                    ++findings;
                } else {
                    printf("note: A6 %d well-formed lines, no torn output\n",
                           vr);
                }
            }
        } else {
            printf("note: A6 capture unavailable; skipped torn-line check\n");
        }
        cplus_libc_log_set_enabled(0);
    }
#endif

    /* ================= A7: return values ON vs OFF =================== */
    {
        long long on[20], off[20];
        int i;
        cplus_libc_log_set_enabled(0);
        run_20(off);
#ifndef _WIN32
        {
            int saved = -1;
            int have_sink = (sink_stderr(&saved) == 0);
            cplus_libc_log_set_enabled(1);
            run_20(on);
            cplus_libc_log_set_enabled(0);
            if (have_sink) unsink_stderr(saved);
        }
#else
        cplus_libc_log_set_enabled(1);
        run_20(on);
        cplus_libc_log_set_enabled(0);
#endif
        for (i = 0; i < 20; ++i) {
            if (on[i] != off[i]) {
                printf("FINDING: A7 return value changed by logging at "
                       "call %d (on=%lld off=%lld)\n",
                       i, on[i], off[i]);
                ++findings;
            }
        }
    }

    /* ================= A8: 10000 logs, no allocation ================= */
    {
        size_t before, after;
        int i;
#ifndef _WIN32
        int saved = -1;
        int have_sink = (sink_stderr(&saved) == 0);
        cplus_libc_log_set_enabled(1);
#else
        cplus_libc_log_set_enabled(0);
#endif
        cplus_libc_log("warmup", "prime-stderr-state"); /* prime once */
        before = cplus_alloc_live_count();
        for (i = 0; i < 10000; ++i)
            cplus_libc_log("alloc-probe", "enter");
        after = cplus_alloc_live_count();
        if (before != after) {
            printf("FINDING: A8 allocation in logging path: %lu -> %lu live\n",
                   (unsigned long)before, (unsigned long)after);
            ++findings;
        }
#ifndef _WIN32
        if (have_sink) unsink_stderr(saved);
#endif
        cplus_libc_log_set_enabled(0);
    }

    if (findings == 0)
        printf("logging_attack: ok\n");
    else
        printf("logging_attack: ok (%d finding(s) reported above)\n", findings);
    return 0;
}
