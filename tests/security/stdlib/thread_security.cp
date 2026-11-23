/* Task 6 Phase 5 - Thread and synchronization security (deterministic).
 *
 * Self-checking, bounded, watchdog-guarded.  Every check prints
 * "<tag> <1|0>" (1 = pass); SKIPs print "SKIP_REASON <topic> <why>"; the final
 * line is "THREAD_SECURITY <checks> <failures>".  Exit 0 means zero failures.
 *
 * Coverage (all deterministic; no timing races, no real deadlock in the parent):
 *   1. guard double-unlock / unlock+drop never double-unlocks (ERRORCHECK).
 *   2. raw double unlock and invalid unlock trap at 107 in forked watchdog
 *      children (positive controls proving the detector fires).
 *   3. use-after-destroy: metadata refusal + zero/NULL handle no-ops only;
 *      no stale handle is ever dereferenced.
 *   4. invalid thread lifecycle: join-before-start, null start, double start,
 *      double join, drop-after-join.
 *   5. partial-init cleanup: condvar allocation-failure injection and a
 *      thread whose start() fails leave inert handles.
 *   6. error paths while a guard is held: failed signal/wait/join leave the
 *      guard intact and the ERRORCHECK mutex releasable (no lost release).
 *   7. bounded contention: exact counters/checksums under a shared mutex.
 *   8. lock ordering: inverted order runs ONLY in a forked watchdog child and
 *      is killed; an ordered control child exits 0 (differential evidence).
 *   9. declared SKIPs for unsupported / non-injectable operations.
 *
 * C+ strict-safe patterns honoured: every owning local has a unique name in
 * its function, no chained temporaries, no variable modulo/division (bit
 * masks only), and no pointer-parameter subscripts.
 */
#include "stdlib/thread.hp"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TS_WORKERS 4
#define TS_ITERS 200

static int ts_checks = 0;
static int ts_failures = 0;

static void ts_check(const char *tag, int cond) {
    ts_checks++;
    if (!cond)
        ts_failures++;
    print(tag, cond ? 1 : 0);
}

/* ---- bounded process + alarm watchdog ------------------------------------ */
struct ts_watchdog_result {
    int reaped;
    int timed_out;
    int exited;
    int exit_code;
    int signaled;
    int term_signal;
    int raw_status;
};

static volatile sig_atomic_t ts_watchdog_alarm;

static void ts_watchdog_on_alarm(int signo) {
    (void)signo;
    ts_watchdog_alarm = 1;
}

static struct ts_watchdog_result ts_run_watchdog(void (*child_fn)(void),
                                                 unsigned int hard_secs) {
    struct ts_watchdog_result r;
    struct sigaction old_action;
    struct sigaction backstop;
    pid_t pid;
    pid_t wrc;
    int status;
    int reaped;
    int timed_out;

    status = 0;
    r.reaped = 0;
    r.timed_out = 0;
    r.exited = 0;
    r.exit_code = -1;
    r.signaled = 0;
    r.term_signal = 0;
    r.raw_status = 0;
    if (child_fn == (void (*)(void))0 || hard_secs == 0)
        return r;

    pid = fork();
    if (pid == (pid_t)0) {
        child_fn();
        _Exit(0);
    }
    if (pid < (pid_t)0)
        return r;

    memset(&backstop, 0, sizeof(backstop));
    backstop.sa_handler = ts_watchdog_on_alarm;
    sigemptyset(&backstop.sa_mask);
    backstop.sa_flags = 0; /* deliberately no SA_RESTART: interrupt waitpid */
    if (sigaction(SIGALRM, &backstop, &old_action) != 0) {
        (void)!kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < (pid_t)0) {
            /* bounded reap */
        }
        r.reaped = 1;
        r.timed_out = 1;
        r.raw_status = status;
        return r;
    }

    ts_watchdog_alarm = 0;
    (void)!alarm(hard_secs);
    status = 0;
    reaped = 0;
    timed_out = 0;
    for (;;) {
        wrc = waitpid(pid, &status, 0);
        if (wrc == pid) {
            reaped = 1;
            break;
        }
        if (wrc < (pid_t)0 && errno == EINTR) {
            if (ts_watchdog_alarm) {
                timed_out = 1;
                break;
            }
            continue;
        }
        if (wrc < (pid_t)0)
            break;
    }
    (void)!alarm(0);
    (void)!sigaction(SIGALRM, &old_action, (struct sigaction *)0);

    if (!reaped) {
        timed_out = 1;
        (void)!kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < (pid_t)0) {
            /* SIGKILL is uncatchable; this reap is bounded. */
        }
        reaped = 1;
    }

    r.reaped = reaped;
    r.timed_out = timed_out;
    r.raw_status = status;
    if (WIFEXITED(status)) {
        r.exited = 1;
        r.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r.signaled = 1;
        r.term_signal = WTERMSIG(status);
    }
    return r;
}

