/* Deep test: stdlib/thread.hp guard lifecycle (unlock / drop / holds).
 *
 * Deterministic, self-checking.  Every scenario prints `label value` and
 * compares the observed value with the expected constant embedded below; the
 * program exits 0 only when every check passes.  The exact expected stdout is
 * recorded in docs/stdlib/DEEP_THREAD_CONTRACT.md.
 *
 * Fault injection: the runtime's opaque mutex handle is re-initialised as a
 * PTHREAD_MUTEX_ERRORCHECK mutex (its first field is a pthread_mutex_t).  With
 * that attribute, pthread_mutex_unlock on an unlocked mutex returns EPERM and
 * cplus_mutex_unlock takes the fatal path (CPLUS_EXIT_RESOURCE == 107), so any
 * hidden double-unlock becomes a deterministic, observable failure instead of
 * silent corruption.  A forked positive control deliberately double-unlocks to
 * prove the detector actually fires.  See the contract doc for the portability
 * note on this white-box cast; it is test-only and does not alter the API.
 */
#include "stdlib/thread.hp"
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

static int failures = 0;

static void check(const char *label, int got, int want) {
    print(label, got);
    if (got != want) {
        failures++;
        print("MISMATCH", label, got, want);
    }
}

/* Test-only fault injection: make double-unlock detectable by the OS. */
static void to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutex_destroy(pm);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

int main(void) {
    mutex m = mutex();
    check("valid", m.is_valid(), 1);
    to_errorcheck(&m);

    /* 1. unlock() followed by the compiler-generated destructor cleanup.
     * NOTE: each owning local uses a unique name.  RaiiLowering deduplicates
     * ownership by variable name within a function, so reusing a name in a
     * later sibling scope silently omits its destructor (see contract doc). */
    {
        guard g1 = guard(&m);
        check("acquire_holds", g1.holds(), 1);
        g1.unlock();
        check("after_unlock", g1.holds(), 0);
    } /* guard_drop(&g1) runs here; must not unlock a second time */
    m.lock();
    m.unlock();
    check("relock_after_drop", m.is_valid(), 1);

    /* 2. repeated unlock() then an explicit drop(), then implicit drop. */
    {
        guard g2 = guard(&m);
        g2.unlock();
        g2.unlock();
        g2.drop();
        check("repeat_unlock_drop", g2.holds(), 0);
    }
    m.lock();
    m.unlock();
    check("relock_after_repeat", m.is_valid(), 1);

    /* 3. holds() after unlock() and after drop(). */
    {
        guard g3 = guard(&m);
        g3.unlock();
        check("holds_after_unlock", g3.holds(), 0);
        g3.drop();
        check("holds_after_drop", g3.holds(), 0);
    }

    /* 4. zero-initialised guard (no owner) is a safe no-op everywhere. */
    {
        guard z;
        check("zero_holds", z.holds(), 0);
        z.unlock();
        z.drop();
        check("zero_after", z.holds(), 0);
    }

    /* 5. explicitly constructed NULL-owner guard. */
    {
        guard n = guard(NULL);
        check("null_holds", n.holds(), 0);
        n.unlock();
        check("null_after", n.holds(), 0);
    }

    /* 6. explicit drop() before the scope-exit drop. */
    {
        guard g4 = guard(&m);
        g4.drop();
        check("explicit_drop", g4.holds(), 0);
    }
    m.lock();
    m.unlock();

    /* Fault-injection positive control in a child: a deliberate double unlock
     * on an ERRORCHECK mutex must take the runtime fatal path (107). */
    {
        mutex cm = mutex();
        to_errorcheck(&cm);
        fflush(stdout); /* child must not inherit a dirty stdout buffer */
        pid_t pid = fork();
        if (pid == 0) {
            cm.lock();
            cm.unlock();
            cm.unlock(); /* defect simulation: second unlock must trap */
            _Exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        check("trap_rc", WIFEXITED(status) ? WEXITSTATUS(status) : -1, 107);
        cm.drop();
    }

    m.drop();
    if (failures == 0)
        print("SELF_CHECK_OK");
    else {
        print("SELF_CHECK_FAIL", failures);
        return 1;
    }
    return 0;
}
