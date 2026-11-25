/* Task 3 Phase 5 - threading safety regressions: lifecycle portion.
 *
 * Self-checking.  Every check prints "<tag> <1|0>" (1 = pass); the final line
 * is "THREAD_SAFETY <checks> <failures>".  Exit 0 means zero failures.
 *
 * Coverage: mutex create/drop, invalid unlock, repeated unlock, nested guards,
 * cleanup after partially initialised objects, deterministic thread::start
 * error paths, repeated create/drop cycles and fd/telemetry balance.
 *
 * Fault injection (test-only): the opaque mutex handle's first field is a
 * pthread_mutex_t.  Re-initialising it as PTHREAD_MUTEX_ERRORCHECK makes an
 * unlock of an unlocked mutex return EPERM, which the runtime turns into a
 * deterministic fatal exit (CPLUS_EXIT_RESOURCE == 107).  Both fatal probes
 * run in forked children with stderr redirected to /dev/null, so the parent
 * keeps clean stderr and exits 0.
 *
 * C+ constraints honoured: unique RAII local names, no chained temporaries,
 * no variable modulo/division, no pointer-parameter subscripts.
 */
#include "stdlib/thread.hp"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int lc_checks = 0;
static int lc_failures = 0;

static void lc_check(const char *tag, int cond) {
    lc_checks++;
    if (!cond)
        lc_failures++;
    print(tag, cond ? 1 : 0);
}

/* Test-only: make a hidden double-unlock / invalid unlock observable. */
static void lc_to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutex_destroy(pm);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

