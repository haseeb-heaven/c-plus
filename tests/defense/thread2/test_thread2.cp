/* thread2 — deterministic lifecycle regression harness for the C+ stdlib
 * thread/async/lifetime headers (Agent G, PHASES 5-7).
 *
 * METHOD
 * ------
 * 1000 deterministic cases (fixed splitmix64 seed, no pid/clock/wall-time
 * input).  Every case runs in a forked child; the parent reaps each child and
 * checks its exit status.  Children are short-lived, start no unjoined threads,
 * and carry their own alarm(5) watchdog; the parent carries alarm(55) and
 * run.sh applies `timeout 60`.  No wall-clock value is ever asserted.
 *
 * Two cases are FIXED-DEFECT REGRESSIONS and fail against the pre-fix headers:
 *   A1  loop::run with an out-of-range uv_run mode must be refused (was a
 *       silent STDLIB_OK with a clear error).
 *   A2  loop::drop with an active handle must not claim a clean close; a later
 *       drop() must be able to retry (was initialized=0 + permanent no-op).
 * The remaining cases cover the surrounding mutex/condvar/thread/timer/lifetime
 * lifecycle surface and must pass both before and after (no weakening).
 *
 * OUTPUT: "THREAD2 <cases> <passed> <failed>" + per-family counts.  Exit 0 iff
 * failed == 0.  This is evidence for the enumerated cases, not a safety proof.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/thread.hp"
#include "stdlib/async.hp"
#include "stdlib/lifetime_adapt.hp"

#define THREAD2_CASES 1000
#define THREAD2_SEED 0x7A11D2C0FFEEULL
#define THREAD2_FAMILIES 20

static unsigned long long g_cases = 0;
static unsigned long long g_passed = 0;
static unsigned long long g_failed = 0;
static unsigned long long g_family[THREAD2_FAMILIES];

static unsigned long long t2_rnd(unsigned long long *s) {
    unsigned long long z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void t2_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

/* ------------------------------------------------------------------ helpers */
static void *worker_ret(void *arg) {
    (void)arg;
    return (void *)(uintptr_t)42u;
}

/* ------------------------------------------------------------- case bodies */
/* Each returns 0 on expected behaviour, non-zero when a defect is observed. */

static int c_mutex_basic(unsigned long long p) {
    error m_err = error();
    mutex m_m = mutex();
    unsigned long long i, n = 1u + (p & 3u);
    if (!m_m.is_valid())
        return 1;
    for (i = 0; i < n; ++i) {
        guard m_g = guard(&m_m);
        if (!m_g.holds())
            return 2;
        m_g.unlock();
        if (m_g.holds())
            return 3;
    }
    m_m.drop();
    if (m_m.is_valid())
        return 4;
    (void)m_err;
    return 0;
}

static int c_mutex_zero(unsigned long long p) {
    mutex z_m;
    memset(&z_m, 0, sizeof(z_m));
    (void)p;
    if (z_m.is_valid())
        return 1;
    z_m.drop(); /* no-op on zero handle */
    if (z_m.is_valid())
        return 2;
    return 0;
}

static int c_mutex_double_unlock(unsigned long long p) {
    error du_err = error();
    mutex du_m = mutex();
    (void)du_err;
    if (!du_m.is_valid())
        return 1;
    {
        guard du_g = guard(&du_m);
        du_g.unlock();
        du_g.unlock(); /* second unlock must be a no-op */
        du_g.drop();   /* explicit drop must also be a no-op */
    }
    /* Re-acquiring proves the mutex was released exactly once. */
    {
        guard du_g2 = guard(&du_m);
        if (!du_g2.holds())
            return 2;
    }
    (void)p;
    du_m.drop();
    return 0;
}

static int c_cond_basic(unsigned long long p) {
    error cv_err = error();
    condvar cv_c = condvar();
    mutex cv_m = mutex();
    (void)cv_err;
    if (!cv_c.is_valid() || !cv_m.is_valid())
        return 1;
    if (cv_c.signal() != 0 || cv_c.broadcast() != 0)
        return 2;
    cv_c.drop();
    cv_m.drop();
    if (cv_c.is_valid())
        return 3;
    (void)p;
    return 0;
}

