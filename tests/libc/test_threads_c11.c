/* Standalone verification for the C11 <threads.h> adapter in
 * libc/src/thread/threads_c11.c. C99 + -pthread, no <threads.h> on this host.
 *
 * Covers: thread create/join (incl. thrd_exit result), current/equal/sleep/
 * yield/detach, mutex (plain+recursive, trylock, timedlock), condvar
 * signal/broadcast/timed-wait, tss (incl. thread-exit destructor), call_once,
 * error-code mapping, opt-in logging parity, and the canonical ISO spellings.
 * All shared state is protected by a mutex or by pthread_once; every thread is
 * joined before its results are read, so there are no test-side data races.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../libc/install-include/cplus_threads.h"

extern void cplus_libc_log_set_enabled(int);

#define LOG_PREFIX "[CPLUS-LIBC] "
#define CAP_LIMIT 8192u
static char cap_buf[CAP_LIMIT];

/* -------------------------------- stderr capture (fd 2, as logger uses write) */
#ifndef _WIN32
typedef struct { int saved, rd, wr, active; } Capture;

static int cap_start(Capture *c)
{
    int fds[2];
    c->saved = c->rd = c->wr = -1;
    c->active = 0;
    if (pipe(fds) != 0) return -1;
    c->rd = fds[0];
    c->wr = fds[1];
    c->saved = dup(STDERR_FILENO);
    if (c->saved < 0) { close(c->rd); close(c->wr); c->rd = c->wr = -1; return -1; }
    if (dup2(c->wr, STDERR_FILENO) < 0) {
        dup2(c->saved, STDERR_FILENO);
        close(c->saved); close(c->rd); close(c->wr);
        c->saved = c->rd = c->wr = -1;
        return -1;
    }
    { int fl = fcntl(c->rd, F_GETFL, 0);
      if (fl >= 0) (void)fcntl(c->rd, F_SETFL, fl | O_NONBLOCK); }
    c->active = 1;
    return 0;
}

static size_t cap_finish(Capture *c)
{
    size_t total = 0;
    if (c->active) {
        fflush(stderr);
        (void)dup2(c->saved, STDERR_FILENO); /* restore before any assert */
        close(c->saved); close(c->wr);
        c->saved = -1; c->wr = -1; c->active = 0;
    }
    for (;;) {
        ssize_t n;
        if (total >= CAP_LIMIT - 1u) break;
        n = read(c->rd, cap_buf + total, CAP_LIMIT - 1u - total);
        if (n > 0) { total += (size_t)n; continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break; /* EAGAIN: all currently buffered output drained */
    }
    cap_buf[total] = '\0';
    if (c->rd >= 0) { close(c->rd); c->rd = -1; }
    return total;
}
#endif /* !_WIN32 */

/* ------------------------------------------------------------ shared state */
static cplus_mtx_t g_mtx;
static long g_counter = 0;

static int counter_worker(void *arg)
{
    long n = *(long *)arg;
    long i;
    for (i = 0; i < n; ++i) {
        assert(cplus_mtx_lock(&g_mtx) == CPLUS_THRD_SUCCESS);
        ++g_counter;
        assert(cplus_mtx_unlock(&g_mtx) == CPLUS_THRD_SUCCESS);
    }
    return 42;
}

static int exit_worker(void *arg)
{
    (void)arg;
    cplus_thrd_exit(77); /* must not return */
    return -1;
}

static cplus_mtx_t g_cv_mtx;
static cplus_cnd_t g_cv;
static int g_ready = 0;

static int signal_waiter(void *arg)
{
    (void)arg;
    assert(cplus_mtx_lock(&g_cv_mtx) == CPLUS_THRD_SUCCESS);
    while (!g_ready)
        assert(cplus_cnd_wait(&g_cv, &g_cv_mtx) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_unlock(&g_cv_mtx) == CPLUS_THRD_SUCCESS);
    return 0;
}

static int timed_waiter(void *arg)
{
    struct timespec deadline;
    int rc;
    (void)arg;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_nsec += 100000000L; /* 100 ms */
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        deadline.tv_sec += 1;
    }
    assert(cplus_mtx_lock(&g_cv_mtx) == CPLUS_THRD_SUCCESS);
    rc = cplus_cnd_timedwait(&g_cv, &g_cv_mtx, &deadline);
    while (rc == CPLUS_THRD_SUCCESS) /* tolerate spurious wakeups */
        rc = cplus_cnd_timedwait(&g_cv, &g_cv_mtx, &deadline);
    assert(cplus_mtx_unlock(&g_cv_mtx) == CPLUS_THRD_SUCCESS);
    return rc;
}

/* --- tss: value is heap-owned, destructor runs on the worker's own exit --- */
static cplus_tss_t g_key;
static int g_dtor_calls = 0;

