/* Agent 2: UNTESTED condvar lifecycle coverage for stdlib/thread.hp.
 *
 * Self-checking and deterministic.  Every cvx_check() prints "<tag> <0|1>"
 * (1 = the asserted condition held).  The final line is
 * "EXTRA_THREAD_CONDVAR <checks> <failures>"; the process exits 0 only when
 * <failures> is 0.  The exact stdout is recorded in extra_thread_condvar_NOTES.md.
 *
 * Every section that can block runs with a SIGALRM watchdog (handler calls
 * _Exit(99)), so a defect that prevents a wake-up terminates deterministically
 * instead of hanging; the external `timeout 30` is a second bound.  Each owning
 * local has a globally unique name within main() -- the C+ RAII lowering
 * de-duplicates ownership by variable name, so reusing a name in a sibling
 * scope silently omits that object's destructor.
 *
 * Covered: construction (incl. injected alloc failure), destruction (explicit,
 * implicit, double drop), zero-init handles, wait with a real worker thread and
 * mutex, signal waking a waiter, broadcast waking three waiters, repeated
 * signal/broadcast with no waiter, invalid-handle return values, defined
 * post-drop no-op behavior, and alloc/free + fd leak checks.
 *
 * Skipped (documented in NOTES): drop() while a waiter is blocked -- POSIX
 * pthread_cond_destroy with waiters is undefined (EBUSY) and condvar::drop()
 * frees the backing storage without synchronising with the waiter.
 */
#include "stdlib/thread.hp"
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>

static int cvx_checks = 0;
static int cvx_failures = 0;

static void cvx_check(const char *tag, int cond) {
    cvx_checks++;
    if (!cond)
        cvx_failures++;
    print(tag, cond ? 1 : 0);
}

/* Watchdog: a blocked wait that is never woken must not hang the runner. */
static void cvx_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