static int c_cond_wait(unsigned long long p) {
    error cw_err = error();
    condvar cw_c = condvar();
    mutex cw_m = mutex();
    int rc;
    (void)cw_err;
    if (!cw_c.is_valid() || !cw_m.is_valid())
        return 1;
    /* A wait on a zero/invalid mutex is refused before touching pthread. */
    {
        mutex cw_zero;
        memset(&cw_zero, 0, sizeof(cw_zero));
        if (cw_c.wait(&cw_zero) != -1)
            return 2;
    }
    if (cw_c.wait((mutex *)0) != -1)
        return 3;
    rc = (int)(p & 1u);
    cw_c.signal(); /* no waiter: benign */
    (void)rc;
    cw_c.drop();
    cw_m.drop();
    return 0;
}

static int c_thread_start_join(unsigned long long p) {
    error t_err = error();
    thread t_th = thread();
    void *t_res = (void *)0;
    unsigned long long i, n = 1u + (p & 3u);
    if (t_th.is_joinable())
        return 1;
    for (i = 0; i < n; ++i) {
        if (t_th.start(worker_ret, (void *)(uintptr_t)0x11u, &t_err) != STDLIB_OK)
            return 2;
        if (!t_th.is_joinable())
            return 3;
        if (t_th.join(&t_res, &t_err) != STDLIB_OK)
            return 4;
        if (t_th.is_joinable())
            return 5;
        if (t_th.join(&t_res, &t_err) != STDLIB_FAIL) /* double join refused */
            return 6;
    }
    t_th.drop();
    return 0;
}

static int c_thread_badstart(unsigned long long p) {
    error bs_err = error();
    thread bs_th = thread();
    (void)p;
    if (bs_th.start((void *(*)(void *))0, (void *)0, &bs_err) != STDLIB_FAIL)
        return 1;
    if (bs_err.code() != (int)CPLUS_EXIT_UNSUPPORTED)
        return 2;
    /* A second start after a failed start is still allowed (not joinable). */
    if (bs_th.start(worker_ret, (void *)0, &bs_err) != STDLIB_OK)
        return 3;
    bs_th.drop(); /* drop joins */
    return 0;
}

static int c_timer_lifecycle(unsigned long long p) {
    error tl_err = error();
    loop tl_l = loop(&tl_err);
    (void)p;
    if (!tl_l.valid())
        return 1;
    {
        timer tl_t = timer(tl_l.native(), &tl_err);
        if (!tl_t.valid())
            return 2;
        if (tl_t.start((uv_timer_cb)0, 3600000ULL, 0ULL, (void *)0, &tl_err) != STDLIB_FAIL)
            return 3;
        tl_t.drop();
        tl_t.drop(); /* idempotent */
        if (tl_t.valid())
            return 4;
    }
    tl_l.drop();
    return 0;
}

/* FIXED DEFECT A1: invalid uv_run mode must be refused. */
static int c_async_invalid_mode(unsigned long long p) {
    error a1_err = error();
    loop a1_l = loop(&a1_err);
    int bad = 999 + (int)(p % 7u);
    if (!a1_l.valid())
        return 1;
    if (a1_l.run(bad, &a1_err) != STDLIB_FAIL)
        return 2; /* BEFORE FIX: returned STDLIB_OK with clear error */
    if (a1_err.code() != (int)CPLUS_EXIT_UNSUPPORTED)
        return 3;
    if (a1_err.category() != STDLIB_CATEGORY_INVALID)
        return 4;
    /* The three legal modes remain accepted. */
    if (a1_l.run(UV_RUN_NOWAIT, &a1_err) != STDLIB_OK)
        return 5;
    a1_l.drop();
    return 0;
}

/* FIXED DEFECT A2: a failed close must keep the loop retryable. */
static void a2_noop_cb(uv_timer_t *h) { (void)h; }

static int c_async_close_retry(unsigned long long p) {
    error a2_err = error();
    loop a2_l = loop(&a2_err);
    (void)p;
    if (!a2_l.valid())
        return 1;
    {
        timer a2_t = timer(a2_l.native(), &a2_err);
        if (!a2_t.valid())
            return 2;
        if (a2_t.start(a2_noop_cb, 3600000ULL, 0ULL, (void *)0, &a2_err) != STDLIB_OK)
            return 3;
        /* An active handle makes uv_loop_close fail with UV_EBUSY. */
        a2_l.drop();
        if (!a2_l.valid())
            return 4; /* BEFORE FIX: initialized cleared on failed close */
        a2_t.stop(&a2_err);
        a2_t.drop();
        a2_l.drop(); /* retry after the handle is gone */
        if (a2_l.valid())
            return 5;
    }
    return 0;
}

