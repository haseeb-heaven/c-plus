/* Independent deep contract tests for stdlib/thread.hp -- Task 5 uncovered cases.
 *
 * This file deliberately contains ONLY cases not already exercised by
 * deep_thread_guard.cp and deep_thread_nested.cp.  Those cover guard unlock /
 * drop / holds, repeated unlock, nested guards (two mutexes and re-acquire),
 * zero-init and NULL guards, the ERRORCHECK double-unlock detector with a forked
 * positive control, the deadlock watchdog with a forked positive control, and
 * cross-TU-style release.  None of the following is duplicated here:
 *
 *   1. explicit mutex create/drop lifecycle plus lock/unlock on a REAL worker
 *      thread (start + join + verify a shared counter);
 *   2. guard ownership transfer via cplus_move -- exactly one unlock (checked
 *      with an ERRORCHECK mutex) and the moved-from guard proven inert by
 *      reading its zeroed raw owner field through a pointer captured BEFORE
 *      the move (using the moved-from name directly is a compile error);
 *   3. a guard held across an early `return` from a function -- the compiler
 *      must emit the drop on the return path (ERRORCHECK catches a double
 *      drop, an ordinary re-lock catches a missing drop);
 *   4. 64 mutex create/lock/unlock/drop cycles with telemetry-failure and
 *      open-fd stability checks (mutexes use libc malloc, so stdlib telemetry
 *      failure counters -- not alloc/free counts -- are the meaningful signal);
 *   5. zero-initialised and NULL-owner mutex/guard operations remain safe and
 *      recoverable (a real mutex still works afterwards).
 *
 * EVERY owning local uses a unique name: RaiiLowering de-duplicates ownership
 * by variable name within a function, so a reused name loses its destructor
 * (see docs/stdlib/DEEP_THREAD_CONTRACT.md, finding F3).
 *
 * SELF-CHECKING: every t5_check() prints "<tag> <1|0>" (1 = pass) and counts a
 * failure.  The last line is "THREAD_TASK5 <checks> <failures>".  Exit code 0
 * means zero failures.  All runs are bounded externally with `timeout 30`
 * because a broken lock could otherwise deadlock the process.
 */
#include "stdlib/thread.hp"
#include <fcntl.h>
#include <stdio.h>

static int t5_checks = 0;
static int t5_failures = 0;

static void t5_check(const char *tag, int cond) {
    t5_checks++;
    if (!cond)
        t5_failures++;
    print(tag, cond ? 1 : 0);
}

/* Test-only fault injection: make a hidden double-unlock observable.  The
 * opaque handle's first field is a pthread_mutex_t.  A second unlock on an
 * ERRORCHECK mutex returns EPERM and cplus_mutex_unlock takes the fatal path
 * (CPLUS_EXIT_RESOURCE == 107). */
static void t5_to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutex_destroy(pm);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

/* Shared worker context.  The worker copies the mutex pointer into a local
 * before calling lock()/unlock(): C+ rewrites chained `c->m->lock()` calls
 * incorrectly, but a single-arrow call on a local pointer lowers correctly. */
typedef struct {
    mutex *mtx;
    int counter;
} t5_worker_ctx;

static void *t5_worker_body(void *arg) {
    t5_worker_ctx *ctx = (t5_worker_ctx *)arg;
    mutex *local_mutex = ctx->mtx;
    local_mutex->lock();
    ctx->counter += 1;
    local_mutex->unlock();
    return NULL;
}

/* Function whose guard must be dropped on the early-return path. */
static int t5_early_return_fn(mutex *m) {
    guard t5_early_guard = guard(m);
    if (m != NULL)
        return 71;
    return 0;
}

/* Raw view of a guard's single field (mutex *owner), for moved-from checks. */
struct t5_guard_raw_view {
    mutex *owner;
};

static int t5_raw_owner_nonnull(void *p) {
    struct t5_guard_raw_view *view = (struct t5_guard_raw_view *)p;
    return view->owner != NULL;
}

/* Count currently open file descriptors (bounded, deterministic).  Mutexes do
 * not open fds, but this is a useful no-leak sanity signal across 64 cycles. */
