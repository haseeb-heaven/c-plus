/* Task 3 Phase 5 - deadlock regressions.
 *
 * Self-checking.  Every check prints "<tag> <1|0>"; the final line is
 * "THREAD_SAFETY <checks> <failures>".  Exit 0 means zero failures.
 *
 * Coverage: a real deadlock is provoked only inside forked children, never in
 * the harness process.  Each child installs a SIGALRM handler that exits with
 * the exact code 77; the parent bounds waitpid with its own alarm and asserts
 * WIFEXITED(status) && WEXITSTATUS(status) == 77.  A healthy child that unlocks
 * before relocking must instead exit 0, proving the watchdog does not
 * false-positive.  A second deadlock shape (re-locking a mutex already held by
 * a live guard) exercises the nested-guard hazard.
 *
 * All fatal/deadlock paths are confined to children with stderr redirected to
 * /dev/null, so the parent keeps clean stderr and exits 0.  The harness itself
 * never starts a thread and never performs an unguarded shared-state race.
 */
#include "stdlib/thread.hp"
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define DL_WATCHDOG_SEC 1
#define DL_PARENT_LIMIT 6
#define DL_EXPECTED_EXIT 77

static int dl_checks = 0;
static int dl_failures = 0;

static void dl_check(const char *tag, int cond) {
    dl_checks++;
    if (!cond)
        dl_failures++;
    print(tag, cond ? 1 : 0);
}

/* Async-signal-safe: only _exit is called from the handler. */
static void dl_alarm(int sig) {
    (void)sig;
    _exit(DL_EXPECTED_EXIT);
}

/* Map a wait status to a small integer so the exact exit code is asserted. */
static int dl_status_code(int status) {
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 1000 + WTERMSIG(status);
    return -1;
}

/* kind 0: raw self-deadlock (lock twice).
 * kind 1: re-lock while a guard already owns the same mutex. */
static int dl_deadlock_child(int kind) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        freopen("/dev/null", "w", stderr);
        (void)!signal(SIGALRM, dl_alarm);
        (void)!alarm(DL_WATCHDOG_SEC);
        {
            mutex dl_m = mutex();
            if (kind == 0) {
                dl_m.lock();
                dl_m.lock(); /* non-recursive mutex: blocks forever */
                dl_m.unlock();
            } else {
                guard dl_g = guard(&dl_m);
                dl_m.lock(); /* deadlocks against the guard's own lock */
                dl_m.unlock();
            }
            dl_m.drop();
        }
        _Exit(0); /* reached only if the "deadlock" unexpectedly did not block */
    }
    int status = 0;
    (void)!alarm(DL_PARENT_LIMIT);
    (void)!waitpid(pid, &status, 0);
    (void)!alarm(0);
    return dl_status_code(status);
}

/* Negative control: correct unlock/relock must finish before the watchdog. */
static int dl_healthy_child(void) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        freopen("/dev/null", "w", stderr);
        (void)!signal(SIGALRM, dl_alarm);
        (void)!alarm(DL_WATCHDOG_SEC);
        {
            mutex dl_ok = mutex();
            dl_ok.lock();
            dl_ok.unlock();
            dl_ok.lock();
            dl_ok.unlock();
            dl_ok.drop();
        }
        _Exit(0);
    }
    int status = 0;
    (void)!alarm(DL_PARENT_LIMIT);
    (void)!waitpid(pid, &status, 0);
    (void)!alarm(0);
    return dl_status_code(status);
}

int main(void) {
    int raw_rc = dl_deadlock_child(0);
    dl_check("dl_raw_self_deadlock_exit", raw_rc == DL_EXPECTED_EXIT);

    int guard_rc = dl_deadlock_child(1);
    dl_check("dl_guard_relock_deadlock_exit", guard_rc == DL_EXPECTED_EXIT);

    int healthy_rc = dl_healthy_child();
    dl_check("dl_healthy_child_exit", healthy_rc == 0);

    /* The harness process itself is still responsive after all children. */
    {
        mutex dl_parent_m = mutex();
        dl_parent_m.lock();
        dl_parent_m.unlock();
        dl_check("dl_parent_alive", dl_parent_m.is_valid() != 0);
        dl_parent_m.drop();
    }

    print("THREAD_SAFETY", dl_checks, dl_failures);
    return dl_failures == 0 ? 0 : 1;
}
