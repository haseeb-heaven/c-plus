/* F-086 -- deterministic fuzz config: mutex lifecycle under bounded workers.
 *
 * Config:  F-086 (f100 thread family)
 * Seed:    0xC0FFEE86 (fixed)
 * Ops:     4 workers x 256 + 256 main-thread ops = 1280 locked mutations,
 *          plus 64 create/lock/unlock/drop lifecycle cycles.
 * Build:   c+ build --strict --sanitize=none f100_thread_mutex.cp -o <out>
 *          c+ build --strict --sanitize=undefined f100_thread_mutex.cp -o <out>
 * Run:     timeout 30 <out>
 * Watchdog: alarm(8) -> _Exit(99) if a lock is never released; `timeout 30`
 *          is the second, external bound.  All shared state is touched only
 *          under the mutex, so there is no unguarded race.
 *
 * Self-checking: a seeded shadow model replays every delta and the final
 * counter must match; an FNV-1a digest folds per-thread deterministic values
 * (worker id, iteration, delta) so interleaving cannot change the digest.
 * Last lines: "F086_MUTEX <checks> <failures>" and "F086_DIGEST <hex>".
 * Exit 0 only on zero failures and a matching watchdog state.
 */
#include "stdlib/thread.hp"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>

#define F086_SEED 0xC0FFEE86ULL
#define F086_WORKERS 4
#define F086_PER_WORKER 256
#define F086_MAIN_OPS 256
#define F086_CYCLES 64
#define F086_WATCHDOG_SECS 8

static unsigned int f086_failures = 0;
static unsigned int f086_checks = 0;

static void f086_check(const char *tag, int cond) {
    f086_checks++;
    if (!cond)
        f086_failures++;
    print(tag, cond ? 1 : 0);
}

/* Watchdog: a lost unlock / deadlock must terminate, never hang. */
static void f086_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

static unsigned long long f086_rnd(unsigned long long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 33;
}

#define F086_FNV_PRIME 1099511628211ULL
static unsigned long long f086_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * F086_FNV_PRIME;
}

typedef struct {
    mutex *m;                       /* shared mutex (borrowed) */
    int counter;                    /* mutated only under *m */
    int progress;                   /* milestone count, only under *m */
    unsigned long long w_digest[F086_WORKERS];
} f086_ctx;

typedef struct {
    f086_ctx *ctx;
    int id;
} f086_worker_arg;

/* Each worker: 256 seeded locked mutations.  The mutex pointer is copied into
 * a local before use: chained `c->m->lock()` is mis-lowered by C+. */
static void *f086_worker(void *arg) {
    f086_worker_arg *wa = (f086_worker_arg *)arg;
    f086_ctx *c = wa->ctx;
    mutex *lm = c->m;
    unsigned long long state = F086_SEED + (unsigned long long)wa->id * 0x9E3779B97F4A7C15ULL;
    unsigned long long d = 1469598103934665603ULL ^ (unsigned long long)wa->id;
    int i;
    for (i = 0; i < F086_PER_WORKER; i++) {
        unsigned long long r = f086_rnd(&state);
        int delta = 1 + (int)(r % 3ULL);
        lm->lock();
        c->counter += delta;
        if ((i & 31) == 0)
            c->progress++;
        lm->unlock();
        /* Store a per-slot digest: distinct array elements, written under the
         * mutex above only for the shared counter (digest slot is thread-local
         * by construction). */
        d = f086_mix(d, (unsigned long long)i);
        d = f086_mix(d, (unsigned long long)delta);
    }
    c->w_digest[wa->id] = d;
    return NULL;
}

/* Replay the exact seeded delta stream for one worker id. */
static int f086_replay(unsigned long long state, int n) {
    int sum = 0;
    int i;
    for (i = 0; i < n; i++)
        sum += 1 + (int)(f086_rnd(&state) % 3ULL);
    return sum;
}

