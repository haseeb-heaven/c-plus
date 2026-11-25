/* Task 3 Phase 5 - threading safety regressions: condvar coordination.
 *
 * Self-checking.  Every check prints "<tag> <1|0>"; the final line is
 * "THREAD_SAFETY <checks> <failures>".  Exit 0 means zero failures.
 *
 * Coverage: condvar wait / signal / broadcast, the "external mutex held around
 * wait" contract, zero-initialised condvar no-ops, and a broadcast that wakes
 * three concurrent waiters.  Predicates and all shared state are protected by
 * the same mutex the wait uses, so the harness never runs an unguarded race.
 *
 * Each waiter publishes an "entered" flag (atomically) before it blocks; the
 * main thread spins on that flag with a bounded iteration count before it
 * signals, so the wait path is exercised deterministically rather than racing
 * the signal.  A SIGALRM watchdog bounds the entire process (exit 90 on hang).
 */
#include "stdlib/thread.hp"
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static int cv_checks = 0;
static int cv_failures = 0;

static void cv_check(const char *tag, int cond) {
    cv_checks++;
    if (!cond)
        cv_failures++;
    print(tag, cond ? 1 : 0);
}

static void cv_watchdog(int sig) {
    static const char msg[] = "FATAL cv watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

struct cv_slot {
    mutex *m;
    condvar *cv;
    int ready;
    int entered;
    int wait_rc;
    int waits;
};

/* One slot per waiter; distinct static objects avoid an indexed pointer. */
static struct cv_slot g_cv_a;
static struct cv_slot g_cv_b;
static struct cv_slot g_cv_c;

static void cv_slot_init(struct cv_slot *s, mutex *m, condvar *c) {
    s->m = m;
    s->cv = c;
    s->ready = 0;
    s->entered = 0;
    s->wait_rc = 77;
    s->waits = 0;
}

static void *cv_waiter(void *arg) {
    struct cv_slot *s = (struct cv_slot *)arg;
    mutex *local_m = s->m;
    condvar *local_cv = s->cv;
    guard wait_guard = guard(local_m);
    __atomic_store_n(&s->entered, 1, __ATOMIC_RELEASE);
    while (s->ready == 0) {
        s->wait_rc = local_cv->wait(local_m);
        if (s->wait_rc != 0)
            break;
        s->waits++;
    }
    return NULL;
}

/* Bounded spin: never an unguarded infinite loop. */
static void cv_await_entered(struct cv_slot *s) {
    int spins = 0;
    while (__atomic_load_n(&s->entered, __ATOMIC_ACQUIRE) == 0 &&
           spins < 2000000) {
        sched_yield();
        spins++;
    }
}

int main(void) {
    error cv_err = error();
    void *cv_result = NULL;

    (void)!signal(SIGALRM, cv_watchdog);
    (void)!alarm(20);

    /* ---- 1. single waiter, signal --------------------------------------- */
    {
        mutex cv_m = mutex();
        condvar cv = condvar();
        cv_check("cv_valid", cv_m.is_valid() && cv.is_valid());
        cv_slot_init(&g_cv_a, &cv_m, &cv);

        thread cv_t1 = thread();
        int rc = cv_t1.start(cv_waiter, &g_cv_a, &cv_err);
        cv_check("cv_waiter_start", rc == STDLIB_OK && cv_err.ok());
        cv_await_entered(&g_cv_a);
        cv_check("cv_waiter_entered",
                 __atomic_load_n(&g_cv_a.entered, __ATOMIC_ACQUIRE) == 1);

        {
            guard signal_guard = guard(&cv_m);
            g_cv_a.ready = 1;
            int src = cv.signal();
            cv_check("cv_signal_rc", src == 0);
        }
        int jrc = cv_t1.join(&cv_result, &cv_err);
        cv_check("cv_signal_wakes_waiter",
                 jrc == STDLIB_OK && cv_err.ok() && cv_result == NULL &&
                     g_cv_a.wait_rc == 0 && g_cv_a.waits >= 1 &&
                     g_cv_a.ready == 1);
        cv_t1.drop();
        cv.drop();
        cv_m.drop();
    }

    /* ---- 2. three waiters, broadcast ------------------------------------ */
    {
        mutex cv_bm = mutex();
        condvar cv_bc = condvar();
        cv_slot_init(&g_cv_a, &cv_bm, &cv_bc);
        cv_slot_init(&g_cv_b, &cv_bm, &cv_bc);
        cv_slot_init(&g_cv_c, &cv_bm, &cv_bc);

        thread cv_t2 = thread();
        thread cv_t3 = thread();
        thread cv_t4 = thread();
        int r2 = cv_t2.start(cv_waiter, &g_cv_a, &cv_err);
        int r3 = cv_t3.start(cv_waiter, &g_cv_b, &cv_err);
        int r4 = cv_t4.start(cv_waiter, &g_cv_c, &cv_err);
        cv_check("cv_bcast_starts",
                 r2 == STDLIB_OK && r3 == STDLIB_OK && r4 == STDLIB_OK);
        cv_await_entered(&g_cv_a);
        cv_await_entered(&g_cv_b);
        cv_await_entered(&g_cv_c);
        cv_check("cv_bcast_all_entered",
                 __atomic_load_n(&g_cv_a.entered, __ATOMIC_ACQUIRE) == 1 &&
                     __atomic_load_n(&g_cv_b.entered, __ATOMIC_ACQUIRE) == 1 &&
                     __atomic_load_n(&g_cv_c.entered, __ATOMIC_ACQUIRE) == 1);

        {
            guard bcast_guard = guard(&cv_bm);
            g_cv_a.ready = 1;
            g_cv_b.ready = 1;
            g_cv_c.ready = 1;
            int brc = cv_bc.broadcast();
            cv_check("cv_broadcast_rc", brc == 0);
        }
        int j2 = cv_t2.join(NULL, &cv_err);
        int j3 = cv_t3.join(NULL, &cv_err);
        int j4 = cv_t4.join(NULL, &cv_err);
        cv_check("cv_broadcast_wakes_all",
                 j2 == STDLIB_OK && j3 == STDLIB_OK && j4 == STDLIB_OK &&
                     g_cv_a.wait_rc == 0 && g_cv_b.wait_rc == 0 &&
                     g_cv_c.wait_rc == 0 && g_cv_a.waits >= 1 &&
                     g_cv_b.waits >= 1 && g_cv_c.waits >= 1);
        cv_t2.drop();
        cv_t3.drop();
        cv_t4.drop();
        cv_bc.drop();
        cv_bm.drop();
    }

    /* ---- 3. signal / broadcast with no waiters are safe ----------------- */
    {
        mutex cv_nm = mutex();
        condvar cv_nc = condvar();
        cv_check("cv_nolistener_signal", cv_nc.signal() == 0);
        cv_check("cv_nolistener_broadcast", cv_nc.broadcast() == 0);
        cv_nc.drop();
        cv_nm.drop();
    }

    /* ---- 4. zero-initialised condvar is safe and inert ------------------ */
    {
        condvar cv_zero;
        mutex cv_zm = mutex();
        cv_check("cv_zero_invalid", !cv_zero.is_valid());
        cv_check("cv_zero_signal", cv_zero.signal() == -1);
        cv_check("cv_zero_broadcast", cv_zero.broadcast() == -1);
        int wrc = cv_zero.wait(&cv_zm);
        cv_check("cv_zero_wait", wrc == -1);
        cv_zero.drop();
        cv_zero.drop();
        cv_zm.drop();
        cv_check("cv_zero_still_invalid", !cv_zero.is_valid());
    }

    (void)!alarm(0);
    print("THREAD_SAFETY", cv_checks, cv_failures);
    return cv_failures == 0 ? 0 : 1;
}