/* ---- test-only fault injection ------------------------------------------- */
/* The runtime mutex handle's first field is a pthread_mutex_t.  Re-init it as
 * ERRORCHECK so an unlock of an unlocked mutex returns EPERM and the runtime
 * takes the fatal path (CPLUS_EXIT_RESOURCE == 107).  Never used on live data
 * outside a test scope. */
static void ts_to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutexattr_t attr;
    pthread_mutex_destroy(pm);
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

static mutex *ts_fatal_m; /* set by the parent before each fatal-child fork */

static void ts_child_raw_double_unlock(void) {
    (void)!freopen("/dev/null", "w", stderr);
    ts_fatal_m->lock();
    ts_fatal_m->unlock();
    ts_fatal_m->unlock(); /* defect simulation: must trap at 107 */
    _Exit(0);
}

static void ts_child_invalid_unlock(void) {
    (void)!freopen("/dev/null", "w", stderr);
    ts_fatal_m->unlock(); /* never locked: must trap at 107 */
    _Exit(0);
}

/* ---- workers ------------------------------------------------------------- */
static void *ts_noop_worker(void *arg) {
    (void)arg;
    return NULL;
}

struct ts_shared {
    mutex *first;
    mutex *second;
    long counter;
    long checksum;
};
static struct ts_shared g_ts;

/* Self-join probe state (single-threaded writer / single worker reader). */
static thread *ts_self_target;
static volatile int ts_self_go;
static volatile int ts_self_finished;
static int ts_self_join_rc;
static int ts_self_joinable_after;

static void *ts_selfjoin_worker(void *arg) {
    error sj_err = error();
    (void)arg;
    while (__atomic_load_n(&ts_self_go, __ATOMIC_ACQUIRE) == 0)
        sched_yield();
    /* pthread_join(pthread_self()) fails with EDEADLK: a deterministic
     * join() error path that does not depend on resource exhaustion. */
    ts_self_join_rc = ts_self_target->join(NULL, &sj_err);
    ts_self_joinable_after = ts_self_target->is_joinable();
    __atomic_store_n(&ts_self_finished, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void ts_count_step(struct ts_shared *s, int i) {
    guard step_g = guard(s->first);
    s->counter++;
    if ((i & 7) == 0) {
        guard nested_g = guard(s->second);
        s->checksum += (long)i;
    }
}

static void *ts_count_worker(void *arg) {
    struct ts_shared *s = (struct ts_shared *)arg;
    int i;
    for (i = 1; i <= TS_ITERS; i++)
        ts_count_step(s, i);
    return NULL;
}

/* ---- lock-order children (no deadlock in the parent) --------------------- */
static pthread_mutex_t ts_dl_a;
static pthread_mutex_t ts_dl_b;
static volatile int ts_dl_a_taken;
static volatile int ts_dl_b_taken;

static void *ts_ordered_worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&ts_dl_a);
    pthread_mutex_lock(&ts_dl_b);
    pthread_mutex_unlock(&ts_dl_b);
    pthread_mutex_unlock(&ts_dl_a);
    return NULL;
}

