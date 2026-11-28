/* F-088 -- deterministic fuzz config: condvar lifecycle under bounded workers.
 *
 * Config:  F-088 (f100 thread family)
 * Seed:    0xC0FFEE88 (fixed)
 * Ops:     4 workers x 256 generation-wait cycles = 1024 condvar waits, plus
 *          256 signal / 64 broadcast calls with no waiter and 64 cv cycles.
 * Build:   --strict --sanitize=none, then --strict --sanitize=undefined.
 * Run:     timeout 30 <out>.
 * Watchdog: alarm(10) -> _Exit(99); external `timeout 30` is the second bound.
 *
 * A generation counter makes every wake deterministic: all workers are proven
 * blocked (under the mutex) before main advances the generation and broadcasts;
 * main then waits until every worker has acknowledged.  No lost wakeup can be
 * generated and no shared state is touched outside the mutex.  The two
 * documented UB cases are SKIPped, not faked: (1) drop() with a blocked waiter
 * (pthread_cond_destroy EBUSY / use-after-free), (2) wait() without the mutex
 * held (POSIX UB).
 *
 * Last lines: "F088_CONDVAR <checks> <failures>" and "F088_DIGEST <hex>".
 */
#include "stdlib/thread.hp"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>

#define F088_SEED 0xC0FFEE88ULL
#define F088_WORKERS 4
#define F088_EPOCHS 256
#define F088_CYCLES 64
#define F088_WATCHDOG_SECS 10

static unsigned int f088_failures = 0;
static unsigned int f088_checks = 0;

static void f088_check(const char *tag, int cond) {
    f088_checks++;
    if (!cond)
        f088_failures++;
    print(tag, cond ? 1 : 0);
}

static void f088_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

static unsigned long long f088_rnd(unsigned long long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 33;
}

#define F088_FNV_PRIME 1099511628211ULL
static unsigned long long f088_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * F088_FNV_PRIME;
}

typedef struct {
    mutex *m;
    condvar *cv;
    int started;                    /* workers that reached wait, under m */
    int epoch;                      /* generation, under m */
    int ack;                        /* acknowledgements, under m */
    int wake_count[F088_WORKERS];   /* per-worker completion count */
} f088_ctx;

typedef struct {
    f088_ctx *ctx;
    int id;
} f088_worker_arg;

static void *f088_worker(void *arg) {
    f088_worker_arg *wa = (f088_worker_arg *)arg;
    f088_ctx *c = wa->ctx;
    mutex *lm = c->m;
    condvar *lc = c->cv;
    int id = wa->id;
    int i;
    for (i = 0; i < F088_EPOCHS; i++) {
        lm->lock();
        if (i == 0)
            c->started++;           /* proves this worker is about to wait */
        {
            int seen = c->epoch;
            while (c->epoch == seen)
                lc->wait(lm);       /* predicate loop: no lost wakeup */
        }
        c->ack++;
        c->wake_count[id]++;
        lm->unlock();
    }
    return NULL;
}