static int t5_count_fds(void) {
    int count = 0;
    int fd;
    for (fd = 0; fd < 256; fd++) {
        if (fcntl(fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

int main(void) {
    error t5_err = error();

    /* ---- 1. Real worker thread: create / lock / unlock / join ------------ */
    {
        mutex t5_worker_mutex = mutex();
        t5_check("t5_worker_valid", t5_worker_mutex.is_valid());
        t5_worker_ctx t5_shared;
        t5_shared.mtx = &t5_worker_mutex;
        t5_shared.counter = 0;
        thread t5_worker_thread = thread();
        int t5_start_rc = t5_worker_thread.start(t5_worker_body, &t5_shared,
                                                 &t5_err);
        t5_check("t5_worker_start",
                 t5_start_rc == STDLIB_OK && t5_err.ok() &&
                     t5_worker_thread.is_joinable());
        void *t5_worker_result = NULL;
        int t5_join_rc = t5_worker_thread.join(&t5_worker_result, &t5_err);
        t5_check("t5_worker_join",
                 t5_join_rc == STDLIB_OK && t5_err.ok() &&
                     t5_worker_result == NULL &&
                     !t5_worker_thread.is_joinable());
        t5_check("t5_worker_counter", t5_shared.counter == 1);
        /* Join again must be a clean failure, not a crash. */
        int t5_rejoin_rc = t5_worker_thread.join(NULL, &t5_err);
        t5_check("t5_worker_rejoin_fails",
                 t5_rejoin_rc == STDLIB_FAIL &&
                     t5_err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                     t5_err.code() == CPLUS_EXIT_RESOURCE);
        t5_worker_thread.drop();
        t5_worker_mutex.drop();
    }

    /* ---- 2. guard ownership transfer via cplus_move --------------------- */
    {
        mutex t5_move_mutex = mutex();
        t5_to_errorcheck(&t5_move_mutex);
        void *t5_moved_from_raw = NULL;
        {
            guard t5_move_src = guard(&t5_move_mutex);
            t5_check("t5_move_src_holds", t5_move_src.holds() != 0);
            /* Capture the address BEFORE the move; using t5_move_src itself
             * afterwards would be a compile-time use-after-move error. */
            t5_moved_from_raw = (void *)&t5_move_src;
            guard t5_move_dst = cplus_move(t5_move_src);
            t5_check("t5_move_dst_holds", t5_move_dst.holds() != 0);
            t5_check("t5_move_src_inert",
                     !t5_raw_owner_nonnull(t5_moved_from_raw));
        } /* both drops run; src's drop is a no-op, dst's unlocks exactly once;
           * an ERRORCHECK double-unlock here would be fatal (exit 107). */
        t5_move_mutex.lock();
        t5_move_mutex.unlock();
        t5_check("t5_move_relock_after", t5_move_mutex.is_valid() != 0);
        t5_move_mutex.drop();
    }

    /* ---- 3. guard held across an early return --------------------------- */
    {
        mutex t5_early_mutex = mutex();
        t5_to_errorcheck(&t5_early_mutex);
        int t5_early_rc = t5_early_return_fn(&t5_early_mutex);
        t5_check("t5_early_return_value", t5_early_rc == 71);
        /* Succeeds only if the compiler dropped the guard on the return path.
         * ERRORCHECK additionally traps a (non-reachable) second drop. */
        t5_early_mutex.lock();
        t5_early_mutex.unlock();
        t5_check("t5_early_relock", t5_early_mutex.is_valid() != 0);
        t5_early_mutex.drop();
    }

    /* ---- 4. 64 create / lock / unlock / drop cycles --------------------- */
    {
        stdlib_telemetry_stats t5_tm_before;
        stdlib_telemetry_stats t5_tm_after;
        int t5_i;
        int t5_fd_before = t5_count_fds();
        stdlib_telemetry_set_enabled(1);
        stdlib_telemetry_reset();
        stdlib_telemetry_snapshot(&t5_tm_before);
        for (t5_i = 0; t5_i < 64; t5_i++) {
            mutex t5_cycle_mutex = mutex();
            if (!t5_cycle_mutex.is_valid())
                break;
            t5_cycle_mutex.lock();
            t5_cycle_mutex.unlock();
            t5_cycle_mutex.drop(); /* explicit; implicit drop is then a no-op */
        }
        stdlib_telemetry_snapshot(&t5_tm_after);
        t5_check("t5_cycles_complete", t5_i == 64);
        t5_check("t5_cycles_telemetry_clean",
                 t5_tm_after.invalid_ops == t5_tm_before.invalid_ops &&
                     t5_tm_after.lifecycle_failures ==
                         t5_tm_before.lifecycle_failures &&
                     t5_tm_after.alloc_failures == t5_tm_before.alloc_failures);
        t5_check("t5_cycles_fd_stable", t5_count_fds() == t5_fd_before);
    }

    /* ---- 5. null / zero-init mutex and guard remain recoverable --------- */
    {
        mutex t5_zero_mutex; /* zero-initialised: handle == NULL */
        t5_check("t5_zero_invalid", !t5_zero_mutex.is_valid());
        t5_zero_mutex.lock();
        t5_zero_mutex.unlock();
        t5_check("t5_zero_native_null", t5_zero_mutex.native() == NULL);
        t5_zero_mutex.drop();
        t5_zero_mutex.drop(); /* double drop is inert */
        t5_check("t5_zero_after_drop", !t5_zero_mutex.is_valid());
        {
            guard t5_zero_guard = guard(&t5_zero_mutex);
            /* owner is non-NULL even though the mutex handle is NULL: the
             * documented edge case; unlock()/drop() stay safe no-ops. */
            t5_check("t5_zero_guard_holds", t5_zero_guard.holds() != 0);
            t5_zero_guard.unlock();
            t5_check("t5_zero_guard_released", !t5_zero_guard.holds());
            t5_zero_guard.unlock();
            t5_zero_guard.drop();
        }
        {
            guard t5_null_guard = guard(NULL);
            t5_check("t5_null_guard_holds", !t5_null_guard.holds());
            t5_null_guard.unlock();
            t5_null_guard.drop();
            t5_check("t5_null_guard_safe", !t5_null_guard.holds());
        }
        /* A real mutex still works after all the no-op operations. */
        {
            mutex t5_recover_mutex = mutex();
            t5_recover_mutex.lock();
            t5_recover_mutex.unlock();
            t5_check("t5_null_recover", t5_recover_mutex.is_valid() != 0);
            t5_recover_mutex.drop();
        }
    }

    print("THREAD_TASK5", t5_checks, t5_failures);
    return t5_failures == 0 ? 0 : 1;
}