static void *ts_inverted_first(void *arg) {
    (void)arg;
    pthread_mutex_lock(&ts_dl_a);
    ts_dl_a_taken = 1;
    while (ts_dl_b_taken == 0)
        sched_yield();
    pthread_mutex_lock(&ts_dl_b); /* waits for the other thread */
    pthread_mutex_unlock(&ts_dl_b);
    pthread_mutex_unlock(&ts_dl_a);
    return NULL;
}

static void *ts_inverted_second(void *arg) {
    (void)arg;
    pthread_mutex_lock(&ts_dl_b);
    ts_dl_b_taken = 1;
    while (ts_dl_a_taken == 0)
        sched_yield();
    pthread_mutex_lock(&ts_dl_a); /* waits for the other thread */
    pthread_mutex_unlock(&ts_dl_a);
    pthread_mutex_unlock(&ts_dl_b);
    return NULL;
}

static void ts_child_lock_ordered(void) {
    thread co_t1 = thread();
    thread co_t2 = thread();
    error co_err = error();
    pthread_mutex_init(&ts_dl_a, NULL);
    pthread_mutex_init(&ts_dl_b, NULL);
    (void)!co_t1.start(ts_ordered_worker, NULL, &co_err);
    (void)!co_t2.start(ts_ordered_worker, NULL, &co_err);
    (void)!co_t1.join(NULL, &co_err);
    (void)!co_t2.join(NULL, &co_err);
    _Exit(0);
}

static void ts_child_lock_inversion(void) {
    thread ci_t1 = thread();
    thread ci_t2 = thread();
    error ci_err = error();
    pthread_mutex_init(&ts_dl_a, NULL);
    pthread_mutex_init(&ts_dl_b, NULL);
    ts_dl_a_taken = 0;
    ts_dl_b_taken = 0;
    (void)!ci_t1.start(ts_inverted_first, NULL, &ci_err);
    (void)!ci_t2.start(ts_inverted_second, NULL, &ci_err);
    /* Both flags become 1, then each thread waits for a mutex the other
     * holds.  The parent watchdog kills this child; the parent itself never
     * blocks on a contended lock. */
    for (;;)
        pause();
}