int main(void) {
    mutex f088_m = mutex();
    condvar f088_cv = condvar();
    error f088_err = error();
    thread f088_t0 = thread();
    thread f088_t1 = thread();
    thread f088_t2 = thread();
    thread f088_t3 = thread();
    thread *f088_threads[F088_WORKERS];
    f088_worker_arg f088_wa[F088_WORKERS];
    f088_ctx f088_shared;
    unsigned long long f088_state = F088_SEED;
    unsigned long long f088_digest = 1469598103934665603ULL;
    char f088_hex[32];
    int f088_i;
    int f088_ok = 1;
    int f088_wait_rc = -99;

    signal(SIGALRM, f088_on_alarm);
    f088_check("f088_valid", f088_m.is_valid() == 1 && f088_cv.is_valid() == 1);

    f088_threads[0] = &f088_t0;
    f088_threads[1] = &f088_t1;
    f088_threads[2] = &f088_t2;
    f088_threads[3] = &f088_t3;
    f088_shared.m = &f088_m;
    f088_shared.cv = &f088_cv;
    f088_shared.started = 0;
    f088_shared.epoch = 0;
    f088_shared.ack = 0;
    for (f088_i = 0; f088_i < F088_WORKERS; f088_i++) {
        f088_shared.wake_count[f088_i] = 0;
        f088_wa[f088_i].ctx = &f088_shared;
        f088_wa[f088_i].id = f088_i;
    }

    alarm(F088_WATCHDOG_SECS);
    for (f088_i = 0; f088_i < F088_WORKERS; f088_i++) {
        thread *lt = f088_threads[f088_i];
        int rc = lt->start(f088_worker, &f088_wa[f088_i], &f088_err);
        if (rc != STDLIB_OK || !f088_err.ok())
            f088_ok = 0;
    }

    /* Wait until all four workers are provably blocked in wait(). */
    f088_m.lock();
    while (f088_shared.started < F088_WORKERS) {
        f088_m.unlock();
        usleep(200);
        f088_m.lock();
    }
    f088_check("f088_all_waiting", f088_shared.started == F088_WORKERS);

    /* Drive 256 generations; every broadcast must wake all four workers. */
    for (f088_i = 0; f088_i < F088_EPOCHS; f088_i++) {
        int want = (f088_i + 1) * F088_WORKERS;
        f088_shared.epoch = f088_i + 1;
        if (f088_cv.broadcast() != 0)
            f088_ok = 0;
        while (f088_shared.ack < want) {
            f088_m.unlock();
            usleep(200);
            f088_m.lock();
        }
        f088_digest = f088_mix(f088_digest, (unsigned long long)f088_i);
        f088_digest = f088_mix(f088_digest, (unsigned long long)f088_shared.ack);
        f088_rnd(&f088_state);      /* fixed-seed state advances deterministically */
    }
    f088_m.unlock();

    for (f088_i = 0; f088_i < F088_WORKERS; f088_i++) {
        thread *lt = f088_threads[f088_i];
        void *res = NULL;
        int rc = lt->join(&res, &f088_err);
        if (rc != STDLIB_OK || !f088_err.ok() || res != NULL)
            f088_ok = 0;
        lt->drop();
    }
    alarm(0);

    f088_check("f088_starts_joins_wakeups", f088_ok == 1);
    f088_check("f088_ack_count", f088_shared.ack == F088_EPOCHS * F088_WORKERS);
    {
        int counts_ok = 1;
        for (f088_i = 0; f088_i < F088_WORKERS; f088_i++) {
            if (f088_shared.wake_count[f088_i] != F088_EPOCHS)
                counts_ok = 0;
            f088_digest = f088_mix(f088_digest,
                                   (unsigned long long)f088_shared.wake_count[f088_i]);
        }
        f088_check("f088_per_worker_counts", counts_ok == 1);
    }

    /* signal() / broadcast() with no waiter are defined no-op successes. */
    {
        int no_waiter_ok = 1;
        unsigned long long s = F088_SEED + 0x5EEDULL;
        for (f088_i = 0; f088_i < 256; f088_i++) {
            unsigned long long r = f088_rnd(&s);
            int rc = (r & 1ULL) ? f088_cv.signal() : f088_cv.broadcast();
            if (rc != 0)
                no_waiter_ok = 0;
        }
        f088_check("f088_signal_broadcast_no_waiter", no_waiter_ok == 1);
    }

    /* Zero-init / NULL-argument refusal. */
    {
        condvar f088_zero_cv;
        mutex f088_zero_m;
        int invalid_ok = 1;
        if (f088_zero_cv.is_valid())
            invalid_ok = 0;
        if (f088_zero_cv.signal() != -1 || f088_zero_cv.broadcast() != -1)
            invalid_ok = 0;
        if (f088_zero_cv.wait(&f088_zero_m) != -1)
            invalid_ok = 0;
        if (f088_cv.wait(NULL) != -1 || f088_cv.wait(&f088_zero_m) != -1)
            invalid_ok = 0;
        f088_check("f088_invalid_handle_refusal", invalid_ok == 1);
        f088_zero_cv.drop();
    }

    /* Deterministic construct / drop cycles. */
    {
        int cycles_ok = 1;
        for (f088_i = 0; f088_i < F088_CYCLES; f088_i++) {
            condvar f088_cycle_cv = condvar();
            if (!f088_cycle_cv.is_valid()) {
                cycles_ok = 0;
                break;
            }
            f088_cycle_cv.signal();
            f088_cycle_cv.broadcast();
            f088_cycle_cv.drop();
            f088_cycle_cv.drop();
        }
        f088_check("f088_cv_cycles", cycles_ok == 1 && f088_i == F088_CYCLES);
    }

    /* SKIP (not a fabricated pass): wait() without the mutex held and drop()
     * with a blocked waiter are POSIX UB / use-after-free, not defined C+
     * contracts; see F100_THREAD_JSON_NOTES.md. */
    print("SKIP f088_drop_with_waiter reason=POSIX_cond_destroy_EBUSY_and_storage_freed_without_sync");
    print("SKIP f088_wait_without_mutex reason=POSIX_cond_wait_requires_mutex_held");

    f088_cv.drop();
    f088_wait_rc = f088_cv.wait(&f088_m);
    f088_check("f088_post_drop_wait_refused", f088_cv.is_valid() == 0 &&
                                             f088_wait_rc == -1 &&
                                             f088_cv.signal() == -1 &&
                                             f088_cv.broadcast() == -1);
    f088_cv.drop();
    f088_m.drop();

    snprintf(f088_hex, sizeof(f088_hex), "%016llx", f088_digest);
    print("F088_DIGEST", f088_hex);
    print("F088_CONDVAR", f088_checks, f088_failures);
    return f088_failures == 0 ? 0 : 1;
}
