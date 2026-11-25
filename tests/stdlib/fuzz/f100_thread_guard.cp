/* F-087 -- deterministic fuzz config: guard lifecycle under bounded workers.
 *
 * Config:  F-087 (f100 thread family)
 * Seed:    0xC0FFEE87 (fixed)
 * Ops:     4 workers x 256 guard lifecycles + 256 main-thread lifecycles =
 *          1280 guard acquire/release cycles, plus 64 explicit drop cycles.
 * Build:   --strict --sanitize=none, then --strict --sanitize=undefined.
 * Run:     timeout 30 <out>.
 * Watchdog: alarm(8) -> _Exit(99); external `timeout 30` is the second bound.
 *
 * The mutex is re-initialised as PTHREAD_MUTEX_ERRORCHECK (test-only white-box
 * cast through native()), so a hidden double-unlock takes the runtime fatal
 * path (exit 107) instead of corrupting memory.  Each guard lives in a helper
 * function with a single owning local (the C+ RAII lowering de-duplicates
 * ownership by variable name, so reusing a guard name in one function would
 * drop only one guard).  All shared state is mutated only while the guard
 * holds the mutex: no unguarded race is generated.
 *
 * Last lines: "F087_GUARD <checks> <failures>" and "F087_DIGEST <hex>".
 */
#include "stdlib/thread.hp"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>

#define F087_SEED 0xC0FFEE87ULL
#define F087_WORKERS 4
#define F087_PER_WORKER 256
#define F087_MAIN_OPS 256
#define F087_CYCLES 64
#define F087_WATCHDOG_SECS 8

static unsigned int f087_failures = 0;
static unsigned int f087_checks = 0;

static void f087_check(const char *tag, int cond) {
    f087_checks++;
    if (!cond)
        f087_failures++;
    print(tag, cond ? 1 : 0);
}

static void f087_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

/* Test-only fault injection: make double-unlock observable by the OS. */
static void f087_to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutex_destroy(pm);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

static unsigned long long f087_rnd(unsigned long long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 33;
}

#define F087_FNV_PRIME 1099511628211ULL
static unsigned long long f087_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * F087_FNV_PRIME;
}

typedef struct {
    mutex *m;
    int counter;
    int progress;
    int bad_holds;
    int bad_initial;
    int bad_after_unlock;
    int bad_after_second;
    unsigned long long w_digest[F087_WORKERS];
} f087_ctx;

typedef struct {
    f087_ctx *ctx;
    int id;
} f087_worker_arg;

/* One guard lifetime in its own function scope.  mode 0: explicit unlock()
 * then destructor no-op; mode 1: let the destructor release; mode 2: unlock()
 * then a second unlock() (documented safe no-op).  All are legal single-unlock
 * paths; ERRORCHECK would catch any accidental second unlock.  NOTE: this
 * helper deliberately never calls an explicit .drop(): the C+ RAII lowering
 * treats an explicit drop() as discharging the ownership obligation and would
 * omit the implicit destructor, which is the behaviour under test here. */
static int f087_one_guard(mutex *m, f087_ctx *c, int mode, int i) {
    guard f087_local_guard = guard(m);
    if (!f087_local_guard.holds()) {
        c->bad_holds++;
        c->bad_initial++;
    }
    c->counter++;
    if ((i & 31) == 0)
        c->progress++;
    if (mode == 0 || mode == 2) {
        f087_local_guard.unlock();
        if (f087_local_guard.holds()) {
            c->bad_holds++;
            c->bad_after_unlock++;
        }
    }
    if (mode == 2) {
        f087_local_guard.unlock();     /* second unlock is a safe no-op */
        if (f087_local_guard.holds()) {
            c->bad_holds++;
            c->bad_after_second++;
        }
    }
    return c->bad_holds;
}

static void *f087_worker(void *arg) {
    f087_worker_arg *wa = (f087_worker_arg *)arg;
    f087_ctx *c = wa->ctx;
    mutex *lm = c->m;              /* single-arrow on a local pointer */
    unsigned long long state = F087_SEED + (unsigned long long)wa->id * 0x9E3779B97F4A7C15ULL;
    unsigned long long d = 1469598103934665603ULL ^ (unsigned long long)wa->id;
    int i;
    for (i = 0; i < F087_PER_WORKER; i++) {
        unsigned long long r = f087_rnd(&state);
        int mode = (int)(r % 3ULL);
        f087_one_guard(lm, c, mode, i);
        d = f087_mix(d, (unsigned long long)i);
        d = f087_mix(d, (unsigned long long)mode);
    }
    c->w_digest[wa->id] = d;
    return NULL;
}