int main(void) {
    error ts_err = error();
    int i;
    long expected_counter;
    long expected_checksum;

    /* ---- 1. guard unlock + drop must never double-unlock ----------------- */
    {
        mutex ts_s1_m = mutex();
        ts_to_errorcheck(&ts_s1_m);
        {
            guard ts_s1_g1 = guard(&ts_s1_m);
            ts_check("ts_s1_guard_holds", ts_s1_g1.holds() != 0);
            ts_s1_g1.unlock();
            ts_check("ts_s1_guard_unlocked", !ts_s1_g1.holds());
        } /* implicit drop must not unlock a second time */
        ts_s1_m.lock();
        ts_s1_m.unlock();
        ts_check("ts_s1_relock_after_drop", ts_s1_m.is_valid() != 0);
        {
            guard ts_s1_g2 = guard(&ts_s1_m);
            ts_s1_g2.unlock();
            ts_s1_g2.unlock(); /* repeated unlock is a no-op */
            ts_s1_g2.drop();
            ts_check("ts_s1_repeat_unlock_inert", !ts_s1_g2.holds());
        }
        ts_s1_m.lock();
        ts_s1_m.unlock();
        ts_check("ts_s1_relock_after_repeat", ts_s1_m.is_valid() != 0);
        ts_s1_m.drop();
        ts_s1_m.drop(); /* double drop is safe */
        ts_check("ts_s1_double_drop_inert", !ts_s1_m.is_valid());
    }

    /* ---- 2. fatal detectors (forked watchdog children) ------------------- */
    {
        mutex ts_s2_m = mutex();
        struct ts_watchdog_result ts_s2_double;
        struct ts_watchdog_result ts_s2_invalid;
        ts_to_errorcheck(&ts_s2_m);
        ts_fatal_m = &ts_s2_m;
        (void)!fflush(stdout);
        ts_s2_double = ts_run_watchdog(ts_child_raw_double_unlock, 2);
        ts_check("ts_s2_double_unlock_trap",
                 ts_s2_double.exited != 0 && ts_s2_double.exit_code == 107 &&
                     ts_s2_double.timed_out == 0);
        (void)!fflush(stdout);
        ts_s2_invalid = ts_run_watchdog(ts_child_invalid_unlock, 2);
        ts_check("ts_s2_invalid_unlock_trap",
                 ts_s2_invalid.exited != 0 && ts_s2_invalid.exit_code == 107 &&
                     ts_s2_invalid.timed_out == 0);
        ts_s2_m.drop();
    }

    /* ---- 3. use-after-destroy: metadata + no-op handles only ------------- */
    {
        mutex ts_s3_m = mutex();
        mutex ts_s3_z;
        guard ts_s3_gz;
        guard ts_s3_gn = guard(NULL);
        int ts_s3_before;
        int ts_s3_after;

        ts_s3_m.drop();
        ts_check("ts_s3_dropped_refuses", !ts_s3_m.is_valid());
        ts_s3_m.lock(); /* zeroed handle: safe no-op, never dereferenced */
        ts_s3_m.unlock();
        ts_check("ts_s3_dropped_ops_inert", !ts_s3_m.is_valid());

        ts_s3_z.lock(); /* zero-initialised mutex: inert */
        ts_s3_z.unlock();
        ts_check("ts_s3_zero_mutex_inert", !ts_s3_z.is_valid());
        ts_check("ts_s3_zero_guard_inert",
                 !ts_s3_gz.holds() && !ts_s3_gn.holds());
        ts_s3_gz.unlock();
        ts_s3_gz.drop();
        ts_s3_gn.unlock();
        ts_s3_gn.drop();
        ts_s3_before = ts_checks;
        {
            guard ts_s3_gp = guard(&ts_s3_m); /* owner is a dropped mutex */
            ts_s3_gp.drop();                  /* handle is NULL: no deref */
        }
        ts_s3_after = ts_checks;
        ts_check("ts_s3_guard_on_inert_owner_survives",
                 ts_s3_before == ts_s3_after);
    }

    /* ---- 4. invalid thread lifecycle ------------------------------------- */
    {
        thread ts_s4_t = thread();
        error ts_s4_err = error();
        int rc;

        ts_check("ts_s4_zero_thread", !ts_s4_t.is_joinable());
        rc = ts_s4_t.join(NULL, &ts_s4_err);
        ts_check("ts_s4_join_before_start",
                 rc == STDLIB_FAIL && ts_s4_err.failed_now() &&
                     ts_s4_err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                     !ts_s4_t.is_joinable());
        rc = ts_s4_t.start(NULL, NULL, &ts_s4_err);
        ts_check("ts_s4_null_start",
                 rc == STDLIB_FAIL && ts_s4_err.failed_now() &&
                     !ts_s4_t.is_joinable());
        rc = ts_s4_t.start(ts_noop_worker, NULL, &ts_s4_err);
        ts_check("ts_s4_start_after_failure",
                 rc == STDLIB_OK && ts_s4_err.ok() && ts_s4_t.is_joinable());
        rc = ts_s4_t.start(ts_noop_worker, NULL, &ts_s4_err);
        ts_check("ts_s4_double_start",
                 rc == STDLIB_FAIL && ts_s4_err.failed_now() &&
                     ts_s4_err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                     ts_s4_err.code() == CPLUS_EXIT_RESOURCE);
        rc = ts_s4_t.join(NULL, &ts_s4_err);
        ts_check("ts_s4_join_ok", rc == STDLIB_OK && ts_s4_err.ok() &&
                                      !ts_s4_t.is_joinable());
        rc = ts_s4_t.join(NULL, &ts_s4_err);
        ts_check("ts_s4_double_join",
                 rc == STDLIB_FAIL && ts_s4_err.failed_now() &&
                     ts_s4_err.category() == STDLIB_CATEGORY_LIFECYCLE);
        ts_s4_t.drop();
        ts_check("ts_s4_drop_inert", !ts_s4_t.is_joinable());
    }

    /* ---- 4b. join() failure path keeps the resource joinable ------------- */
    {
        thread ts_4b_t = thread();
        error ts_4b_err = error();
        int ts_4b_rc;
        int ts_4b_spin;

        ts_self_go = 0;
        ts_self_finished = 0;
        ts_self_join_rc = STDLIB_OK;
        ts_self_joinable_after = 0;
        ts_4b_rc = ts_4b_t.start(ts_selfjoin_worker, NULL, &ts_4b_err);
        ts_self_target = &ts_4b_t;
        __atomic_store_n(&ts_self_go, 1, __ATOMIC_RELEASE);
        for (ts_4b_spin = 0; ts_4b_spin < 1000000; ts_4b_spin++) {
            if (__atomic_load_n(&ts_self_finished, __ATOMIC_ACQUIRE) != 0)
                break;
            sched_yield();
        }
        ts_check("ts_4b_worker_finished",
                 __atomic_load_n(&ts_self_finished, __ATOMIC_ACQUIRE) != 0);
        ts_check("ts_4b_selfjoin_fails",
                 ts_4b_rc == STDLIB_OK && ts_self_join_rc == STDLIB_FAIL);
        ts_check("ts_4b_joinable_kept_on_failure",
                 ts_self_joinable_after != 0);
        ts_4b_t.drop(); /* reaps the still-running worker via pthread_join */
        ts_check("ts_4b_drop_inert", !ts_4b_t.is_joinable());
    }

    /* ---- 5. partial-init cleanup ----------------------------------------- */
    {
        condvar ts_s5_bad_cv;
        thread ts_s5_bad_t = thread();
        error ts_s5_err = error();
        int rc;

        stdlib_telemetry_set_enabled(1);
        stdlib_telemetry_reset();
        stdlib_alloc_fail_at(1);
        {
            condvar ts_s5_cv = condvar();
            stdlib_alloc_fail_reset();
            ts_check("ts_s5_partial_cv_invalid", !ts_s5_cv.is_valid());
            ts_check("ts_s5_partial_cv_signal", ts_s5_cv.signal() == -1);
            ts_check("ts_s5_partial_cv_broadcast", ts_s5_cv.broadcast() == -1);
            ts_s5_cv.drop();
        }
        ts_s5_bad_cv.drop(); /* zero-init condvar: inert */
        rc = ts_s5_bad_t.start(NULL, NULL, &ts_s5_err);
        ts_s5_bad_t.drop();
        ts_check("ts_s5_partial_thread_clean",
                 rc == STDLIB_FAIL && !ts_s5_bad_t.is_joinable());
    }

    /* ---- 6. error paths while a guard is held ---------------------------- */
    {
        mutex ts_s6_m = mutex();
        condvar ts_s6_bad_cv;
        thread ts_s6_t = thread();
        error ts_s6_err = error();
        int rc_signal;
        int rc_wait;
        int rc_join;

        ts_to_errorcheck(&ts_s6_m);
        {
            guard ts_s6_g = guard(&ts_s6_m);
            rc_signal = ts_s6_bad_cv.signal();
            rc_wait = ts_s6_bad_cv.wait(&ts_s6_m);
            rc_join = ts_s6_t.join(NULL, &ts_s6_err);
            ts_check("ts_s6_errors_under_lock",
                     ts_s6_g.holds() != 0 && rc_signal == -1 && rc_wait == -1 &&
                         rc_join == STDLIB_FAIL && ts_s6_err.failed_now() &&
                         ts_s6_err.category() == STDLIB_CATEGORY_LIFECYCLE);
        } /* guard drop must release exactly once */
        ts_s6_m.lock();
        ts_s6_m.unlock();
        ts_check("ts_s6_lock_reusable_after_errors", ts_s6_m.is_valid() != 0);
        ts_s6_bad_cv.drop();
        ts_s6_m.drop();
    }

    /* ---- 7. bounded contention with exact counters ----------------------- */
    {
        mutex ts_s7_a = mutex();
        mutex ts_s7_b = mutex();
        thread ts_s7_w0 = thread();
        thread ts_s7_w1 = thread();
        thread ts_s7_w2 = thread();
        thread ts_s7_w3 = thread();
        error ts_s7_err = error();
        int ts_s7_started = 0;
        int ts_s7_rc0;
        int ts_s7_rc1;
        int ts_s7_rc2;
        int ts_s7_rc3;

        g_ts.first = &ts_s7_a;
        g_ts.second = &ts_s7_b;
        g_ts.counter = 0;
        g_ts.checksum = 0;
        ts_s7_rc0 = ts_s7_w0.start(ts_count_worker, &g_ts, &ts_s7_err);
        ts_s7_rc1 = ts_s7_w1.start(ts_count_worker, &g_ts, &ts_s7_err);
        ts_s7_rc2 = ts_s7_w2.start(ts_count_worker, &g_ts, &ts_s7_err);
        ts_s7_rc3 = ts_s7_w3.start(ts_count_worker, &g_ts, &ts_s7_err);
        if (ts_s7_rc0 == STDLIB_OK)
            ts_s7_started++;
        if (ts_s7_rc1 == STDLIB_OK)
            ts_s7_started++;
        if (ts_s7_rc2 == STDLIB_OK)
            ts_s7_started++;
        if (ts_s7_rc3 == STDLIB_OK)
            ts_s7_started++;
        if (ts_s7_rc0 == STDLIB_OK)
            (void)!ts_s7_w0.join(NULL, &ts_s7_err);
        if (ts_s7_rc1 == STDLIB_OK)
            (void)!ts_s7_w1.join(NULL, &ts_s7_err);
        if (ts_s7_rc2 == STDLIB_OK)
            (void)!ts_s7_w2.join(NULL, &ts_s7_err);
        if (ts_s7_rc3 == STDLIB_OK)
            (void)!ts_s7_w3.join(NULL, &ts_s7_err);
        expected_counter = (long)TS_WORKERS * (long)TS_ITERS;
        expected_checksum = 0;
        for (i = 1; i <= TS_ITERS; i++) {
            if ((i & 7) == 0)
                expected_checksum += (long)i;
        }
        expected_checksum *= (long)TS_WORKERS;
        ts_check("ts_s7_all_workers_started", ts_s7_started == TS_WORKERS);
        ts_check("ts_s7_counter_exact",
                 ts_s7_started == TS_WORKERS && g_ts.counter == expected_counter);
        ts_check("ts_s7_checksum_exact",
                 ts_s7_started == TS_WORKERS && g_ts.checksum == expected_checksum);
        ts_s7_a.lock();
        ts_s7_a.unlock();
        ts_s7_b.lock();
        ts_s7_b.unlock();
        ts_check("ts_s7_locks_reusable", ts_s7_a.is_valid() && ts_s7_b.is_valid());
        ts_s7_b.drop();
        ts_s7_a.drop();
    }

    /* ---- 8. lock ordering: inverted order only in a forked watchdog ------ */
    {
        struct ts_watchdog_result ts_s8_ok;
        struct ts_watchdog_result ts_s8_inv;
        (void)!fflush(stdout);
        ts_s8_ok = ts_run_watchdog(ts_child_lock_ordered, 2);
        ts_check("ts_s8_ordered_control_exits",
                 ts_s8_ok.exited != 0 && ts_s8_ok.exit_code == 0 &&
                     ts_s8_ok.timed_out == 0);
        (void)!fflush(stdout);
        ts_s8_inv = ts_run_watchdog(ts_child_lock_inversion, 2);
        ts_check("ts_s8_inversion_detected",
                 ts_s8_inv.timed_out != 0 && ts_s8_inv.signaled != 0 &&
                     ts_s8_inv.term_signal == SIGKILL);
    }

    /* ---- 9. declared SKIPs ------------------------------------------------ */
    print("SKIP_REASON", "thread_cancellation",
          "stdlib/thread.hp has no cancel()/cleanup API; raw pthread_cancel bypasses managed RAII lowering");
    print("SKIP_REASON", "pthread_create_failure",
          "pthread_create resource failure is not deterministically injectable in-process");

    print("THREAD_SECURITY", ts_checks, ts_failures);
    return ts_failures == 0 ? 0 : 1;
}
