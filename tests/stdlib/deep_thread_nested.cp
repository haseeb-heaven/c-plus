/* Deep test: nested guards, cross-TU-style release and a watchdog detector.
 *
 * Self-checking with exact expected stdout documented in
 * docs/stdlib/DEEP_THREAD_CONTRACT.md.  Exits 0 only when every check passes.
 *
 * Two fault-injection techniques are used:
 *  - The ERRORCHECK re-initialisation (first pthread_mutex_t field of the
 *    opaque handle) turns any hidden double-unlock into fatal exit 107.
 *  - A forked child with an alarm(2) watchdog proves the lock is actually
 *    released by unlock(): a re-lock that blocks is killed by SIGALRM and the
 *    parent observes the signal.  A second, deliberately self-deadlocking
 *    child proves the detector itself works (positive control).
 */
#include "stdlib/thread.hp"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>

static int failures = 0;

static void check(const char *label, int got, int want) {
    print(label, got);
    if (got != want) {
        failures++;
        print("MISMATCH", label, got, want);
    }
}

static void to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutex_destroy(pm);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

/* Simulated translation unit A: acquire and transfer the guard. */
guard acquire(mutex *m) {
    guard g = guard(m);
    return g;
}

/* Simulated translation unit B: release through an opaque pointer. */
void release_via_ptr(guard *g) {
    g->unlock();
}

int main(void) {
    mutex a = mutex();
    mutex b = mutex();
    check("valid", a.is_valid() && b.is_valid(), 1);
    to_errorcheck(&a);
    to_errorcheck(&b);

    /* 1. Guards in nested scopes on two different mutexes: reverse drop. */
    {
        guard g1 = guard(&a);
        check("nested_outer_holds", g1.holds(), 1);
        {
            guard g2 = guard(&b);
            check("nested_inner_holds", g2.holds(), 1);
        } /* guard_drop(&g2) */
        b.lock();
        b.unlock();
        check("inner_released", b.is_valid(), 1);
    } /* guard_drop(&g1) */
    a.lock();
    a.unlock();
    check("outer_released", a.is_valid(), 1);

    /* 2. Re-acquire the same mutex in an inner scope after outer unlock.
     * Unique names h1/h2 (not g1/g2): RaiiLowering deduplicates ownership by
     * name within a function, so reusing g1/g2 here would omit both drops. */
    {
        guard h1 = guard(&a);
        h1.unlock();
        {
            guard h2 = guard(&a);
            check("seq_inner_holds", h2.holds(), 1);
        }
    }
    a.lock();
    a.unlock();
    check("seq_released", a.is_valid(), 1);

    /* 3. Cross-TU-style: acquire in one function, release in another. */
    {
        guard held = acquire(&a);
        check("cross_acquire_holds", held.holds(), 1);
        release_via_ptr(&held);
        check("cross_release_holds", held.holds(), 0);
    }
    a.lock();
    a.unlock();
    check("cross_relock", a.is_valid(), 1);

    /* 4. Subprocess + timeout: unlock() must actually release the mutex. */
    {
        mutex dm = mutex(); /* normal, non-recursive mutex */
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            guard g = guard(&dm);
            g.unlock();
            alarm(2);
            dm.lock(); /* blocks forever if the guard never released */
            dm.unlock();
            _Exit(0);
        }
        int status = 0;
        alarm(6);
        waitpid(pid, &status, 0);
        alarm(0);
        check("child_rc", WIFEXITED(status) ? WEXITSTATUS(status) : -1, 0);

        /* Positive control: leave it locked, then re-lock -> real deadlock. */
        fflush(stdout);
        pid = fork();
        if (pid == 0) {
            dm.lock();
            alarm(2);
            dm.lock(); /* self-deadlock on the non-recursive mutex */
            dm.unlock();
            _Exit(0);
        }
        status = 0;
        alarm(6);
        waitpid(pid, &status, 0);
        alarm(0);
        check("deadlock_detector", WIFSIGNALED(status) ? 1 : 0, 1);
        dm.drop();
    }

    a.drop();
    b.drop();
    if (failures == 0)
        print("SELF_CHECK_OK");
    else {
        print("SELF_CHECK_FAIL", failures);
        return 1;
    }
    return 0;
}