/* Bounded open-fd count (no fd is opened by this test; used as a leak probe). */
static int cvx_count_fds(void) {
    int count = 0;
    int fd;
    for (fd = 0; fd < 256; fd++) {
        if (fcntl(fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

/* Single waiter: signal() must wake exactly the one blocked worker. */
typedef struct {
    mutex *m;
    condvar *cv;
    int ready;
    int sig_rc;
} sw_ctx;

static void *sw_worker(void *arg) {
    sw_ctx *c = (sw_ctx *)arg;
    mutex *lm = c->m;
    condvar *lc = c->cv;
    lm->lock();
    c->ready = 1;
    c->sig_rc = lc->signal();
    lm->unlock();
    return NULL;
}

/* Three waiters: broadcast() must wake all of them. */
typedef struct {
    mutex *m;
    condvar *cv;
    int started;
    int epoch;
    int woke;
} bc_ctx;

static void *bc_worker(void *arg) {
    bc_ctx *c = (bc_ctx *)arg;
    mutex *lm = c->m;
    condvar *lc = c->cv;
    lm->lock();
    c->started++;
    while (c->epoch == 0)
        lc->wait(lm);
    c->woke++;
    lm->unlock();
    return NULL;
}

int main(void) {
    int cvx_fd_before;

    signal(SIGALRM, cvx_on_alarm);
    cvx_fd_before = cvx_count_fds();

    /* ---- A. construction / explicit destruction ------------------------ */
    {
        condvar ca_cv = condvar();
        cvx_check("ca_construct_valid", ca_cv.is_valid() == 1);
        cvx_check("ca_signal_no_waiter", ca_cv.signal() == 0);
        ca_cv.drop();
        cvx_check("ca_after_drop_invalid", ca_cv.is_valid() == 0);
        ca_cv.drop();
        cvx_check("ca_double_drop_safe", ca_cv.is_valid() == 0);
    }

    /* ---- A2. constructor allocation-failure path ----------------------- */
    {
        mutex nf_zm; /* zero-initialised: handle == NULL */
        stdlib_alloc_fail_at(1);
        condvar nf_cv = condvar();
        cvx_check("nf_construct_fails", nf_cv.is_valid() == 0);
        cvx_check("nf_signal_noop", nf_cv.signal() == -1);
        cvx_check("nf_bcast_noop", nf_cv.broadcast() == -1);
        cvx_check("nf_wait_noop", nf_cv.wait(&nf_zm) == -1);
        nf_cv.drop();
        stdlib_alloc_fail_reset();
    }

    /* ---- B. zero-initialised condvar and invalid-handle args ----------- */
    {
        mutex zb_m;   /* zero-init mutex: handle == NULL */
        condvar zb_cv; /* zero-init condvar: cv == NULL */
        mutex zb_valid_m = mutex();
        condvar zb_valid_cv = condvar();
        cvx_check("zb_cv_invalid", zb_cv.is_valid() == 0);
        cvx_check("zb_cv_signal", zb_cv.signal() == -1);
        cvx_check("zb_cv_broadcast", zb_cv.broadcast() == -1);
        cvx_check("zb_cv_wait", zb_cv.wait(&zb_m) == -1);
        cvx_check("zb_cv_wait_null", zb_cv.wait(NULL) == -1);
        cvx_check("zb_wait_null_m", zb_valid_cv.wait(NULL) == -1);
        cvx_check("zb_wait_zero_m", zb_valid_cv.wait(&zb_m) == -1);
        zb_cv.drop();
        zb_cv.drop();
        cvx_check("zb_double_drop_safe", zb_cv.is_valid() == 0);
        zb_valid_cv.drop();
        zb_valid_m.drop();
    }

    /* ---- C. wait with a real worker thread; signal() wakes it ---------- */
    {
        mutex sw_m = mutex();
        condvar sw_cv = condvar();
        error sw_e = error();
        sw_ctx sw_shared;
        thread sw_t = thread();
        void *sw_res = NULL;
        int sw_wait_rc = -99;
        int sw_src, sw_jrc;

        sw_shared.m = &sw_m;
        sw_shared.cv = &sw_cv;
        sw_shared.ready = 0;
        sw_shared.sig_rc = -7;
        cvx_check("sw_valid", sw_m.is_valid() == 1 && sw_cv.is_valid() == 1);
        /* Hold the mutex across start() so the worker cannot set ready=1
         * before the main thread actually enters wait(); this makes the
         * single wait() and its return value deterministic. */
        sw_m.lock();
        sw_src = sw_t.start(sw_worker, &sw_shared, &sw_e);
        cvx_check("sw_start", sw_src == STDLIB_OK && sw_e.ok() &&
                                  sw_t.is_joinable() == 1);
        alarm(3);
        while (sw_shared.ready == 0)
            sw_wait_rc = sw_cv.wait(&sw_m);
        sw_m.unlock();
        alarm(0);
        sw_jrc = sw_t.join(&sw_res, &sw_e);
        cvx_check("sw_join", sw_jrc == STDLIB_OK && sw_e.ok() &&
                                 sw_res == NULL && sw_t.is_joinable() == 0);
        cvx_check("sw_wait_rc", sw_wait_rc == 0);
        cvx_check("sw_ready", sw_shared.ready == 1);
        cvx_check("sw_signal_rc", sw_shared.sig_rc == 0);
        sw_t.drop();
        sw_cv.drop();
        sw_m.drop();
    }

    /* ---- D. broadcast() wakes all three waiters ------------------------ */
    {
        mutex bc_m = mutex();
        condvar bc_cv = condvar();
        error bc_e = error();
        bc_ctx bc_shared;
        thread bc_t0 = thread();
        thread bc_t1 = thread();
        thread bc_t2 = thread();
        void *bc_res = NULL;
        int bc_s0, bc_s1, bc_s2, bc_brc;

        bc_shared.m = &bc_m;
        bc_shared.cv = &bc_cv;
        bc_shared.started = 0;
        bc_shared.epoch = 0;
        bc_shared.woke = 0;
        cvx_check("bc_valid", bc_m.is_valid() == 1 && bc_cv.is_valid() == 1);
        bc_s0 = bc_t0.start(bc_worker, &bc_shared, &bc_e);
        bc_s1 = bc_t1.start(bc_worker, &bc_shared, &bc_e);
        bc_s2 = bc_t2.start(bc_worker, &bc_shared, &bc_e);
        cvx_check("bc_starts", bc_s0 == STDLIB_OK && bc_s1 == STDLIB_OK &&
                                   bc_s2 == STDLIB_OK && bc_e.ok());
        alarm(5);
        bc_m.lock();
        /* Each worker increments `started` while holding the mutex and then
         * blocks inside wait() (which releases it).  Seeing started==3 under
         * the mutex therefore means all three are genuinely blocked. */
        while (bc_shared.started < 3) {
            bc_m.unlock();
            usleep(500);
            bc_m.lock();
        }
        cvx_check("bc_all_waiting", bc_shared.started == 3);
        bc_shared.epoch = 1;
        bc_brc = bc_cv.broadcast();
        bc_m.unlock();
        bc_t0.join(&bc_res, &bc_e);
        bc_t1.join(&bc_res, &bc_e);
        bc_t2.join(&bc_res, &bc_e);
        alarm(0);
        cvx_check("bc_broadcast_rc", bc_brc == 0);
        cvx_check("bc_all_woke", bc_shared.woke == 3);
        bc_t0.drop();
        bc_t1.drop();
        bc_t2.drop();
        bc_cv.drop();
        bc_m.drop();
    }

    /* SKIP: destroying a condvar with a blocked waiter is not a defined
     * contract (POSIX EBUSY / UB; condvar::drop() frees the storage without
     * synchronising with the waiter).  See extra_thread_condvar_NOTES.md. */
    print("SKIP drop_with_waiter reason=POSIX_cond_destroy_with_waiters_is_UB_EBUSY_and_drop_frees_storage_without_sync");

    /* ---- E. repeated signal / broadcast with no waiter ----------------- */
    {
        condvar rn_cv = condvar();
        int rn_ok = 1;
        int rn_i;
        for (rn_i = 0; rn_i < 8; rn_i++) {
            if (rn_cv.signal() != 0)
                rn_ok = 0;
        }
        for (rn_i = 0; rn_i < 4; rn_i++) {
            if (rn_cv.broadcast() != 0)
                rn_ok = 0;
        }
        cvx_check("rn_repeat_no_waiter", rn_ok == 1);
        rn_cv.drop();
    }

    /* ---- F. use after drop (defined: drop sets cv=NULL == zero-init) ---- */
    {
        mutex ua_m = mutex();
        condvar ua_cv = condvar();
        ua_cv.drop();
        cvx_check("ua_invalid", ua_cv.is_valid() == 0);
        cvx_check("ua_signal", ua_cv.signal() == -1);
        cvx_check("ua_broadcast", ua_cv.broadcast() == -1);
        cvx_check("ua_wait", ua_cv.wait(&ua_m) == -1);
        ua_cv.drop();
        ua_m.drop();
    }

    /* ---- G. resource-leak + deterministic-completion checks ------------ */
    {
        stdlib_telemetry_stats g_before;
        stdlib_telemetry_stats g_after;
        int g_i;
        stdlib_telemetry_set_enabled(1);
        stdlib_telemetry_reset();
        stdlib_telemetry_snapshot(&g_before);
        for (g_i = 0; g_i < 32; g_i++) {
            condvar g_cv = condvar();
            if (!g_cv.is_valid())
                break;
            g_cv.signal();
            g_cv.broadcast();
            g_cv.drop();
        }
        stdlib_telemetry_snapshot(&g_after);
        cvx_check("g_cycles_complete", g_i == 32);
        cvx_check("g_alloc_free_balanced",
                  (g_after.alloc_count - g_before.alloc_count) ==
                      (g_after.free_count - g_before.free_count));
        cvx_check("g_no_invalid_ops", g_after.invalid_ops == g_before.invalid_ops);
        cvx_check("g_fd_stable", cvx_count_fds() == cvx_fd_before);
        stdlib_telemetry_set_enabled(0);
    }

    print("EXTRA_THREAD_CONDVAR", cvx_checks, cvx_failures);
    return cvx_failures == 0 ? 0 : 1;
}