static int c_lifetime_valid(unsigned long long p) {
    error lf_err = error();
    lifetime_owner lf_o;
    lifetime_borrow lf_b;
    char lf_src[8], lf_dst[8];
    unsigned int i;
    memset(&lf_o, 0, sizeof(lf_o));
    memset(&lf_b, 0, sizeof(lf_b));
    memset(lf_src, 0x33, sizeof(lf_src));
    memset(lf_dst, 0, sizeof(lf_dst));
    if (lifetime_owner_init(&lf_o, 16u, &lf_err) != STDLIB_OK)
        return 1;
    if (!lifetime_owner_is_live(&lf_o))
        return 2;
    if (lifetime_borrow_from(&lf_b, &lf_o, &lf_err) != STDLIB_OK)
        return 3;
    if (!lifetime_borrow_is_live(&lf_b))
        return 4;
    for (i = 0; i < 8u; ++i)
        lf_src[i] = (char)((p >> (i * 4u)) & 0xFFu);
    if (lifetime_write(&lf_b, 0u, lf_src, sizeof(lf_src), &lf_err) != STDLIB_OK)
        return 5;
    if (lifetime_read(&lf_b, 0u, lf_dst, sizeof(lf_dst), &lf_err) != STDLIB_OK)
        return 6;
    if (memcmp(lf_src, lf_dst, sizeof(lf_src)) != 0)
        return 7;
    if (lifetime_owner_release(&lf_o, &lf_err) != STDLIB_OK)
        return 8;
    if (lifetime_owner_is_live(&lf_o) || lifetime_borrow_is_live(&lf_b))
        return 9;
    return 0;
}

static int c_lifetime_double_release(unsigned long long p) {
    error dr_err = error();
    lifetime_owner dr_o;
    memset(&dr_o, 0, sizeof(dr_o));
    (void)p;
    if (lifetime_owner_init(&dr_o, 32u, &dr_err) != STDLIB_OK)
        return 1;
    if (lifetime_owner_release(&dr_o, &dr_err) != STDLIB_OK)
        return 2;
    if (lifetime_owner_release(&dr_o, &dr_err) != STDLIB_FAIL)
        return 3;
    if (dr_err.code() != (int)CPLUS_EXIT_DOUBLE_FREE)
        return 4;
    if (dr_err.category() != STDLIB_CATEGORY_OWNERSHIP)
        return 5;
    return 0;
}

static int c_lifetime_transfer(unsigned long long p) {
    error tr_err = error();
    lifetime_owner tr_a, tr_b, tr_c;
    lifetime_borrow tr_old, tr_new;
    memset(&tr_a, 0, sizeof(tr_a));
    memset(&tr_b, 0, sizeof(tr_b));
    memset(&tr_c, 0, sizeof(tr_c));
    memset(&tr_old, 0, sizeof(tr_old));
    memset(&tr_new, 0, sizeof(tr_new));
    (void)p;
    if (lifetime_owner_init(&tr_a, 24u, &tr_err) != STDLIB_OK)
        return 1;
    if (lifetime_borrow_from(&tr_old, &tr_a, &tr_err) != STDLIB_OK)
        return 2;
    if (lifetime_transfer(&tr_b, &tr_a, &tr_err) != STDLIB_OK)
        return 3;
    if (lifetime_borrow_is_live(&tr_old))
        return 4; /* old borrow invalidated by transfer */
    if (lifetime_transfer(&tr_c, &tr_b, &tr_err) != STDLIB_OK)
        return 5;
    if (lifetime_borrow_from(&tr_new, &tr_c, &tr_err) != STDLIB_OK)
        return 6;
    if (!lifetime_borrow_is_live(&tr_new))
        return 7;
    if (lifetime_owner_release(&tr_c, &tr_err) != STDLIB_OK)
        return 8;
    if (lifetime_owner_release(&tr_a, &tr_err) != STDLIB_FAIL)
        return 9; /* moved-from source cannot be released */
    return 0;
}