static void tss_dtor(void *p)
{
    free(p);
    ++g_dtor_calls;
}

static int tss_worker(void *arg)
{
    int *p;
    (void)arg;
    p = (int *)malloc(sizeof *p);
    assert(p != NULL);
    *p = 7;
    assert(cplus_tss_set(g_key, p) == CPLUS_THRD_SUCCESS);
    assert(*(int *)cplus_tss_get(g_key) == 7);
    return 0;
}

/* --- call_once ----------------------------------------------------------- */
static cplus_once_flag g_once = CPLUS_ONCE_FLAG_INIT;
static int g_once_runs = 0;

static void once_init(void) { ++g_once_runs; }
static void once_noop(void) { }

static int once_worker(void *arg)
{
    (void)arg;
    cplus_call_once(&g_once, once_init);
    return 0;
}

static int detach_worker(void *arg)
{
    (void)arg;
    cplus_thrd_yield();
    return 0;
}

/* --- (7b) detached start-context lifetime regression (audit F1) -----------
 * A detached worker must NOT read its start context out of the creating
 * thread's stack frame. Each worker is spawned from a short-lived frame that
 * is immediately poisoned, then sleeps 50 ms before touching its argument.
 * With a stack-resident context the worker would see a clobbered func/arg
 * (wild call / crash under ASan); with the heap-allocated context every worker
 * must finish with its payload intact. */
#define DETACH_ITERS 64
#define DETACH_MAGIC 0xC0FFEEu

typedef struct { unsigned magic; int value; } DetachPayload;

static cplus_mtx_t g_detach_mtx;
static cplus_cnd_t g_detach_cnd;
static int g_detach_done = 0;
static int g_detach_bad = 0;
static DetachPayload g_detach_payloads[DETACH_ITERS];

static int delayed_detach_worker(void *arg)
{
    DetachPayload *p = (DetachPayload *)arg;
    struct timespec nap;
    nap.tv_sec = 0;
    nap.tv_nsec = 50L * 1000L * 1000L; /* outlive the creating stack frame */
    (void)cplus_thrd_sleep(&nap, NULL);
    assert(cplus_mtx_lock(&g_detach_mtx) == CPLUS_THRD_SUCCESS);
    if (p->magic != DETACH_MAGIC || p->value < 0 || p->value >= DETACH_ITERS)
        ++g_detach_bad;
    ++g_detach_done;
    assert(cplus_cnd_broadcast(&g_detach_cnd) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_unlock(&g_detach_mtx) == CPLUS_THRD_SUCCESS);
    return 0;
}

/* Spawn + detach from a frame that dies on return. */
static void spawn_detach_one(int idx)
{
    cplus_thrd_t tdet;
    assert(cplus_thrd_create(&tdet, delayed_detach_worker,
                             &g_detach_payloads[idx]) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_detach(tdet) == CPLUS_THRD_SUCCESS);
}

/* Reuse/clobber the stack region the dead frame just occupied. */
static void poison_stack(void)
{
    volatile unsigned char buf[1024];
    size_t k;
    for (k = 0; k < sizeof buf; ++k) buf[k] = 0xA5u;
    (void)buf[sizeof buf - 1u]; /* keep the writes observable */
}

/* ---------------------------------------------- logging parity helper set */
typedef struct {
    int mtx_init;
    int mtx_trylock;
    int mtx_unlock;
    int cnd_init;
    int tss_create;
    int call_once_rc;
} LoggedResults;

static LoggedResults run_logged_set(void)
{
    cplus_thrd_t t;
    cplus_mtx_t m;
    cplus_cnd_t c;
    cplus_tss_t k;
    cplus_once_flag f = CPLUS_ONCE_FLAG_INIT;
    LoggedResults r;

    t = cplus_thrd_current();
    (void)cplus_thrd_equal(t, t);

    r.mtx_init = cplus_mtx_init(&m, CPLUS_MTX_PLAIN);
    r.mtx_trylock = cplus_mtx_trylock(&m);
    r.mtx_unlock = cplus_mtx_unlock(&m);
    cplus_mtx_destroy(&m);

    r.cnd_init = cplus_cnd_init(&c);
    cplus_cnd_destroy(&c);

    r.tss_create = cplus_tss_create(&k, NULL);
    cplus_tss_delete(k);

    cplus_call_once(&f, once_noop);
    r.call_once_rc = 1; /* call_once returns void; marker presence is checked */
    return r;
}