int main(void) {
    mutex f086_m = mutex();
    f086_ctx f086_shared;
    error f086_err = error();
    /* Class-typed arrays are NOT constructed by the C+ lowering (local class
     * arrays lower to raw C arrays with no ctor calls), so each thread handle
     * is an individually named owning local reached through a pointer table. */
    thread f086_t0 = thread();
    thread f086_t1 = thread();
    thread f086_t2 = thread();
    thread f086_t3 = thread();
    thread *f086_threads[F086_WORKERS];
    f086_worker_arg f086_wa[F086_WORKERS];
    unsigned long long f086_digests[F086_WORKERS + 1];
    unsigned long long f086_main_digest = 1469598103934665603ULL;
    unsigned long long f086_state = F086_SEED + 0xDEADBEEFULL;
    int f086_expected = 0;
    unsigned long long f086_final = 1469598103934665603ULL;
    char f086_hex[32];
    int f086_i;
    int f086_ok = 1;

    signal(SIGALRM, f086_on_alarm);
    f086_check("f086_mutex_valid", f086_m.is_valid() == 1);

    f086_shared.m = &f086_m;
    f086_shared.counter = 0;
    f086_shared.progress = 0;
    f086_threads[0] = &f086_t0;
    f086_threads[1] = &f086_t1;
    f086_threads[2] = &f086_t2;
    f086_threads[3] = &f086_t3;
    for (f086_i = 0; f086_i < F086_WORKERS; f086_i++) {
        f086_shared.w_digest[f086_i] = 0;
        f086_wa[f086_i].ctx = &f086_shared;
        f086_wa[f086_i].id = f086_i;
    }

    /* --- bounded worker phase, started under no lock (workers take it) --- */
    alarm(F086_WATCHDOG_SECS);
    for (f086_i = 0; f086_i < F086_WORKERS; f086_i++) {
        thread *lt = f086_threads[f086_i];
        int rc = lt->start(f086_worker, &f086_wa[f086_i], &f086_err);
        if (rc != STDLIB_OK || !f086_err.ok())
            f086_ok = 0;
    }
    /* Main thread contributes its own 256 locked mutations. */
    for (f086_i = 0; f086_i < F086_MAIN_OPS; f086_i++) {
        unsigned long long r = f086_rnd(&f086_state);
        int delta = 1 + (int)(r % 3ULL);
        f086_m.lock();
        f086_shared.counter += delta;
        f086_m.unlock();
        f086_main_digest = f086_mix(f086_main_digest, (unsigned long long)f086_i);
        f086_main_digest = f086_mix(f086_main_digest, (unsigned long long)delta);
    }
    for (f086_i = 0; f086_i < F086_WORKERS; f086_i++) {
        thread *lt = f086_threads[f086_i];
        void *res = NULL;
        int rc = lt->join(&res, &f086_err);
        if (rc != STDLIB_OK || !f086_err.ok() || res != NULL)
            f086_ok = 0;
        lt->drop();
    }
    alarm(0);

    f086_check("f086_starts_and_joins", f086_ok == 1);
    /* Independent shadow replay validates that no locked update was lost. */
    f086_expected = f086_replay(F086_SEED + 0xDEADBEEFULL, F086_MAIN_OPS);
    for (f086_i = 0; f086_i < F086_WORKERS; f086_i++)
        f086_expected += f086_replay(F086_SEED + (unsigned long long)f086_i * 0x9E3779B97F4A7C15ULL,
                                     F086_PER_WORKER);
    f086_check("f086_counter_matches_shadow", f086_shared.counter == f086_expected);
    f086_check("f086_progress_milestones",
               f086_shared.progress == F086_WORKERS * (F086_PER_WORKER / 32));

    /* Combine per-thread digests in a fixed order -> interleaving-independent. */
    f086_final = f086_main_digest;
    for (f086_i = 0; f086_i < F086_WORKERS; f086_i++) {
        f086_digests[f086_i] = f086_shared.w_digest[f086_i];
        f086_final = f086_mix(f086_final, f086_digests[f086_i]);
    }
    f086_final = f086_mix(f086_final, (unsigned long long)f086_shared.counter);

    /* --- deterministic create / lock / unlock / drop cycles --------------- */
    {
        int cycles_ok = 1;
        for (f086_i = 0; f086_i < F086_CYCLES; f086_i++) {
            mutex f086_cycle_m = mutex();
            if (!f086_cycle_m.is_valid()) {
                cycles_ok = 0;
                break;
            }
            f086_cycle_m.lock();
            f086_cycle_m.unlock();
            f086_cycle_m.drop();       /* explicit; implicit drop is a no-op */
            f086_cycle_m.drop();       /* double drop is inert */
            if (f086_cycle_m.is_valid() || f086_cycle_m.native() != NULL)
                cycles_ok = 0;
        }
        f086_check("f086_lifecycle_cycles", cycles_ok == 1 && f086_i == F086_CYCLES);
    }

    /* --- post-drop refusal on the primary handle (no UAF) ---------------- */
    f086_m.drop();
    f086_check("f086_post_drop_invalid", f086_m.is_valid() == 0);
    f086_m.lock();                     /* documented no-op on a dropped handle */
    f086_m.unlock();
    f086_m.drop();                     /* idempotent */
    f086_check("f086_double_drop_inert", f086_m.native() == NULL);

    snprintf(f086_hex, sizeof(f086_hex), "%016llx", f086_final);
    print("F086_DIGEST", f086_hex);
    print("F086_MUTEX", f086_checks, f086_failures);
    return f086_failures == 0 ? 0 : 1;
}
