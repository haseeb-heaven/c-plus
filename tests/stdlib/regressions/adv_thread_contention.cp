/* Adversarial regression family: bounded multithreaded mutex/guard contention.
 *
 * Self-checking. Every CASE line carries a classification code:
 *   0 = SUCCESS      exact contract result observed
 *   1 = RECOVERABLE  expected rejection handled without crash/hang/leak
 *   2 = FATAL        lost update, deadlock, double-unlock or descriptor damage
 *   3 = SKIP         not portable on this host (reason printed)
 *
 * A SIGALRM watchdog bounds every wait; the process _exit(90)s if a lock is
 * ever held forever. All shared state is mutated under a mutex/guard, so the
 * test itself never introduces an unguarded data race that can crash under a
 * sanitizer. Owning locals use unique names because C+ RAII lowering tracks
 * owned variables by name within a function (a reused name loses its
 * destructor). The final line is ADV_THREAD_CONTENTION; exit is non-zero if
 * any FATAL case was observed.
 */
#include "stdlib/thread.hp"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ADV_WORKERS 8
#define ADV_ITERS   4000

static int g_pass = 0;
static int g_recover = 0;
static int g_fatal = 0;
static int g_skip = 0;

static void on_watchdog(int sig) {
    static const char msg[] = "FATAL watchdog timeout (adv_thread_contention)\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static void result(const char *name, int cls) {
    print("CASE", name, cls);
    if (cls == 0)
        g_pass++;
    else if (cls == 1)
        g_recover++;
    else if (cls == 2)
        g_fatal++;
    else
        g_skip++;
}

/* Shared state reached by workers through static storage; no stack address is
 * handed to pthread_create, so the strict-mode stack-borrow check is moot. */
struct adv_shared {
    mutex *first;
    mutex *second;
    long counter;
    long checksum;
};
static struct adv_shared g_shared;

/* One guarded step per call: the guard is a single function-scope owner, so
 * its destructor always runs exactly once and cannot be lost by loop lowering.
 * Lock order is first -> second everywhere; no thread ever takes them in the
 * reverse order, so the nested case cannot deadlock. */
static void guarded_step(struct adv_shared *s, long amount) {
    guard step_guard = guard(s->first);
    s->counter++;
    if ((amount & 7L) == 0) {
        guard nested_guard = guard(s->second);
        s->checksum += amount;
    }
    /* Explicit unlock; the destructor clears owner and becomes a no-op. */
    if ((amount & 3L) == 0)
        step_guard.unlock();
}

static void *count_worker(void *arg) {
    struct adv_shared *s = (struct adv_shared *)arg;
    long i;
    for (i = 1; i <= ADV_ITERS; i++)
        guarded_step(s, i);
    return NULL;
}

/* A worker that only holds the mutex briefly and yields, to widen the window
 * in which other threads are waiting on the same lock. */
static void *yield_worker(void *arg) {
    struct adv_shared *s = (struct adv_shared *)arg;
    int i;
    for (i = 0; i < 64; i++) {
        guard yield_guard = guard(s->first);
        s->checksum += 1;
        if ((i & 3) == 0)
            sched_yield();
    }
    return NULL;
}

