/* 100% per-method coverage for stdlib/thread.hp (all 21 public callables).
 *
 * Each public method is invoked with at least one deterministic case whose
 * observable result is verified.  Void methods are verified through their
 * side effect (the native pthread mutex state, an owned flag, or a handle
 * becoming NULL).  Every printed line is "<header> <method> PASS|FAIL".
 *
 * Ownership notes (match the committed deep-thread fixtures):
 *  - Every owning local has a UNIQUE name per scope (the C+ RAII lowering
 *    de-duplicates by variable name, F3 in the thread audit).
 *  - Workers use file-scope state and a NULL arg, so no stack-borrow heuristic
 *    can fire and no worker outlives its join.
 *  - A locked-state probe casts native() to pthread_mutex_t* (first field of
 *    cplus_mutex); trylock distinguishes locked from unlocked without blocking.
 */
#include "stdlib/thread.hp"
#include <pthread.h>

static int th_checks = 0;
static int th_failures = 0;

static void th_verify(const char *header, const char *method, int cond) {
    th_checks++;
    if (cond)
        print(header, method, "PASS");
    else {
        th_failures++;
        print(header, method, "FAIL");
    }
}

/* Returns 1 if the pthread mutex behind `m` is currently locked. */
static int th_mutex_is_locked(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    int rc;
    if (pm == NULL)
        return 0;
    rc = pthread_mutex_trylock(pm);
    if (rc == 0) {
        pthread_mutex_unlock(pm); /* was unlocked: restore and report 0 */
        return 0;
    }
    return 1; /* EBUSY => was locked */
}

/* ---- worker bodies (file scope -> no borrow heuristic, no lifetime risk) -- */
static int th_start_counter = 0;
static void *th_start_worker(void *arg) {
    (void)arg;
    th_start_counter++;
    return NULL;
}

static int th_join_sentinel = 0;
static void *th_join_worker(void *arg) {
    (void)arg;
    return &th_join_sentinel; /* real address: no int-to-pointer provenance */
}

static int th_drop_flag = 0;
static void *th_drop_worker(void *arg) {
    (void)arg;
    th_drop_flag = 1;
    return NULL;
}

/* ---- condvar coordination contexts / workers ----------------------------- */
typedef struct {
    mutex *m;
    condvar *cv;
    int flag;
    int woke;
    int wait_rc;
} th_cv_ctx;

static void *th_signal_waiter(void *arg) {
    th_cv_ctx *c = (th_cv_ctx *)arg;
    mutex *local_m = c->m;
    condvar *local_cv = c->cv;
    c->wait_rc = 0; /* default: predicate already true -> no wait needed */
    local_m->lock();
    while (c->flag == 0) {
        c->wait_rc = local_cv->wait(local_m);
        if (c->wait_rc != 0)
            break;
    }
    c->woke++;
    local_m->unlock();
    return NULL;
}

static void *th_signal_notifier(void *arg) {
    th_cv_ctx *c = (th_cv_ctx *)arg;
    mutex *local_m = c->m;
    condvar *local_cv = c->cv;
    local_m->lock();
    c->flag = 1;
    local_cv->signal();
    local_m->unlock();
    return NULL;
}