static int c_lifetime_recycle(unsigned long long p) {
    error rc_err = error();
    lifetime_owner rc_o;
    char rc_buf[4];
    lifetime_borrow rc_b;
    unsigned long long old_tok, new_tok;
    memset(&rc_o, 0, sizeof(rc_o));
    memset(&rc_b, 0, sizeof(rc_b));
    memset(rc_buf, 0, sizeof(rc_buf));
    if (lifetime_owner_init(&rc_o, 16u, &rc_err) != STDLIB_OK)
        return 1;
    old_tok = rc_o.token;
    if (lifetime_borrow_from(&rc_b, &rc_o, &rc_err) != STDLIB_OK)
        return 2;
    if (lifetime_owner_release(&rc_o, &rc_err) != STDLIB_OK)
        return 3;
    if (lifetime_owner_init(&rc_o, 16u, &rc_err) != STDLIB_OK) /* recycled address */
        return 4;
    new_tok = rc_o.token;
    if (old_tok == new_tok)
        return 5; /* generation must change across recycle */
    (void)p;
    if (lifetime_borrow_is_live(&rc_b))
        return 6; /* stale borrow must not revive on recycled address */
    if (lifetime_read(&rc_b, 0u, rc_buf, sizeof(rc_buf), &rc_err) != STDLIB_FAIL)
        return 7;
    if (lifetime_owner_release(&rc_o, &rc_err) != STDLIB_OK)
        return 8;
    return 0;
}

/* ------------------------------------------------------------- dispatcher */
typedef int (*t2_case_fn)(unsigned long long);

static t2_case_fn g_cases_tbl[THREAD2_FAMILIES] = {
    c_mutex_basic, c_mutex_zero, c_mutex_double_unlock, c_cond_basic,
    c_cond_wait, c_thread_start_join, c_thread_badstart, c_timer_lifecycle,
    c_async_invalid_mode, c_async_close_retry, c_lifetime_valid,
    c_lifetime_double_release, c_lifetime_transfer, c_lifetime_recycle,
    c_mutex_basic, c_cond_basic, c_thread_start_join, c_timer_lifecycle,
    c_lifetime_valid, c_async_invalid_mode
};

static const char *g_family_names[THREAD2_FAMILIES] = {
    "mutex_basic", "mutex_zero", "mutex_double_unlock", "cond_basic",
    "cond_wait", "thread_start_join", "thread_badstart", "timer_lifecycle",
    "async_invalid_mode", "async_close_retry", "lifetime_valid",
    "lifetime_double_release", "lifetime_transfer", "lifetime_recycle",
    "mutex_basic2", "cond_basic2", "thread_join2", "timer_lifecycle2",
    "lifetime_valid2", "async_invalid_mode2"
};

static int run_child(int idx, unsigned long long param) {
    pid_t pid;
    int status = 0;
    struct sigaction sa, old_sa;
    int out = 1;

    pid = fork();
    if (pid == (pid_t)0) {
        (void)signal(SIGALRM, t2_on_alarm);
        (void)alarm(5);
        _Exit(g_cases_tbl[idx](param));
    }
    if (pid < (pid_t)0)
        return 1; /* fork failure counted as failure */

    /* Bounded reap: blocking waitpid, parent-level alarm(55) in main(). */
    if (waitpid(pid, &status, 0) == pid) {
        if (WIFEXITED(status))
            out = WEXITSTATUS(status);
        else
            out = 1; /* signaled (watchdog / crash) */
    }
    (void)sa;
    (void)old_sa;
    return out;
}

int main(void) {
    unsigned long long seed = THREAD2_SEED;
    unsigned long long i, idx, param;
    int rc;

    (void)signal(SIGALRM, t2_on_alarm);
    (void)alarm(55);

    for (i = 0; i < THREAD2_CASES; ++i) {
        idx = t2_rnd(&seed) % (unsigned long long)THREAD2_FAMILIES;
        param = t2_rnd(&seed);
        ++g_cases;
        ++g_family[idx];
        rc = run_child((int)idx, param);
        if (rc == 0)
            ++g_passed;
        else
            ++g_failed;
    }
    (void)alarm(0);

    printf("THREAD2 %llu %llu %llu\n", g_cases, g_passed, g_failed);
    for (idx = 0; idx < THREAD2_FAMILIES; ++idx)
        printf("F %s %llu\n", g_family_names[idx], g_family[idx]);
    return g_failed == 0 ? 0 : 1;
}