int main(void) {
    mutex f087_m = mutex();
    f087_ctx f087_shared;
    error f087_err = error();
    thread f087_t0 = thread();
    thread f087_t1 = thread();
    thread f087_t2 = thread();
    thread f087_t3 = thread();
    thread *f087_threads[F087_WORKERS];
    f087_worker_arg f087_wa[F087_WORKERS];
    unsigned long long f087_main_digest = 1469598103934665603ULL;
    unsigned long long f087_state = F087_SEED + 0xDEADBEEFULL;
    unsigned long long f087_final = 1469598103934665603ULL;
    char f087_hex[32];
    int f087_i;
    int f087_ok = 1;

    signal(SIGALRM, f087_on_alarm);
    f087_check("f087_mutex_valid", f087_m.is_valid() == 1);
    f087_to_errorcheck(&f087_m);   /* fault injection: ERRORCHECK mutex */

    f087_threads[0] = &f087_t0;
    f087_threads[1] = &f087_t1;
    f087_threads[2] = &f087_t2;
    f087_threads[3] = &f087_t3;
    f087_shared.m = &f087_m;
    f087_shared.counter = 0;
    f087_shared.progress = 0;
    f087_shared.bad_holds = 0;
    f087_shared.bad_initial = 0;
    f087_shared.bad_after_unlock = 0;
    f087_shared.bad_after_second = 0;
    for (f087_i = 0; f087_i < F087_WORKERS; f087_i++) {
        f087_shared.w_digest[f087_i] = 0;
        f087_wa[f087_i].ctx = &f087_shared;
        f087_wa[f087_i].id = f087_i;
    }

    alarm(F087_WATCHDOG_SECS);
    for (f087_i = 0; f087_i < F087_WORKERS; f087_i++) {
        thread *lt = f087_threads[f087_i];
        int rc = lt->start(f087_worker, &f087_wa[f087_i], &f087_err);
        if (rc != STDLIB_OK || !f087_err.ok())
            f087_ok = 0;
    }
    for (f087_i = 0; f087_i < F087_MAIN_OPS; f087_i++) {
        unsigned long long r = f087_rnd(&f087_state);
        int mode = (int)(r % 3ULL);
        f087_one_guard(&f087_m, &f087_shared, mode, f087_i);
        f087_main_digest = f087_mix(f087_main_digest, (unsigned long long)f087_i);
        f087_main_digest = f087_mix(f087_main_digest, (unsigned long long)mode);
    }
    for (f087_i = 0; f087_i < F087_WORKERS; f087_i++) {
        thread *lt = f087_threads[f087_i];
        void *res = NULL;
        int rc = lt->join(&res, &f087_err);
        if (rc != STDLIB_OK || !f087_err.ok() || res != NULL)
            f087_ok = 0;
        lt->drop();
    }
    alarm(0);

    f087_check("f087_starts_and_joins", f087_ok == 1);
    f087_check("f087_counter_matches_shadow",
               f087_shared.counter == F087_WORKERS * F087_PER_WORKER + F087_MAIN_OPS);
    f087_check("f087_progress_milestones",
               f087_shared.progress == (F087_WORKERS + 1) * (F087_PER_WORKER / 32));
    f087_check("f087_holds_contract", f087_shared.bad_holds == 0);
    /* A successful relock proves every guard released exactly once. */
    f087_m.lock();
    f087_m.unlock();
    f087_check("f087_relock_after_all_guards", f087_m.is_valid() == 1);

    f087_final = f087_main_digest;
    for (f087_i = 0; f087_i < F087_WORKERS; f087_i++)
        f087_final = f087_mix(f087_final, f087_shared.w_digest[f087_i]);
    f087_final = f087_mix(f087_final, (unsigned long long)f087_shared.counter);

    /* Zero-init and NULL guards are documented safe no-ops. */
    {
        guard f087_zero_guard;
        guard f087_null_guard = guard(NULL);
        f087_check("f087_zero_guard_noop", !f087_zero_guard.holds());
        f087_zero_guard.unlock();
        f087_zero_guard.drop();
        f087_check("f087_null_guard_noop", !f087_null_guard.holds());
        f087_null_guard.unlock();
        f087_null_guard.drop();
    }

    /* Deterministic explicit guard/drop cycles. */
    {
        int cycles_ok = 1;
        for (f087_i = 0; f087_i < F087_CYCLES; f087_i++) {
            mutex f087_cycle_m = mutex();
            guard f087_cycle_guard = guard(&f087_cycle_m);
            if (!f087_cycle_guard.holds())
                cycles_ok = 0;
            f087_cycle_guard.unlock();
            f087_cycle_guard.drop();
            f087_cycle_m.drop();
        }
        f087_check("f087_guard_cycles", cycles_ok == 1 && f087_i == F087_CYCLES);
    }

    f087_m.drop();
    f087_check("f087_post_drop_invalid", f087_m.is_valid() == 0);
    f087_m.drop();
    f087_check("f087_double_drop_inert", f087_m.native() == NULL);

    snprintf(f087_hex, sizeof(f087_hex), "%016llx", f087_final);
    print("F087_DIGEST", f087_hex);
    print("F087_GUARD", f087_checks, f087_failures);
    return f087_failures == 0 ? 0 : 1;
}