int main(void) {
    error th_err = error();

    /* ================= mutex (6) ================= */
    {
        mutex th_ctor_mutex = mutex();
        th_verify("thread.hp", "mutex.ctor",
                  th_ctor_mutex.is_valid() != 0 &&
                      th_ctor_mutex.native() != NULL);
        th_ctor_mutex.drop();
    }
    {
        mutex th_valid_mutex = mutex();
        mutex th_valid_zero; /* zero-initialised: handle == NULL */
        th_verify("thread.hp", "mutex.is_valid",
                  th_valid_mutex.is_valid() != 0 &&
                      th_valid_zero.is_valid() == 0);
        th_valid_mutex.drop();
    }
    {
        mutex th_lock_mutex = mutex();
        th_lock_mutex.lock();
        th_verify("thread.hp", "mutex.lock", th_mutex_is_locked(&th_lock_mutex));
        th_lock_mutex.unlock();
    }
    {
        mutex th_unlock_mutex = mutex();
        th_unlock_mutex.lock();
        th_unlock_mutex.unlock();
        th_verify("thread.hp", "mutex.unlock",
                  th_mutex_is_locked(&th_unlock_mutex) == 0);
    }
    {
        mutex th_native_mutex = mutex();
        mutex th_native_zero;
        th_verify("thread.hp", "mutex.native",
                  th_native_mutex.native() != NULL &&
                      th_native_zero.native() == NULL);
        th_native_mutex.drop();
    }
    {
        mutex th_drop_mutex = mutex();
        void *th_before = th_drop_mutex.native();
        th_drop_mutex.drop();
        th_verify("thread.hp", "mutex.drop",
                  th_before != NULL && th_drop_mutex.native() == NULL &&
                      th_drop_mutex.is_valid() == 0);
    }

    /* ================= guard (4) ================= */
    {
        mutex th_gctor_mutex = mutex();
        guard th_gctor = guard(&th_gctor_mutex);
        th_verify("thread.hp", "guard.ctor",
                  th_gctor.holds() != 0 &&
                      th_mutex_is_locked(&th_gctor_mutex) == 1);
    }
    {
        mutex th_holds_mutex = mutex();
        guard th_holds_yes = guard(&th_holds_mutex);
        guard th_holds_no = guard(NULL);
        th_verify("thread.hp", "guard.holds",
                  th_holds_yes.holds() != 0 && th_holds_no.holds() == 0);
    }
    {
        mutex th_guard_unlock_mutex = mutex();
        guard th_guard_unlock = guard(&th_guard_unlock_mutex);
        th_guard_unlock.unlock();
        th_verify("thread.hp", "guard.unlock",
                  th_guard_unlock.holds() == 0 &&
                      th_mutex_is_locked(&th_guard_unlock_mutex) == 0);
    }
    {
        mutex th_guard_drop_mutex = mutex();
        guard th_guard_drop = guard(&th_guard_drop_mutex);
        th_guard_drop.drop();
        th_verify("thread.hp", "guard.drop",
                  th_guard_drop.holds() == 0 &&
                      th_mutex_is_locked(&th_guard_drop_mutex) == 0);
    }

    /* ================= condvar (6) ================= */
    {
        condvar th_cv_ctor = condvar();
        th_verify("thread.hp", "condvar.ctor", th_cv_ctor.is_valid() != 0);
    }
    {
        condvar th_cv_valid = condvar();
        condvar th_cv_zero; /* zero-initialised: cv == NULL */
        th_verify("thread.hp", "condvar.is_valid",
                  th_cv_valid.is_valid() != 0 && th_cv_zero.is_valid() == 0);
    }
    {
        /* Main waits on a predicate set by a notifier under the same mutex. */
        mutex th_wait_mutex = mutex();
        condvar th_wait_cv = condvar();
        th_cv_ctx th_wait_ctx;
        int th_wait_rc = -99;
        int th_wait_i;
        th_wait_ctx.m = &th_wait_mutex;
        th_wait_ctx.cv = &th_wait_cv;
        th_wait_ctx.flag = 0;
        th_wait_ctx.woke = 0;
        th_wait_ctx.wait_rc = -99;
        thread th_wait_notifier = thread();
        th_wait_mutex.lock();
        th_wait_notifier.start(th_signal_notifier, &th_wait_ctx, &th_err);
        for (th_wait_i = 0; th_wait_i < 1000 && th_wait_ctx.flag == 0;
             th_wait_i++) {
            th_wait_rc = th_wait_cv.wait(&th_wait_mutex);
            if (th_wait_rc != 0)
                break;
        }
        th_wait_mutex.unlock();
        th_wait_notifier.join(NULL, &th_err);
        /* Invalid-input branches of wait(). */
        condvar th_wait_zero;
        int th_wait_null_rc = th_wait_cv.wait(NULL);
        int th_wait_zero_rc = th_wait_zero.wait(&th_wait_mutex);
        th_verify("thread.hp", "condvar.wait",
                  th_wait_rc == 0 && th_wait_ctx.flag == 1 &&
                      th_wait_null_rc == -1 && th_wait_zero_rc == -1);
    }
    {
        /* A real waiter is woken by signal(); return value is checked too. */
        mutex th_sig_mutex = mutex();
        condvar th_sig_cv = condvar();
        th_cv_ctx th_sig_ctx;
        int th_sig_direct;
        condvar th_sig_zero;
        int th_sig_zero_rc;
        th_sig_ctx.m = &th_sig_mutex;
        th_sig_ctx.cv = &th_sig_cv;
        th_sig_ctx.flag = 0;
        th_sig_ctx.woke = 0;
        th_sig_ctx.wait_rc = -99;
        thread th_sig_waiter = thread();
        th_sig_waiter.start(th_signal_waiter, &th_sig_ctx, &th_err);
        /* Publish the predicate under the mutex first: a waiter that has not
         * blocked yet sees flag==1 and never waits, so an early signal cannot
         * be lost.  A blocked waiter is released by the direct signal below. */
        th_sig_mutex.lock();
        th_sig_ctx.flag = 1;
        th_sig_mutex.unlock();
        th_sig_direct = th_sig_cv.signal();
        th_sig_waiter.join(NULL, &th_err);
        th_sig_zero_rc = th_sig_zero.signal();
        th_verify("thread.hp", "condvar.signal",
                  th_sig_direct == 0 && th_sig_ctx.wait_rc == 0 &&
                      th_sig_ctx.woke == 1 && th_sig_zero_rc == -1);
    }
    {
        /* Two waiters, both released by one broadcast(). */
        mutex th_bc_mutex = mutex();
        condvar th_bc_cv = condvar();
        th_cv_ctx th_bc_ctx;
        int th_bc_direct;
        condvar th_bc_zero;
        int th_bc_zero_rc;
        th_bc_ctx.m = &th_bc_mutex;
        th_bc_ctx.cv = &th_bc_cv;
        th_bc_ctx.flag = 0;
        th_bc_ctx.woke = 0;
        th_bc_ctx.wait_rc = -99;
        thread th_bc_waiter_a = thread();
        thread th_bc_waiter_b = thread();
        th_bc_waiter_a.start(th_signal_waiter, &th_bc_ctx, &th_err);
        th_bc_waiter_b.start(th_signal_waiter, &th_bc_ctx, &th_err);
        th_bc_mutex.lock();
        th_bc_ctx.flag = 1;
        th_bc_direct = th_bc_cv.broadcast();
        th_bc_mutex.unlock();
        th_bc_waiter_a.join(NULL, &th_err);
        th_bc_waiter_b.join(NULL, &th_err);
        th_bc_zero_rc = th_bc_zero.broadcast();
        th_verify("thread.hp", "condvar.broadcast",
                  th_bc_direct == 0 && th_bc_ctx.wait_rc == 0 &&
                      th_bc_ctx.woke == 2 && th_bc_zero_rc == -1);
    }
    {
        condvar th_cv_drop = condvar();
        th_cv_drop.drop();
        th_verify("thread.hp", "condvar.drop", th_cv_drop.is_valid() == 0);
    }

    /* ================= thread (5) ================= */
    {
        thread th_tctor = thread();
        th_verify("thread.hp", "thread.ctor", th_tctor.is_joinable() == 0);
    }
    {
        thread th_joinable = thread();
        int th_before = th_joinable.is_joinable();
        int th_start_rc = th_joinable.start(th_start_worker, NULL, &th_err);
        int th_during = th_joinable.is_joinable();
        th_joinable.join(NULL, &th_err);
        int th_after = th_joinable.is_joinable();
        th_verify("thread.hp", "thread.is_joinable",
                  th_before == 0 && th_during != 0 && th_after == 0);
    }
    {
        thread th_start_t = thread();
        th_start_counter = 0;
        int th_start_rc = th_start_t.start(th_start_worker, NULL, &th_err);
        int th_start_ok = th_start_rc == STDLIB_OK && th_err.ok() != 0 &&
                          th_start_t.is_joinable() != 0;
        int th_dbl_rc = th_start_t.start(th_start_worker, NULL, &th_err);
        thread th_null_t = thread();
        int th_null_rc = th_null_t.start(NULL, NULL, &th_err);
        int th_start_fail_seen = th_err.failed_now() != 0; /* before join resets */
        th_start_t.join(NULL, &th_err);
        th_verify("thread.hp", "thread.start",
                  th_start_ok && th_start_counter == 1 &&
                      th_dbl_rc == STDLIB_FAIL && th_null_rc == STDLIB_FAIL &&
                      th_start_fail_seen != 0);
    }
    {
        thread th_join_t = thread();
        void *th_out = NULL;
        int th_join_rc;
        int th_join_bad;
        th_join_t.start(th_join_worker, NULL, &th_err);
        th_join_rc = th_join_t.join(&th_out, &th_err);
        int th_join_err_ok = th_err.ok() != 0; /* capture before failed rejoin */
        th_join_bad = th_join_t.join(NULL, &th_err);
        th_verify("thread.hp", "thread.join",
                  th_join_rc == STDLIB_OK && th_join_err_ok &&
                      th_out == (void *)&th_join_sentinel &&
                      th_join_t.is_joinable() == 0 &&
                      th_join_bad == STDLIB_FAIL);
    }
    {
        thread th_drop_t = thread();
        th_drop_flag = 0;
        th_drop_t.start(th_drop_worker, NULL, &th_err);
        th_drop_t.drop(); /* joins while still joinable */
        th_verify("thread.hp", "thread.drop",
                  th_drop_flag == 1 && th_drop_t.is_joinable() == 0);
    }

    print("METHOD_TEST", "thread", th_checks, th_failures);
    return th_failures == 0 ? 0 : 1;
}
