/* Task 3 Phase 5 - threading safety regressions: bounded contention.
 *
 * Self-checking.  Every check prints "<tag> <1|0>"; the final line is
 * "THREAD_SAFETY <checks> <failures>".  Exit 0 means zero failures.
 *
 * Coverage: several bounded worker threads contend on one mutex and on a
 * nested pair of mutexes (always first -> second, so no lock-order inversion).
 * Every shared-state write happens under a guard, so the harness never runs
 * an unguarded data race.  Final counters/checksums are compared to values
 * computed with plain loops (no variable modulo/division).
 *
 * A SIGALRM watchdog bounds the whole process: a lost release or a hang exits
 * 90, which the external `timeout 30` runner classifies as a failure, never a
 * pass.  Owning locals use unique names; no chained temporaries are used.
 */
#include "stdlib/thread.hp"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define CT_WORKERS 6
#define CT_ITERS 500
#define CT_YIELDERS 4
#define CT_YIELD_STEPS 64

static int ct_checks = 0;
static int ct_failures = 0;

static void ct_check(const char *tag, int cond) {
    ct_checks++;
    if (!cond)
        ct_failures++;
    print(tag, cond ? 1 : 0);
}

/* Safety net only: a hang must not keep the harness alive. */
static void ct_watchdog(int sig) {
    static const char msg[] = "FATAL ct watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

/* Shared state lives in static storage; no stack address reaches a thread. */
struct ct_shared {
    mutex *first;
    mutex *second;
    long counter;
    long checksum;
};
static struct ct_shared g_ct;

/* One guarded step.  Lock order is always first -> second. */
static void ct_step(struct ct_shared *s, long amount) {
    guard step_guard = guard(s->first);
    s->counter++;
    if ((amount & 7L) == 0) {
        guard nested_guard = guard(s->second);
        s->checksum += amount;
    }
    /* Explicit unlock + implicit drop must not double-unlock. */
    if ((amount & 3L) == 0)
        step_guard.unlock();
}

static void *ct_worker(void *arg) {
    struct ct_shared *s = (struct ct_shared *)arg;
    long i;
    for (i = 1; i <= CT_ITERS; i++)
        ct_step(s, i);
    return NULL;
}

/* Yield while holding the lock to widen the contenders' wait window. */
static void *ct_yield_worker(void *arg) {
    struct ct_shared *s = (struct ct_shared *)arg;
    int i;
    for (i = 0; i < CT_YIELD_STEPS; i++) {
        guard yield_guard = guard(s->first);
        s->checksum++;
        if ((i & 3) == 0)
            sched_yield();
    }
    return NULL;
}

/* Single increment for the stdlib thread-wrapper check. */
static void *ct_once_worker(void *arg) {
    struct ct_shared *s = (struct ct_shared *)arg;
    guard once_guard = guard(s->first);
    s->counter++;
    return NULL;
}

int main(void) {
    error ct_err = error();
    pthread_t ct_raw[CT_WORKERS];
    pthread_t ct_yraw[CT_YIELDERS];
    int started = 0;
    int ystarted = 0;
    int i;
    long expected_counter;
    long expected_checksum;
    long expected_yield;

    (void)!signal(SIGALRM, ct_watchdog);
    (void)!alarm(20);

    {
        mutex ct_first = mutex();
        mutex ct_second = mutex();
        ct_check("ct_mutexes_valid", ct_first.is_valid() && ct_second.is_valid());
        g_ct.first = &ct_first;
        g_ct.second = &ct_second;
        g_ct.counter = 0;
        g_ct.checksum = 0;

        /* ---- 1. exact counters under six contending workers ------------- */
        for (i = 0; i < CT_WORKERS; i++) {
            if (pthread_create(&ct_raw[i], NULL, ct_worker, &g_ct) == 0)
                started++;
            else
                break;
        }
        if (started == CT_WORKERS) {
            for (i = 0; i < CT_WORKERS; i++)
                (void)!pthread_join(ct_raw[i], NULL);
            expected_counter = (long)CT_WORKERS * (long)CT_ITERS;
            expected_checksum = 0;
            for (i = 1; i <= CT_ITERS; i++)
                if ((i & 7) == 0)
                    expected_checksum += i;
            expected_checksum *= (long)CT_WORKERS;
            ct_check("ct_all_workers_started", started == CT_WORKERS);
            ct_check("ct_counter_exact", g_ct.counter == expected_counter);
            ct_check("ct_checksum_exact", g_ct.checksum == expected_checksum);
            print("ct_expect", expected_counter, expected_checksum);
        } else {
            for (i = 0; i < started; i++)
                (void)!pthread_join(ct_raw[i], NULL);
            ct_check("ct_all_workers_started", 0);
            ct_check("ct_counter_exact", 0);
            ct_check("ct_checksum_exact", 0);
        }

        /* ---- 2. contention with a yielding lock holder ------------------ */
        expected_yield = g_ct.checksum;
        for (i = 0; i < CT_YIELDERS; i++) {
            if (pthread_create(&ct_yraw[i], NULL, ct_yield_worker, &g_ct) == 0)
                ystarted++;
            else
                break;
        }
        if (ystarted == CT_YIELDERS) {
            for (i = 0; i < CT_YIELDERS; i++)
                (void)!pthread_join(ct_yraw[i], NULL);
            ct_check("ct_yield_contention_exact",
                     g_ct.checksum == expected_yield +
                                          (long)CT_YIELDERS * (long)CT_YIELD_STEPS);
        } else {
            for (i = 0; i < ystarted; i++)
                (void)!pthread_join(ct_yraw[i], NULL);
            ct_check("ct_yield_contention_exact", 0);
        }

        /* ---- 3. stdlib thread wrapper: start / join / exact increment --- */
        {
            thread ct_wa = thread();
            thread ct_wb = thread();
            void *ct_joined = NULL;
            g_ct.counter = 0;
            int rc_a = ct_wa.start(ct_once_worker, &g_ct, &ct_err);
            int rc_b = ct_wb.start(ct_once_worker, &g_ct, &ct_err);
            ct_check("ct_wrapper_start",
                     rc_a == STDLIB_OK && rc_b == STDLIB_OK && ct_err.ok() &&
                         ct_wa.is_joinable() && ct_wb.is_joinable());
            int ja = ct_wa.join(&ct_joined, &ct_err);
            int jb = ct_wb.join(NULL, &ct_err);
            ct_check("ct_wrapper_join_exact",
                     ja == STDLIB_OK && jb == STDLIB_OK && ct_err.ok() &&
                         ct_joined == NULL && g_ct.counter == 2);
            ct_wa.drop();
            ct_wb.drop();
            ct_check("ct_wrapper_inert_after_drop",
                     !ct_wa.is_joinable() && !ct_wb.is_joinable());
        }

        /* ---- 4. post-contention lock still usable, no lost release ------ */
        ct_first.lock();
        ct_first.unlock();
        ct_second.lock();
        ct_second.unlock();
        ct_check("ct_locks_reusable", ct_first.is_valid() && ct_second.is_valid());

        ct_second.drop();
        ct_first.drop();
        ct_check("ct_mutexes_dropped",
                 !ct_first.is_valid() && !ct_second.is_valid());
    }

    (void)!alarm(0);
    print("THREAD_SAFETY", ct_checks, ct_failures);
    return ct_failures == 0 ? 0 : 1;
}