int main(void)
{
    cplus_thrd_t t1, t2, tw, te, tdet;
    cplus_tss_t bad_key;
    struct timespec zero, past, rem;
    long a1 = 1000, a2 = 1000;
    int r1 = -1, r2 = -1, rw = -1, re = -1, wt_rc = -1;
    int i, all_ok = 1;
    cplus_mtx_t rec;

    /* ---- (0) logging is OFF by default ---------------------------------- */
#ifndef _WIN32
    {
        Capture c;
        if (cap_start(&c) == 0) {
            (void)run_logged_set();
            (void)cap_finish(&c);
            assert(strstr(cap_buf, LOG_PREFIX) == NULL);
        }
    }
#endif

    /* ---- (1) current / equal / sleep / yield ---------------------------- */
    assert(cplus_thrd_equal(cplus_thrd_current(), cplus_thrd_current()) != 0);
    memset(&zero, 0, sizeof zero);
    assert(cplus_thrd_sleep(&zero, &rem) == 0);
    cplus_thrd_yield();

    /* ---- (2) create / join / result / mutex counter --------------------- */
    assert(cplus_mtx_init(&g_mtx, CPLUS_MTX_PLAIN) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_create(&t1, counter_worker, &a1) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_create(&t2, counter_worker, &a2) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_join(t1, &r1) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_join(t2, &r2) == CPLUS_THRD_SUCCESS);
    assert(g_counter == 2000);
    assert(r1 == 42 && r2 == 42);
    cplus_mtx_destroy(&g_mtx);

    assert(cplus_thrd_create(&te, exit_worker, NULL) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_join(te, &re) == CPLUS_THRD_SUCCESS);
    assert(re == 77); /* thrd_exit result visible through thrd_join */

    /* ---- (3) error codes ------------------------------------------------ */
    assert(cplus_mtx_init(&rec, 0x80) == CPLUS_THRD_ERROR); /* bad type flags */
    assert(cplus_mtx_init(&g_mtx, CPLUS_MTX_PLAIN) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_lock(&g_mtx) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_trylock(&g_mtx) == CPLUS_THRD_BUSY);    /* EBUSY mapping */
    assert(clock_gettime(CLOCK_REALTIME, &past) == 0);
    past.tv_sec -= 1; /* expired CLOCK_REALTIME deadline */
    assert(cplus_mtx_timedlock(&g_mtx, &past) == CPLUS_THRD_TIMEDOUT);
    assert(cplus_mtx_unlock(&g_mtx) == CPLUS_THRD_SUCCESS);
    cplus_mtx_destroy(&g_mtx);

    assert(cplus_mtx_init(&rec, CPLUS_MTX_RECURSIVE) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_lock(&rec) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_lock(&rec) == CPLUS_THRD_SUCCESS); /* recursive OK */
    assert(cplus_mtx_unlock(&rec) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_unlock(&rec) == CPLUS_THRD_SUCCESS);
    cplus_mtx_destroy(&rec);

    /* ---- (4) condvar: signal/broadcast + timed wait --------------------- */
    assert(cplus_mtx_init(&g_cv_mtx, CPLUS_MTX_PLAIN) == CPLUS_THRD_SUCCESS);
    assert(cplus_cnd_init(&g_cv) == CPLUS_THRD_SUCCESS);
    g_ready = 0; /* written before any waiter thread exists */
    assert(cplus_thrd_create(&tw, signal_waiter, NULL) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_lock(&g_cv_mtx) == CPLUS_THRD_SUCCESS);
    g_ready = 1;
    assert(cplus_cnd_signal(&g_cv) == CPLUS_THRD_SUCCESS);
    assert(cplus_cnd_broadcast(&g_cv) == CPLUS_THRD_SUCCESS);
    assert(cplus_mtx_unlock(&g_cv_mtx) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_join(tw, &rw) == CPLUS_THRD_SUCCESS);
    assert(rw == 0 && g_ready == 1);

    assert(cplus_thrd_create(&tw, timed_waiter, &wt_rc) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_join(tw, &wt_rc) == CPLUS_THRD_SUCCESS);
    assert(wt_rc == CPLUS_THRD_TIMEDOUT);

    assert(cplus_mtx_lock(&g_cv_mtx) == CPLUS_THRD_SUCCESS);
    assert(cplus_cnd_timedwait(&g_cv, &g_cv_mtx, &past) == CPLUS_THRD_TIMEDOUT);
    assert(cplus_mtx_unlock(&g_cv_mtx) == CPLUS_THRD_SUCCESS);
    cplus_cnd_destroy(&g_cv);
    cplus_mtx_destroy(&g_cv_mtx);

    /* ---- (5) tss: value, invalid key, destructor at thread exit --------- */
    assert(cplus_tss_create(&g_key, tss_dtor) == CPLUS_THRD_SUCCESS);
    assert(cplus_tss_get(g_key) == NULL); /* unset in main thread */
    bad_key = (cplus_tss_t)123456789UL;
    assert(cplus_tss_set(bad_key, (void *)1) == CPLUS_THRD_ERROR);
    assert(cplus_thrd_create(&tw, tss_worker, NULL) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_join(tw, NULL) == CPLUS_THRD_SUCCESS); /* destructor ran */
    assert(g_dtor_calls == 1);
    cplus_tss_delete(g_key);

    /* ---- (6) call_once from several threads ----------------------------- */
    {
        cplus_thrd_t once_threads[4];
        g_once_runs = 0;
        for (i = 0; i < 4; ++i)
            assert(cplus_thrd_create(&once_threads[i], once_worker, NULL) == CPLUS_THRD_SUCCESS);
        for (i = 0; i < 4; ++i)
            assert(cplus_thrd_join(once_threads[i], NULL) == CPLUS_THRD_SUCCESS);
        assert(g_once_runs == 1);
    }

    /* ---- (7) detach ------------------------------------------------------ */
    assert(cplus_thrd_create(&tdet, detach_worker, NULL) == CPLUS_THRD_SUCCESS);
    assert(cplus_thrd_detach(tdet) == CPLUS_THRD_SUCCESS);

    /* ---- (7b) detached start-context lifetime regression (audit F1) ----- */
    assert(cplus_mtx_init(&g_detach_mtx, CPLUS_MTX_PLAIN) == CPLUS_THRD_SUCCESS);
    assert(cplus_cnd_init(&g_detach_cnd) == CPLUS_THRD_SUCCESS);
    for (i = 0; i < DETACH_ITERS; ++i) {
        g_detach_payloads[i].magic = DETACH_MAGIC;
        g_detach_payloads[i].value = i;
        spawn_detach_one(i); /* creating frame dies here */
        poison_stack();      /* clobber that stack region immediately */
    }
    assert(cplus_mtx_lock(&g_detach_mtx) == CPLUS_THRD_SUCCESS);
    while (g_detach_done < DETACH_ITERS) {
        struct timespec deadline;
        int rc;
        assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
        deadline.tv_sec += 5; /* bounded wait: workers nap only 50 ms */
        rc = cplus_cnd_timedwait(&g_detach_cnd, &g_detach_mtx, &deadline);
        assert(rc == CPLUS_THRD_SUCCESS); /* timeout => a worker never finished */
    }
    assert(g_detach_done == DETACH_ITERS);
    assert(g_detach_bad == 0);
    assert(cplus_mtx_unlock(&g_detach_mtx) == CPLUS_THRD_SUCCESS);
    cplus_cnd_destroy(&g_detach_cnd);
    cplus_mtx_destroy(&g_detach_mtx);

    /* ---- (8) canonical ISO C11 spellings compile and work --------------- */
    {
        thrd_t canon = cplus_thrd_current();
        once_flag of = ONCE_FLAG_INIT;
        assert(thrd_equal(canon, canon) != 0);
        assert(thrd_success == 0 && thrd_busy == 1 && thrd_nomem == 2);
        assert(thrd_timedout == 3 && thrd_error == 4);
        assert(mtx_plain == 0 && mtx_recursive == 1 && mtx_timed == 2);
        assert(TSS_DTOR_ITERATIONS >= 1);
        cplus_call_once(&of, once_noop);
    }

    /* ---- (9) logging parity: identical results + per-API entry markers - */
#ifndef _WIN32
    {
        LoggedResults off_r, on_r;
        Capture c;
        cplus_libc_log_set_enabled(0);
        off_r = run_logged_set();
        if (cap_start(&c) == 0) {
            cplus_libc_log_set_enabled(1);
            on_r = run_logged_set();
            (void)cap_finish(&c); /* restores stderr before any assert */
        } else {
            cplus_libc_log_set_enabled(1);
            on_r = run_logged_set();
        }
        cplus_libc_log_set_enabled(0);
        assert(all_ok); /* keeps -Wall quiet about the accumulator below */
        assert(off_r.mtx_init == on_r.mtx_init);
        assert(off_r.mtx_trylock == on_r.mtx_trylock);
        assert(off_r.mtx_unlock == on_r.mtx_unlock);
        assert(off_r.cnd_init == on_r.cnd_init);
        assert(off_r.tss_create == on_r.tss_create);
        assert(off_r.call_once_rc == on_r.call_once_rc);
        assert(on_r.mtx_init == CPLUS_THRD_SUCCESS);
        assert(on_r.mtx_trylock == CPLUS_THRD_SUCCESS);
        assert(on_r.cnd_init == CPLUS_THRD_SUCCESS);
        assert(on_r.tss_create == CPLUS_THRD_SUCCESS);

        assert(strstr(cap_buf, LOG_PREFIX "thrd_current:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "thrd_equal:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "mtx_init:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "mtx_trylock:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "mtx_unlock:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "mtx_destroy:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "cnd_init:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "cnd_destroy:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "tss_create:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "tss_delete:") != NULL);
        assert(strstr(cap_buf, LOG_PREFIX "call_once:") != NULL);
    }
#endif

    puts("threads_c11: ok");
    return 0;
}