int main(void) {
    error err = error();
    int sentinel = -1;
    pthread_t raw[ADV_WORKERS];
    int started = 0;
    long expected_counter;
    long expected_checksum;
    long i;
    int ok;

    (void)!signal(SIGALRM, on_watchdog);
    (void)!alarm(25);

    sentinel = open("/dev/null", O_RDONLY);
    if (sentinel < 3) {
        result("sentinel_fd", 3);
        if (sentinel >= 0)
            close(sentinel);
    }

    {
        mutex contention_first = mutex();
        mutex contention_second = mutex();
        ok = contention_first.is_valid() && contention_second.is_valid();
        g_shared.first = &contention_first;
        g_shared.second = &contention_second;
        g_shared.counter = 0;
        g_shared.checksum = 0;
        result("mutex_valid", ok ? 0 : 2);

        /* --- 8 raw pthread workers contend on one mutex, nested on another */
        for (i = 0; i < ADV_WORKERS; i++) {
            if (pthread_create(&raw[i], NULL, count_worker, &g_shared) == 0)
                started++;
            else
                break;
        }
        if (started == ADV_WORKERS) {
            for (i = 0; i < ADV_WORKERS; i++)
                (void)!pthread_join(raw[i], NULL);
            expected_counter = (long)ADV_WORKERS * ADV_ITERS;
            expected_checksum = 0;
            for (i = 1; i <= ADV_ITERS; i++)
                if ((i & 7L) == 0)
                    expected_checksum += i;
            expected_checksum *= ADV_WORKERS;
            ok = g_shared.counter == expected_counter &&
                 g_shared.checksum == expected_checksum;
            result("guard_contention_no_lost_updates", ok ? 0 : 2);
            print("CHECK counter", g_shared.counter, expected_counter);
            print("CHECK checksum", g_shared.checksum, expected_checksum);
        } else {
            for (i = 0; i < started; i++)
                (void)!pthread_join(raw[i], NULL);
            result("guard_contention_no_lost_updates", 3);
        }

        /* --- lock holder yields while contenders wait; watchdog covers hang */
        {
            long before = g_shared.checksum;
            int ystarted = 0;
            int y;
            pthread_t yraw[4];
            for (y = 0; y < 4; y++) {
                if (pthread_create(&yraw[y], NULL, yield_worker, &g_shared) == 0)
                    ystarted++;
                else
                    break;
            }
            if (ystarted == 4) {
                for (y = 0; y < 4; y++)
                    (void)!pthread_join(yraw[y], NULL);
                result("contention_with_yield",
                       g_shared.checksum == before + 4 * 64 ? 0 : 2);
            } else {
                for (y = 0; y < ystarted; y++)
                    (void)!pthread_join(yraw[y], NULL);
                result("contention_with_yield", 3);
            }
        }

        /* --- explicit unlock then implicit destructor: no double unlock ---- */
        {
            guard explicit_guard = guard(&contention_first);
            ok = explicit_guard.holds();
            explicit_guard.unlock();
            ok = ok && !explicit_guard.holds();
            explicit_guard.unlock();
            explicit_guard.drop();
            ok = ok && !explicit_guard.holds();
            /* If the guard had double-unlocked, this lock would hang/fatal. */
            contention_first.lock();
            contention_first.unlock();
            result("guard_unlock_then_drop", ok ? 0 : 2);
        }

        /* --- zero-init / NULL guards are safe no-ops ---------------------- */
        {
            guard zero_guard;
            guard null_guard = guard(NULL);
            ok = !zero_guard.holds() && !null_guard.holds();
            zero_guard.unlock();
            zero_guard.drop();
            null_guard.unlock();
            null_guard.drop();
            ok = ok && !zero_guard.holds() && !null_guard.holds();
            contention_first.lock();
            contention_first.unlock();
            result("guard_zero_and_null", ok ? 0 : 2);
        }

        /* --- the stdlib thread wrapper: start / join / double drop -------- */
        {
            thread wrapper_a = thread();
            thread wrapper_b = thread();
            void *joined = NULL;
            ok = !wrapper_a.is_joinable() && !wrapper_b.is_joinable();
            ok = ok && wrapper_a.start(count_worker, &g_shared, &err) == STDLIB_OK;
            ok = ok && wrapper_b.start(count_worker, &g_shared, &err) == STDLIB_OK;
            ok = ok && wrapper_a.is_joinable() && wrapper_b.is_joinable();
            ok = ok && wrapper_a.join(&joined, &err) == STDLIB_OK;
            ok = ok && wrapper_b.join(NULL, &err) == STDLIB_OK;
            wrapper_a.drop();
            wrapper_a.drop();
            wrapper_b.drop();
            wrapper_b.drop();
            ok = ok && !wrapper_a.is_joinable() && !wrapper_b.is_joinable();
            ok = ok && wrapper_a.join(NULL, &err) == STDLIB_FAIL;
            ok = ok && err.failed_now();
            result("thread_wrapper_lifecycle", ok ? 0 : 2);
        }

        /* --- condvar: predicate wait with bounded signalling -------------- */
        {
            condvar done_cv = condvar();
            int signalled = 0;
            int waits = 0;
            ok = done_cv.is_valid();
            guard cv_guard = guard(&contention_first);
            while (g_shared.counter < 0 && waits < 3) {
                (void)!done_cv.wait(&contention_first);
                waits++;
            }
            signalled = done_cv.signal();
            ok = ok && (signalled == 0 || signalled == -1);
            ok = ok && done_cv.broadcast() == 0;
            done_cv.drop();
            done_cv.drop();
            result("condvar_bounded", ok ? 0 : 2);
        }

        /* --- mutex double drop leaves a safe, non-owning object -----------
         * NOTE: guard::holds() reports whether the owner POINTER is set, not
         * whether the mutex still has a live OS handle. A guard over a dropped
         * mutex therefore still holds the pointer, but mutex_lock/unlock on a
         * dropped handle are no-ops, so this stays crash-free. Classify the
         * observation as RECOVERABLE rather than pretending the guard is empty.
         */
        {
            mutex dropped_mutex = mutex();
            int guard_claimed = 0;
            dropped_mutex.drop();
            dropped_mutex.drop();
            ok = !dropped_mutex.is_valid();
            {
                guard on_dropped = guard(&dropped_mutex);
                guard_claimed = on_dropped.holds();
                on_dropped.drop();
            }
            ok = ok && !dropped_mutex.is_valid();
            result("mutex_double_drop", ok ? (guard_claimed ? 1 : 0) : 2);
        }

        result("mutex_scopes_reentrant",
               contention_first.is_valid() && contention_second.is_valid() ? 0 : 2);
        contention_second.drop();
        contention_first.drop();
        contention_first.drop();
        result("mutex_drop_idempotent",
               !contention_first.is_valid() && !contention_second.is_valid() ? 0 : 2);
    }

    if (sentinel >= 3) {
        ok = fcntl(sentinel, F_GETFD) != -1;
        result("sentinel_survived", ok ? 0 : 2);
        close(sentinel);
    }

    (void)!alarm(0);
    print("SUMMARY", g_pass, g_recover, g_fatal, g_skip);
    print("RESULT", g_fatal == 0 ? "PASS" : "FAIL", g_fatal);
    print("ADV_THREAD_CONTENTION");
    return g_fatal == 0 ? 0 : 1;
}