/* Bounded, deterministic open-fd count (no /proc dependency). */
static int lc_count_fds(void) {
    int count = 0;
    int fd;
    for (fd = 0; fd < 256; fd++) {
        if (fcntl(fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

static void *lc_noop_worker(void *arg) {
    (void)arg;
    return NULL;
}

int main(void) {
    error lc_err = error();
    int fd_start;
    int fd_end;
    stdlib_telemetry_stats tm_before;
    stdlib_telemetry_stats tm_after;

    /* ---- 1. mutex create / lock / unlock / drop ------------------------- */
    {
        mutex lc_m = mutex();
        lc_check("lc_mutex_created", lc_m.is_valid() != 0);
        lc_m.lock();
        lc_m.unlock();
        lc_check("lc_mutex_lock_unlock", lc_m.is_valid() != 0);
        lc_m.drop();
        lc_check("lc_mutex_dropped", !lc_m.is_valid());
        lc_m.drop();
        lc_m.lock();
        lc_m.unlock();
        lc_check("lc_mutex_double_drop_safe", !lc_m.is_valid());
    }

    /* ---- 2. guard unlock / repeated unlock / drop ----------------------- */
    {
        mutex lc_gm = mutex();
        lc_to_errorcheck(&lc_gm);
        {
            guard lc_g = guard(&lc_gm);
            lc_check("lc_guard_holds", lc_g.holds() != 0);
            lc_g.unlock();
            lc_check("lc_guard_unlocked", !lc_g.holds());
            lc_g.unlock();
            lc_g.drop();
            lc_check("lc_guard_repeated_unlock_safe", !lc_g.holds());
        }
        lc_gm.lock();
        lc_gm.unlock();
        lc_check("lc_guard_relock_after", lc_gm.is_valid() != 0);
        lc_gm.drop();
    }

    /* ---- 3. zero-init / NULL guards are safe no-ops --------------------- */
    {
        guard lc_zero_g;
        guard lc_null_g = guard(NULL);
        lc_check("lc_guard_zero_inert", !lc_zero_g.holds() && !lc_null_g.holds());
        lc_zero_g.unlock();
        lc_zero_g.drop();
        lc_null_g.unlock();
        lc_null_g.drop();
        lc_check("lc_guard_null_inert", !lc_zero_g.holds() && !lc_null_g.holds());
    }

    /* ---- 4. nested guards on two mutexes, reverse drop ------------------ */
    {
        mutex lc_outer = mutex();
        mutex lc_inner = mutex();
        {
            guard lc_og = guard(&lc_outer);
            lc_check("lc_nested_outer", lc_og.holds() != 0);
            {
                guard lc_ig = guard(&lc_inner);
                lc_check("lc_nested_inner", lc_ig.holds() != 0);
            }
            lc_inner.lock();
            lc_inner.unlock();
            lc_check("lc_nested_inner_released", lc_inner.is_valid() != 0);
        }
        lc_outer.lock();
        lc_outer.unlock();
        lc_check("lc_nested_outer_released", lc_outer.is_valid() != 0);
        lc_inner.drop();
        lc_outer.drop();
    }

    /* ---- 5. invalid unlock: never-locked ERRORCHECK mutex --------------- */
    {
        mutex lc_bad = mutex();
        lc_to_errorcheck(&lc_bad);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            freopen("/dev/null", "w", stderr);
            mutex *child_m = &lc_bad;
            child_m->unlock();
            _Exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        lc_check("lc_invalid_unlock_trap",
                 WIFEXITED(status) && WEXITSTATUS(status) == 107);
        lc_bad.drop();
    }

    /* ---- 6. repeated unlock on a raw mutex is a deterministic fatal ----- */
    {
        mutex lc_rep = mutex();
        lc_to_errorcheck(&lc_rep);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            freopen("/dev/null", "w", stderr);
            mutex *child_m = &lc_rep;
            child_m->lock();
            child_m->unlock();
            child_m->unlock();
            _Exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        lc_check("lc_repeated_unlock_trap",
                 WIFEXITED(status) && WEXITSTATUS(status) == 107);
        lc_rep.drop();
    }

    /* ---- 7. cleanup after partial init: condvar alloc failure ----------- */
    {
        stdlib_telemetry_set_enabled(1);
        stdlib_telemetry_reset();
        stdlib_alloc_fail_at(1);
        condvar lc_failed_cv = condvar();
        stdlib_alloc_fail_reset();
        lc_check("lc_partial_cv_invalid", !lc_failed_cv.is_valid());
        lc_check("lc_partial_cv_signal", lc_failed_cv.signal() == -1);
        lc_check("lc_partial_cv_broadcast", lc_failed_cv.broadcast() == -1);
        lc_failed_cv.drop();
    }

    /* ---- 8. deterministic thread::start error paths --------------------- */
    {
        thread lc_t = thread();
        lc_check("lc_thread_zero", !lc_t.is_joinable());
        int rc = lc_t.start(NULL, NULL, &lc_err);
        lc_check("lc_start_null_fn", rc == STDLIB_FAIL && lc_err.failed_now() &&
                                        !lc_t.is_joinable());
        rc = lc_t.start(lc_noop_worker, NULL, &lc_err);
        lc_check("lc_start_after_failure",
                 rc == STDLIB_OK && lc_t.is_joinable() && lc_err.ok());
        int rc2 = lc_t.start(lc_noop_worker, NULL, &lc_err);
        lc_check("lc_double_start_fails",
                 rc2 == STDLIB_FAIL && lc_err.failed_now() &&
                     lc_err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                     lc_err.code() == CPLUS_EXIT_RESOURCE);
        lc_t.drop();
        lc_check("lc_thread_drop_joins", !lc_t.is_joinable());
    }

    /* ---- 9. pthread_create failure is not deterministically injectable -- */
    /* The two deterministic thread::start error paths above are covered;
     * an actual pthread_create resource failure cannot be forced portably
     * in-process, so this single case is a declared SKIP, never a pass. */
    print("SKIP_REASON", "thread_create_failure",
          "pthread_create resource failure is not deterministically injectable in-process");

    /* ---- 10. repeated create/drop cycles + fd/telemetry balance --------- */
    {
        int i;
        fd_start = lc_count_fds();
        stdlib_telemetry_set_enabled(1);
        stdlib_telemetry_reset();
        stdlib_telemetry_snapshot(&tm_before);
        for (i = 0; i < 64; i++) {
            mutex cm = mutex();
            cm.lock();
            cm.unlock();
            cm.drop();
            condvar cc = condvar();
            cc.signal();
            cc.drop();
        }
        stdlib_telemetry_snapshot(&tm_after);
        fd_end = lc_count_fds();
        lc_check("lc_cycles_complete", i == 64);
        lc_check("lc_cycles_telemetry_clean",
                 tm_after.invalid_ops == tm_before.invalid_ops &&
                     tm_after.lifecycle_failures == tm_before.lifecycle_failures &&
                     tm_after.alloc_failures == tm_before.alloc_failures);
        lc_check("lc_cycles_alloc_free_balanced",
                 tm_after.alloc_count - tm_before.alloc_count ==
                     tm_after.free_count - tm_before.free_count);
        lc_check("lc_cycles_fd_stable", fd_end == fd_start);
    }

    print("THREAD_SAFETY", lc_checks, lc_failures);
    return lc_failures == 0 ? 0 : 1;
}
