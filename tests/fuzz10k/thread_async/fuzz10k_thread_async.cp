/* FUZZ10K / thread_async -- 12,000 deterministic metadata fuzz scenarios.
 *
 * SCOPE
 *   stdlib/thread.hp (mutex / guard / condvar / thread)
 *   stdlib/async.hp  (loop / timer -- libuv backed)
 *   stdlib/lifetime_adapt.hp (owner / borrow / generation tokens)
 *   stdlib/allocator.hp + stdlib/error.hp (generation / liveness helpers)
 *
 * DESIGN (deterministic only)
 *   One fixed PRNG seed (FUZZ_SEED, splitmix64) drives every scenario.  There
 *   is no clock, no pid, no environment input and no rand().  Each scenario
 *   picks one case from a static table and receives two derived 64-bit
 *   parameters that vary the length/size/value of a short operation sequence.
 *   "future" is a modelled single-consumption result handle (thread::join +
 *   lifetime result slot); stdlib has no dedicated future type.  This is an
 *   honest modelling choice, documented in NOTES.md.
 *
 *   Every case either hits a documented REFUSAL (metadata error with a defined
 *   category/code) or a DEFINED result, and leaves two canaries plus the
 *   stdlib live-block count intact.  Stale pointers are consulted through
 *   metadata only and are NEVER dereferenced.  Worker threads are tiny and
 *   always joined; double-lock is made observable as EDEADLK by reconfiguring
 *   a test-only mutex as PTHREAD_MUTEX_ERRORCHECK, so no scenario can deadlock.
 *
 *   Per-scenario bound: alarm(5) is armed before each case and cleared after,
 *   so a stuck join/loop terminates with _Exit(99) instead of hanging.  The
 *   external `timeout 600` in run.sh is a second, process-wide bound.  No
 *   wall-clock value is ever asserted.
 *
 * OUTPUT
 *   scenarios.log : one line per scenario -- S <id> <api> <case> <result> <ok>
 *                   followed by FUZZ10K_DISTINCT and FUZZ10K_DIGEST lines.
 *   stdout        : digest line, then exactly
 *                   FUZZ10K thread_async <scenarios> <crashes> <hangs> <corruptions> <refusals>
 *   Exit status is non-zero iff any corruption/refusal-contract failure or a
 *   duplicate scenario tuple is observed.  A hang/crash prevents the final
 *   line entirely; run.sh maps that abnormal exit to the crashes/hangs count.
 *   This is evidence for the tested inputs, NOT a proof of safety.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "stdlib/thread.hp"
#include "stdlib/async.hp"
#include "stdlib/lifetime_adapt.hp"
#include "stdlib/allocator.hp"

#define FZ_N 12000
#define FZ_SEED 0xF0A7704A5EED5A17ULL
#define FZ_WATCHDOG_SECS 5
#define FZ_CANARY_A 0xA11CE15EDEADBEEFULL
#define FZ_CANARY_B 0x5EED5A17C0FFEE01ULL
#define FZ_WORK_CONST 0x5DEECE66DULL

static unsigned long long g_failures = 0;   /* corruptions / contract breaks */
static unsigned long long g_refusals = 0;   /* correct documented refusals   */
static unsigned long long g_state_ok = 0;
static unsigned long long g_digest = 1469598103934665603ULL;
static FILE *g_log = (FILE *)0;

/* Test-only ERRORCHECK mutex, configured once in main() for double-lock. */
static mutex *g_ec_ptr;
static int g_ec_ok = 0;

typedef struct fz_tuple {
    int case_index;
    unsigned long long p1;
    unsigned long long p2;
} fz_tuple;

static fz_tuple g_tuple[FZ_N];

typedef struct fz_state {
    unsigned long long canary_a;
    unsigned long long canary_b;
    size_t base_live;
    int fail;
} fz_state;

#define FZ_REQUIRE(st, cond)                                                   \
    do {                                                                       \
        if (!(cond))                                                           \
            (st)->fail = 1;                                                    \
    } while (0)

static void fz_state_init(fz_state *s) {
    s->canary_a = FZ_CANARY_A;
    s->canary_b = FZ_CANARY_B;
    s->base_live = stdlib_state.meta_live;
    s->fail = 0;
}

static int fz_state_ok(const fz_state *s) {
    return s->canary_a == FZ_CANARY_A && s->canary_b == FZ_CANARY_B &&
           s->base_live == stdlib_state.meta_live && s->fail == 0;
}

static unsigned long long fz_rnd(unsigned long long *s) {
    unsigned long long z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

#define FZ_FNV_PRIME 1099511628211ULL
static unsigned long long fz_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FZ_FNV_PRIME;
}

static unsigned long long fz_hash_str(const char *s) {
    unsigned long long d = 1469598103934665603ULL;
    while (s != (const char *)0 && *s != '\0') {
        d = fz_mix(d, (unsigned long long)(unsigned char)*s);
        s++;
    }
    return d;
}

static void fz_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

static void fz_to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutexattr_t attr;
    pthread_mutex_destroy(pm);
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

static void *fz_worker(void *arg) {
    unsigned long long *p = (unsigned long long *)arg;
    unsigned long long v = *p ^ FZ_WORK_CONST;
    return (void *)(uintptr_t)v;
}

static void fz_noop_cb(uv_timer_t *h) {
    (void)h;
}

typedef int (*fz_case_fn)(fz_state *s, unsigned long long p1,
                          unsigned long long p2, const char **result_out,
                          int *refused_out);

/* ======================================================================= */
/* mutex family                                                              */
/* ======================================================================= */

static int cs_mutex_cycle(fz_state *s, unsigned long long p1,
                          unsigned long long p2, const char **res, int *ref) {
    mutex m = mutex();
    unsigned int n = (unsigned int)(1u + (p1 % 4ULL));
    unsigned int i;
    (void)p2;
    *res = "defined";
    *ref = 0;
    FZ_REQUIRE(s, m.is_valid() == 1);
    for (i = 0; i < n; i++) {
        m.lock();
        m.unlock();
    }
    m.drop();
    FZ_REQUIRE(s, m.native() == (void *)0 && m.is_valid() == 0);
    m.drop();
    FZ_REQUIRE(s, m.native() == (void *)0);
    return s->fail == 0;
}

static int cs_mutex_double_unlock(fz_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    mutex m = mutex();
    guard g = guard(&m);
    (void)p1;
    (void)p2;
    *res = "noop";
    *ref = 1;
    FZ_REQUIRE(s, g.holds() == 1);
    g.unlock();
    FZ_REQUIRE(s, g.holds() == 0);
    g.unlock();
    FZ_REQUIRE(s, g.holds() == 0);
    m.lock();
    m.unlock();
    g.drop();
    m.drop();
    FZ_REQUIRE(s, m.native() == (void *)0);
    return s->fail == 0;
}

/* Second lock is refused with EDEADLK by the ERRORCHECK test mutex. */
static int cs_mutex_double_lock(fz_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    mutex *m = g_ec_ptr;
    int rc1;
    int rc2;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    if (!g_ec_ok || m == (mutex *)0) {
        s->fail = 1;
        return 0;
    }
    rc1 = pthread_mutex_lock((pthread_mutex_t *)m->native());
    rc2 = pthread_mutex_lock((pthread_mutex_t *)m->native());
    pthread_mutex_unlock((pthread_mutex_t *)m->native());
    FZ_REQUIRE(s, rc1 == 0);
    FZ_REQUIRE(s, rc2 == EDEADLK);
    return s->fail == 0;
}

static int cs_mutex_double_drop(fz_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    mutex m = mutex();
    unsigned int n = (unsigned int)(1u + (p2 % 3ULL));
    unsigned int i;
    (void)p1;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, m.is_valid() == 1);
    m.drop();
    FZ_REQUIRE(s, m.native() == (void *)0);
    for (i = 0; i < n; i++)
        m.drop();
    FZ_REQUIRE(s, m.native() == (void *)0 && m.is_valid() == 0);
    return s->fail == 0;
}

static int cs_mutex_zero_handle(fz_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    mutex m;
    unsigned int n = (unsigned int)(1u + (p1 % 3ULL));
    unsigned int i;
    (void)p2;
    *res = "noop";
    *ref = 0;
    FZ_REQUIRE(s, m.is_valid() == 0);
    for (i = 0; i < n; i++) {
        m.lock();
        m.unlock();
    }
    m.drop();
    FZ_REQUIRE(s, m.native() == (void *)0);
    return s->fail == 0;
}

static int cs_mutex_null_guard(fz_state *s, unsigned long long p1,
                               unsigned long long p2, const char **res,
                               int *ref) {
    guard g = guard((mutex *)0);
    (void)p1;
    (void)p2;
    *res = "noop";
    *ref = 1;
    FZ_REQUIRE(s, g.holds() == 0);
    g.unlock();
    g.drop();
    FZ_REQUIRE(s, g.holds() == 0);
    return s->fail == 0;
}

static int cs_mutex_guard_drop(fz_state *s, unsigned long long p1,
                               unsigned long long p2, const char **res,
                               int *ref) {
    mutex m = mutex();
    unsigned int i;
    unsigned int n = (unsigned int)(1u + (p1 % 3ULL));
    (void)p2;
    *res = "defined";
    *ref = 0;
    for (i = 0; i < n; i++) {
        guard g = guard(&m);
        FZ_REQUIRE(s, g.holds() == 1);
        g.drop();
        FZ_REQUIRE(s, g.holds() == 0);
        m.lock(); /* relock proves the guard released exactly once */
        m.unlock();
    }
    m.drop();
    return s->fail == 0;
}

/* ======================================================================= */
/* condvar family                                                            */
/* ======================================================================= */

static int cs_cv_signal_broadcast(fz_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    condvar cv = condvar();
    unsigned int n = (unsigned int)(1u + (p1 % 4ULL));
    unsigned int i;
    (void)p2;
    *res = "defined";
    *ref = 0;
    FZ_REQUIRE(s, cv.is_valid() == 1);
    for (i = 0; i < n; i++) {
        FZ_REQUIRE(s, cv.signal() == 0);
        FZ_REQUIRE(s, cv.broadcast() == 0);
    }
    cv.drop();
    cv.drop();
    FZ_REQUIRE(s, cv.is_valid() == 0);
    return s->fail == 0;
}

static int cs_cv_wait_invalid(fz_state *s, unsigned long long p1,
                              unsigned long long p2, const char **res,
                              int *ref) {
    condvar cv = condvar();
    mutex m;
    unsigned int n = (unsigned int)(1u + (p1 % 3ULL));
    unsigned int i;
    (void)p2;
    *res = "refused";
    *ref = 1;
    for (i = 0; i < n; i++) {
        FZ_REQUIRE(s, cv.wait((mutex *)0) == -1);
        FZ_REQUIRE(s, cv.wait(&m) == -1); /* zero mutex: refuse before blocking */
    }
    cv.drop();
    return s->fail == 0;
}

static int cs_cv_zero_handle(fz_state *s, unsigned long long p1,
                             unsigned long long p2, const char **res, int *ref) {
    condvar cv;
    mutex m = mutex();
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, cv.is_valid() == 0);
    FZ_REQUIRE(s, cv.signal() == -1);
    FZ_REQUIRE(s, cv.broadcast() == -1);
    FZ_REQUIRE(s, cv.wait(&m) == -1);
    cv.drop();
    m.drop();
    FZ_REQUIRE(s, cv.is_valid() == 0);
    return s->fail == 0;
}

static int cs_cv_double_drop(fz_state *s, unsigned long long p1,
                             unsigned long long p2, const char **res, int *ref) {
    condvar cv = condvar();
    unsigned int n = (unsigned int)(1u + (p2 % 3ULL));
    unsigned int i;
    (void)p1;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, cv.is_valid() == 1);
    cv.drop();
    for (i = 0; i < n; i++)
        cv.drop();
    FZ_REQUIRE(s, cv.is_valid() == 0);
    return s->fail == 0;
}

static int cs_cv_after_drop(fz_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res, int *ref) {
    condvar cv = condvar();
    mutex m = mutex();
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    cv.drop();
    FZ_REQUIRE(s, cv.signal() == -1);
    FZ_REQUIRE(s, cv.broadcast() == -1);
    FZ_REQUIRE(s, cv.wait(&m) == -1);
    m.drop();
    return s->fail == 0;
}

/* ======================================================================= */
/* thread family                                                             */
/* ======================================================================= */

static int cs_thread_start_join(fz_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1 ^ p2;
    void *out = (void *)0;
    *res = "defined";
    *ref = 0;
    FZ_REQUIRE(s, thr.is_joinable() == 0);
    FZ_REQUIRE(s, thr.start(fz_worker, &in, &err) == STDLIB_OK && err.ok());
    FZ_REQUIRE(s, thr.is_joinable() == 1);
    FZ_REQUIRE(s, thr.join(&out, &err) == STDLIB_OK && err.ok());
    FZ_REQUIRE(s, out == (void *)(uintptr_t)(in ^ FZ_WORK_CONST));
    FZ_REQUIRE(s, thr.is_joinable() == 0);
    thr.drop();
    return s->fail == 0;
}

static int cs_thread_double_start(fz_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1;
    void *out = (void *)0;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, thr.start(fz_worker, &in, &err) == STDLIB_OK);
    FZ_REQUIRE(s, thr.start(fz_worker, &in, &err) == STDLIB_FAIL);
    FZ_REQUIRE(s, err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                       err.code() == CPLUS_EXIT_RESOURCE);
    FZ_REQUIRE(s, thr.is_joinable() == 1);
    FZ_REQUIRE(s, thr.join(&out, &err) == STDLIB_OK);
    thr.drop();
    return s->fail == 0;
}

static int cs_thread_failed_join(fz_state *s, unsigned long long p1,
                                 unsigned long long p2, const char **res,
                                 int *ref) {
    thread thr = thread();
    error err = error();
    void *out = (void *)0;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, thr.join(&out, &err) == STDLIB_FAIL);
    FZ_REQUIRE(s, err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                       err.code() == CPLUS_EXIT_RESOURCE);
    FZ_REQUIRE(s, out == (void *)0);
    thr.drop();
    return s->fail == 0;
}

static int cs_thread_null_fn(fz_state *s, unsigned long long p1,
                             unsigned long long p2, const char **res, int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p2;
    (void)p1;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, thr.start((void *(*)(void *))0, &in, &err) == STDLIB_FAIL);
    FZ_REQUIRE(s, err.failed_now() && err.code() == CPLUS_EXIT_UNSUPPORTED);
    FZ_REQUIRE(s, thr.is_joinable() == 0);
    thr.drop();
    return s->fail == 0;
}

static int cs_thread_drop_joins(fz_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1 ^ p2;
    void *out = (void *)0;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, thr.start(fz_worker, &in, &err) == STDLIB_OK);
    thr.drop();
    FZ_REQUIRE(s, thr.is_joinable() == 0);
    FZ_REQUIRE(s, thr.join(&out, &err) == STDLIB_FAIL && err.failed_now());
    thr.drop();
    return s->fail == 0;
}

/* Small bounded pool of distinct named handles (class arrays are not ctor'd). */
static int cs_thread_multi(fz_state *s, unsigned long long p1,
                           unsigned long long p2, const char **res, int *ref) {
    thread t0 = thread();
    thread t1 = thread();
    thread t2 = thread();
    thread *tv[3];
    unsigned long long v[3];
    void *out = (void *)0;
    error err = error();
    unsigned int n = (unsigned int)(1u + (p1 % 3ULL));
    unsigned int i;
    (void)p2;
    *res = "defined";
    *ref = 0;
    tv[0] = &t0;
    tv[1] = &t1;
    tv[2] = &t2;
    v[0] = p1;
    v[1] = p2;
    v[2] = p1 ^ p2;
    for (i = 0; i < n; i++)
        FZ_REQUIRE(s, tv[i]->start(fz_worker, &v[i], &err) == STDLIB_OK);
    for (i = 0; i < n; i++) {
        FZ_REQUIRE(s, tv[i]->join(&out, &err) == STDLIB_OK);
        FZ_REQUIRE(s, out == (void *)(uintptr_t)(v[i] ^ FZ_WORK_CONST));
        tv[i]->drop();
    }
    return s->fail == 0;
}

/* ======================================================================= */
/* future family (single-consumption result modelled via thread/lifetime)    */
/* ======================================================================= */

static int cs_future_get_once(fz_state *s, unsigned long long p1,
                              unsigned long long p2, const char **res, int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1 ^ p2;
    void *out = (void *)0;
    *res = "defined";
    *ref = 0;
    FZ_REQUIRE(s, thr.start(fz_worker, &in, &err) == STDLIB_OK);
    FZ_REQUIRE(s, thr.join(&out, &err) == STDLIB_OK);
    FZ_REQUIRE(s, out == (void *)(uintptr_t)(in ^ FZ_WORK_CONST));
    thr.drop();
    return s->fail == 0;
}

static int cs_future_double_get(fz_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1;
    void *out = (void *)0;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, thr.start(fz_worker, &in, &err) == STDLIB_OK);
    FZ_REQUIRE(s, thr.join(&out, &err) == STDLIB_OK);
    FZ_REQUIRE(s, thr.join(&out, &err) == STDLIB_FAIL);
    FZ_REQUIRE(s, err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_LIFECYCLE);
    FZ_REQUIRE(s, out == (void *)(uintptr_t)(in ^ FZ_WORK_CONST));
    thr.drop();
    return s->fail == 0;
}

static int cs_future_before_start(fz_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    thread thr = thread();
    error err = error();
    void *out = (void *)0;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, thr.join(&out, &err) == STDLIB_FAIL && err.failed_now());
    FZ_REQUIRE(s, out == (void *)0);
    thr.drop();
    return s->fail == 0;
}

static int cs_future_consumed_slot(fz_state *s, unsigned long long p1,
                                   unsigned long long p2, const char **res,
                                   int *ref) {
    lifetime_owner o;
    lifetime_borrow b;
    error err = error();
    unsigned long long val = p1 ^ p2;
    unsigned char slot[8];
    int i;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 8; i++)
        slot[i] = 0xAAu;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, lifetime_owner_init(&o, 16u, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_borrow_from(&b, &o, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_write(&b, 0u, &val, 8u, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_read(&b, 0u, slot, 8u, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    for (i = 0; i < 8; i++)
        slot[i] = 0xAAu;
    FZ_REQUIRE(s, lifetime_borrow_is_live(&b) == 0);
    FZ_REQUIRE(s, lifetime_read(&b, 0u, slot, 8u, &err) == STDLIB_FAIL &&
                       err.code() == CPLUS_EXIT_USE_AFTER_FREE);
    FZ_REQUIRE(s, slot[0] == 0xAAu);
    return s->fail == 0;
}

/* ======================================================================= */
/* timer family                                                              */
/* ======================================================================= */

static int cs_timer_lifecycle(fz_state *s, unsigned long long p1,
                              unsigned long long p2, const char **res, int *ref) {
    error err = error();
    loop l = loop(&err);
    timer tmr = timer(l.native(), &err);
    unsigned long long user = p1 ^ p2;
    unsigned int n = (unsigned int)(1u + (p1 % 3ULL));
    unsigned int i;
    *res = "defined";
    *ref = 0;
    FZ_REQUIRE(s, l.valid() == 1);
    FZ_REQUIRE(s, tmr.valid() == 1);
    for (i = 0; i < n; i++) {
        FZ_REQUIRE(s, tmr.start(fz_noop_cb, 3600000ULL, 0ULL, &user, &err) ==
                           STDLIB_OK);
        FZ_REQUIRE(s, tmr.is_active() == 1);
        FZ_REQUIRE(s, tmr.stop(&err) == STDLIB_OK && err.ok());
        FZ_REQUIRE(s, tmr.is_active() == 0);
    }
    tmr.drop();
    FZ_REQUIRE(s, tmr.valid() == 0);
    l.drop();
    FZ_REQUIRE(s, l.valid() == 0);
    return s->fail == 0;
}

static int cs_timer_null_loop(fz_state *s, unsigned long long p1,
                              unsigned long long p2, const char **res, int *ref) {
    error err = error();
    timer tmr = timer((uv_loop_t *)0, &err);
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, tmr.valid() == 0);
    FZ_REQUIRE(s, err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_INVALID);
    tmr.drop();
    FZ_REQUIRE(s, tmr.valid() == 0);
    return s->fail == 0;
}

static int cs_timer_null_cb(fz_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res, int *ref) {
    error err = error();
    loop l = loop(&err);
    timer tmr = timer(l.native(), &err);
    unsigned long long user = p1;
    unsigned int n = (unsigned int)(1u + (p2 % 3ULL));
    unsigned int i;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, tmr.valid() == 1);
    for (i = 0; i < n; i++) {
        FZ_REQUIRE(s, tmr.start((uv_timer_cb)0, 3600000ULL, 0ULL, &user, &err) ==
                           STDLIB_FAIL);
        FZ_REQUIRE(s, err.failed_now() && err.code() == CPLUS_EXIT_UNSUPPORTED);
    }
    tmr.drop();
    l.drop();
    FZ_REQUIRE(s, tmr.valid() == 0);
    return s->fail == 0;
}

static int cs_timer_stop_invalid(fz_state *s, unsigned long long p1,
                                 unsigned long long p2, const char **res,
                                 int *ref) {
    error err = error();
    timer tmr;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, tmr.valid() == 0);
    FZ_REQUIRE(s, tmr.stop(&err) == STDLIB_FAIL && err.failed_now());
    FZ_REQUIRE(s, err.code() == CPLUS_EXIT_UNSUPPORTED);
    tmr.drop();
    FZ_REQUIRE(s, tmr.valid() == 0);
    return s->fail == 0;
}

static int cs_timer_double_drop(fz_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    error err = error();
    loop l = loop(&err);
    timer tmr = timer(l.native(), &err);
    unsigned int n = (unsigned int)(1u + (p2 % 3ULL));
    unsigned int i;
    (void)p1;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, tmr.valid() == 1);
    tmr.drop();
    for (i = 0; i < n; i++)
        tmr.drop();
    FZ_REQUIRE(s, tmr.valid() == 0);
    l.drop();
    return s->fail == 0;
}

/* ======================================================================= */
/* lifetime family                                                           */
/* ======================================================================= */

static int cs_life_valid_rw(fz_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res, int *ref) {
    lifetime_owner o;
    lifetime_borrow b;
    error err = error();
    unsigned char src[8];
    unsigned char dst[8];
    size_t bytes = (size_t)(16u + (p2 % 64u));
    int i;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 8; i++) {
        src[i] = (unsigned char)(p1 + (unsigned long long)i);
        dst[i] = 0u;
    }
    *res = "defined";
    *ref = 0;
    FZ_REQUIRE(s, lifetime_owner_init(&o, bytes, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_borrow_from(&b, &o, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_write(&b, 0u, src, 8u, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_read(&b, 0u, dst, 8u, &err) == STDLIB_OK);
    for (i = 0; i < 8; i++)
        FZ_REQUIRE(s, dst[i] == src[i]);
    FZ_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    FZ_REQUIRE(s, o.ptr == (void *)0 && o.token == LIFETIME_NO_TOKEN);
    return s->fail == 0;
}

static int cs_life_stale_token(fz_state *s, unsigned long long p1,
                               unsigned long long p2, const char **res, int *ref) {
    lifetime_owner o;
    lifetime_borrow b;
    error err = error();
    void *saved_ptr;
    lifetime_token_t saved_token;
    unsigned char src[4];
    unsigned char dst[4];
    unsigned int n = (unsigned int)(1u + (p2 % 3ULL));
    unsigned int i;
    unsigned int k;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 4; i++) {
        src[i] = (unsigned char)(p1 + (unsigned long long)i);
        dst[i] = 0xAAu;
    }
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, lifetime_owner_init(&o, 16u, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_borrow_from(&b, &o, &err) == STDLIB_OK);
    saved_ptr = b.ptr;
    saved_token = b.token;
    FZ_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    /* metadata only: never dereference saved_ptr */
    FZ_REQUIRE(s, stdlib_alloc_generation(saved_ptr) == LIFETIME_NO_TOKEN);
    FZ_REQUIRE(s, b.token == saved_token && saved_token != LIFETIME_NO_TOKEN);
    for (k = 0; k < n; k++) {
        FZ_REQUIRE(s, lifetime_borrow_is_live(&b) == 0);
        FZ_REQUIRE(s, lifetime_read(&b, 0u, dst, 4u, &err) == STDLIB_FAIL &&
                           err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                           err.code() == CPLUS_EXIT_USE_AFTER_FREE);
        FZ_REQUIRE(s, lifetime_write(&b, 0u, src, 4u, &err) == STDLIB_FAIL);
    }
    for (i = 0; i < 4; i++)
        FZ_REQUIRE(s, dst[i] == 0xAAu);
    return s->fail == 0;
}

static int cs_life_double_release(fz_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    lifetime_owner o;
    error err = error();
    size_t live_before;
    unsigned int n = (unsigned int)(1u + (p1 % 3ULL));
    unsigned int i;
    (void)p2;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, lifetime_owner_init(&o, 16u, &err) == STDLIB_OK);
    live_before = stdlib_state.meta_live;
    FZ_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    FZ_REQUIRE(s, stdlib_state.meta_live == live_before - 1u);
    for (i = 0; i < n; i++) {
        FZ_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_FAIL);
        FZ_REQUIRE(s, err.category() == STDLIB_CATEGORY_OWNERSHIP &&
                           err.code() == CPLUS_EXIT_DOUBLE_FREE);
    }
    FZ_REQUIRE(s, stdlib_state.meta_live == live_before - 1u);
    return s->fail == 0;
}

static int cs_life_transfer(fz_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res, int *ref) {
    lifetime_owner a;
    lifetime_owner c;
    lifetime_borrow b;
    error err = error();
    unsigned char src[4];
    unsigned char dst[4];
    lifetime_token_t ta;
    unsigned int n = (unsigned int)(1u + (p1 % 2ULL));
    unsigned int i;
    a.ptr = (void *)0;
    a.token = LIFETIME_NO_TOKEN;
    c.ptr = (void *)0;
    c.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 4; i++) {
        src[i] = (unsigned char)(p1 + p2 + (unsigned long long)i);
        dst[i] = 0u;
    }
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, lifetime_owner_init(&a, 16u, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_borrow_from(&b, &a, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_write(&b, 0u, src, 4u, &err) == STDLIB_OK);
    ta = a.token;
    FZ_REQUIRE(s, lifetime_transfer(&c, &a, &err) == STDLIB_OK);
    FZ_REQUIRE(s, a.ptr == (void *)0 && a.token == LIFETIME_NO_TOKEN);
    FZ_REQUIRE(s, c.ptr != (void *)0 && c.token != LIFETIME_NO_TOKEN &&
                       c.token != ta);
    for (i = 0; i < n; i++) {
        FZ_REQUIRE(s, lifetime_borrow_is_live(&b) == 0);
        FZ_REQUIRE(s, lifetime_read(&b, 0u, dst, 4u, &err) == STDLIB_FAIL);
    }
    FZ_REQUIRE(s, dst[0] == 0u);
    FZ_REQUIRE(s, lifetime_borrow_from(&b, &a, &err) == STDLIB_FAIL);
    FZ_REQUIRE(s, lifetime_owner_release(&a, &err) == STDLIB_FAIL);
    FZ_REQUIRE(s, lifetime_owner_release(&c, &err) == STDLIB_OK);
    return s->fail == 0;
}

static int cs_life_null_handles(fz_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    lifetime_borrow b;
    error err = error();
    unsigned char dst[4];
    (void)p1;
    (void)p2;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, lifetime_owner_init((lifetime_owner *)0, 16u, &err) ==
                       STDLIB_FAIL);
    FZ_REQUIRE(s, lifetime_owner_release((lifetime_owner *)0, &err) ==
                       STDLIB_FAIL);
    FZ_REQUIRE(s, lifetime_transfer((lifetime_owner *)0, (lifetime_owner *)0,
                                    &err) == STDLIB_FAIL);
    FZ_REQUIRE(s, lifetime_borrow_from(&b, (lifetime_owner *)0, &err) ==
                       STDLIB_FAIL);
    FZ_REQUIRE(s, lifetime_read((lifetime_borrow *)0, 0u, dst, 4u, &err) ==
                       STDLIB_FAIL);
    FZ_REQUIRE(s, lifetime_write(&b, 0u, (const void *)0, 4u, &err) ==
                       STDLIB_FAIL);
    FZ_REQUIRE(s, lifetime_borrow_is_live((lifetime_borrow *)0) == 0);
    FZ_REQUIRE(s, err.failed_now());
    return s->fail == 0;
}

static int cs_life_uaf_meta(fz_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res, int *ref) {
    lifetime_owner o;
    error err = error();
    void *saved_ptr;
    unsigned long long saved_gen;
    (void)p1;
    (void)p2;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, lifetime_owner_init(&o, 32u, &err) == STDLIB_OK);
    saved_ptr = o.ptr;
    saved_gen = o.token;
    FZ_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    FZ_REQUIRE(s, stdlib_alloc_size(saved_ptr) == 0u);
    FZ_REQUIRE(s, stdlib_alloc_generation(saved_ptr) == 0ULL);
    FZ_REQUIRE(s, stdlib_alloc_view_live(saved_ptr) == 0);
    FZ_REQUIRE(s, stdlib_alloc_cap_state(saved_ptr) == STDLIB_ALLOC_CAP_NONE);
    FZ_REQUIRE(s, stdlib_alloc_hasvalidcap(saved_ptr) == 0);
    FZ_REQUIRE(s, stdlib_alloc_cap_covers(saved_ptr, 0u, 4u) == 0);
    FZ_REQUIRE(s, stdlib_alloc_cap_check(saved_ptr, saved_gen, 0u, 4u) == 0);
    FZ_REQUIRE(s, stdlib_alloc_id_matches((stdlib_alloc_id *)0, saved_ptr) == 0);
    return s->fail == 0;
}

static int cs_life_bounds(fz_state *s, unsigned long long p1,
                          unsigned long long p2, const char **res, int *ref) {
    lifetime_owner o;
    lifetime_borrow b;
    error err = error();
    unsigned char src[8];
    unsigned char dst[8];
    size_t off;
    size_t n;
    int i;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 8; i++) {
        src[i] = (unsigned char)(p1 + (unsigned long long)i);
        dst[i] = 0xAAu;
    }
    off = (size_t)(14u + (p2 % 3ULL)); /* 14..16, cap == 16 */
    n = 4u;                            /* off+n always exceeds 16 */
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, lifetime_owner_init(&o, 16u, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_borrow_from(&b, &o, &err) == STDLIB_OK);
    FZ_REQUIRE(s, lifetime_read(&b, off, dst, n, &err) == STDLIB_FAIL);
    FZ_REQUIRE(s, err.category() == STDLIB_CATEGORY_BOUNDS &&
                       err.code() == CPLUS_EXIT_BOUNDS);
    FZ_REQUIRE(s, lifetime_write(&b, off, src, n, &err) == STDLIB_FAIL);
    for (i = 0; i < 8; i++)
        FZ_REQUIRE(s, dst[i] == 0xAAu);
    FZ_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    return s->fail == 0;
}

/* ======================================================================= */
/* alloc generation / liveness helper family                                 */
/* ======================================================================= */

static int cs_alloc_id_live(fz_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res, int *ref) {
    size_t n = (size_t)(16u + (p2 % 64u));
    void *p = stdlib_alloc(n);
    stdlib_alloc_id id;
    stdlib_alloc_id id2;
    (void)p1;
    *res = "defined";
    *ref = 0;
    FZ_REQUIRE(s, p != (void *)0);
    FZ_REQUIRE(s, stdlib_alloc_identity(p, &id) == 0 && id.base == p &&
                       id.generation != 0ULL);
    FZ_REQUIRE(s, stdlib_alloc_id_matches(&id, p) == 1);
    FZ_REQUIRE(s, stdlib_alloc_cap_check(p, id.generation, 0u, n) == 1);
    FZ_REQUIRE(s, stdlib_alloc_view_live(p) == 1);
    FZ_REQUIRE(s, stdlib_alloc_view_remaining(p, n / 2u) == n - (n / 2u));
    FZ_REQUIRE(s, stdlib_alloc_identity(p, &id2) == 0 &&
                       stdlib_alloc_id_equal(&id, &id2) == 1);
    stdlib_release(p);
    return s->fail == 0;
}

static int cs_alloc_stale_generation(fz_state *s, unsigned long long p1,
                                     unsigned long long p2, const char **res,
                                     int *ref) {
    void *p = stdlib_alloc(16u);
    stdlib_alloc_id id;
    unsigned long long gen;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, p != (void *)0);
    FZ_REQUIRE(s, stdlib_alloc_identity(p, &id) == 0);
    gen = id.generation;
    stdlib_release(p);
    FZ_REQUIRE(s, stdlib_alloc_view_generation(p) == 0ULL);
    FZ_REQUIRE(s, stdlib_alloc_id_matches(&id, p) == 0);
    FZ_REQUIRE(s, stdlib_alloc_view_unchanged(p, gen) == 0);
    FZ_REQUIRE(s, stdlib_alloc_cap_check(p, gen, 0u, 1u) == 0);
    FZ_REQUIRE(s, stdlib_alloc_id_equal(&id, &id) == 1);
    return s->fail == 0;
}

static int cs_alloc_cap_bounds(fz_state *s, unsigned long long p1,
                               unsigned long long p2, const char **res, int *ref) {
    void *p = stdlib_alloc(16u);
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, p != (void *)0);
    FZ_REQUIRE(s, stdlib_alloc_cap_covers(p, 0u, 16u) == 1);
    FZ_REQUIRE(s, stdlib_alloc_cap_covers(p, 14u, 4u) == 0);
    FZ_REQUIRE(s, stdlib_alloc_cap_covers(p, 17u, 0u) == 0);
    FZ_REQUIRE(s, stdlib_alloc_cap_check(p, stdlib_alloc_generation(p), 0u,
                                         16u) == 1);
    stdlib_release(p);
    return s->fail == 0;
}

static int cs_alloc_null_interior(fz_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    void *p = stdlib_alloc(32u);
    stdlib_alloc_id id;
    void *dst = (void *)0;
    void *src;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    FZ_REQUIRE(s, p != (void *)0);
    FZ_REQUIRE(s, stdlib_alloc_view_bytes((void *)0) == 0u);
    FZ_REQUIRE(s, stdlib_alloc_identity((void *)0, &id) == -1 &&
                       id.base == (const void *)0 && id.generation == 0ULL);
    FZ_REQUIRE(s, stdlib_alloc_generation((unsigned char *)p + 1) == 0ULL);
    FZ_REQUIRE(s, stdlib_alloc_view_live((unsigned char *)p + 1) == 0);
    FZ_REQUIRE(s, stdlib_alloc_cap_state((unsigned char *)p + 1) ==
                       STDLIB_ALLOC_CAP_NONE);
    src = p;
    FZ_REQUIRE(s, stdlib_alloc_view_transfer_ok(&dst, &src) == 1);
    stdlib_release(p);
    FZ_REQUIRE(s, stdlib_alloc_view_transfer_ok(&dst, &src) == 0);
    return s->fail == 0;
}

static int cs_alloc_transfer_view(fz_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    void *p = stdlib_alloc(24u);
    void *a = (void *)0;
    void *b = (void *)0;
    unsigned long long g0;
    unsigned long long g1;
    int rc;
    (void)p1;
    (void)p2;
    *res = "defined";
    *ref = 0;
    FZ_REQUIRE(s, p != (void *)0);
    g0 = stdlib_alloc_generation(p);
    b = p;
    rc = stdlib_transfer_ownership(&a, &b);
    FZ_REQUIRE(s, rc == 0 && a == p && b == (void *)0);
    g1 = stdlib_alloc_generation(a);
    FZ_REQUIRE(s, g1 != 0ULL && g1 != g0);
    FZ_REQUIRE(s, stdlib_alloc_check_generation(p, g0) == 0);
    FZ_REQUIRE(s, stdlib_alloc_view_live(p) == 1);
    FZ_REQUIRE(s, stdlib_transfer_ownership(&b, &a) == 0);
    FZ_REQUIRE(s, a == (void *)0 && b == p);
    stdlib_release(p);
    return s->fail == 0;
}

/* ======================================================================= */
/* case table + driver                                                       */
/* ======================================================================= */

typedef struct fz_case_def {
    const char *api;
    const char *cname;
    fz_case_fn fn;
} fz_case_def;

static fz_case_def FZ_CASES[] = {
    /* mutex (7) */
    {"mutex", "cycle", cs_mutex_cycle},
    {"mutex", "double_unlock", cs_mutex_double_unlock},
    {"mutex", "double_lock", cs_mutex_double_lock},
    {"mutex", "double_drop", cs_mutex_double_drop},
    {"mutex", "zero_handle", cs_mutex_zero_handle},
    {"mutex", "null_guard", cs_mutex_null_guard},
    {"mutex", "guard_drop", cs_mutex_guard_drop},
    /* condvar (5) */
    {"condvar", "signal_broadcast", cs_cv_signal_broadcast},
    {"condvar", "wait_invalid", cs_cv_wait_invalid},
    {"condvar", "zero_handle", cs_cv_zero_handle},
    {"condvar", "double_drop", cs_cv_double_drop},
    {"condvar", "after_drop", cs_cv_after_drop},
    /* thread (6) */
    {"thread", "start_join", cs_thread_start_join},
    {"thread", "double_start", cs_thread_double_start},
    {"thread", "failed_join", cs_thread_failed_join},
    {"thread", "null_fn", cs_thread_null_fn},
    {"thread", "drop_joins", cs_thread_drop_joins},
    {"thread", "multi", cs_thread_multi},
    /* future (4) */
    {"future", "get_once", cs_future_get_once},
    {"future", "double_get", cs_future_double_get},
    {"future", "before_start", cs_future_before_start},
    {"future", "consumed_slot", cs_future_consumed_slot},
    /* timer (5) */
    {"timer", "lifecycle", cs_timer_lifecycle},
    {"timer", "null_loop", cs_timer_null_loop},
    {"timer", "null_cb", cs_timer_null_cb},
    {"timer", "stop_invalid", cs_timer_stop_invalid},
    {"timer", "double_drop", cs_timer_double_drop},
    /* lifetime (7) */
    {"lifetime", "valid_rw", cs_life_valid_rw},
    {"lifetime", "stale_token", cs_life_stale_token},
    {"lifetime", "double_release", cs_life_double_release},
    {"lifetime", "transfer", cs_life_transfer},
    {"lifetime", "null_handles", cs_life_null_handles},
    {"lifetime", "uaf_meta", cs_life_uaf_meta},
    {"lifetime", "bounds", cs_life_bounds},
    /* alloc (5) */
    {"alloc", "id_live", cs_alloc_id_live},
    {"alloc", "stale_generation", cs_alloc_stale_generation},
    {"alloc", "cap_bounds", cs_alloc_cap_bounds},
    {"alloc", "null_interior", cs_alloc_null_interior},
    {"alloc", "transfer_view", cs_alloc_transfer_view},
};

static int fz_tuple_cmp(const void *va, const void *vb) {
    const fz_tuple *a = (const fz_tuple *)va;
    const fz_tuple *b = (const fz_tuple *)vb;
    if (a->case_index != b->case_index)
        return a->case_index < b->case_index ? -1 : 1;
    if (a->p1 != b->p1)
        return a->p1 < b->p1 ? -1 : 1;
    if (a->p2 != b->p2)
        return a->p2 < b->p2 ? -1 : 1;
    return 0;
}

int main(int argc, char **argv) {
    unsigned long long rng = FZ_SEED;
    unsigned long long rsel;
    unsigned long long p1;
    unsigned long long p2;
    unsigned long long dup = 0;
    unsigned long long i;
    int ncases = (int)(sizeof(FZ_CASES) / sizeof(FZ_CASES[0]));
    int ci;
    int outcome;
    int sok;
    int refused;
    const char *result;
    char hex[32];
    mutex ec_m = mutex();
    const char *log_path = "tests/fuzz10k/thread_async/scenarios.log";

    if (argc > 1)
        log_path = argv[1];
    g_log = fopen(log_path, "w");
    if (g_log == (FILE *)0) {
        fprintf(stderr, "FUZZ10K: cannot open log %s\n", log_path);
        return 2;
    }

    signal(SIGALRM, fz_on_alarm);

    /* Configure and self-check the ERRORCHECK mutex for the double-lock case. */
    g_ec_ptr = &ec_m;
    fz_to_errorcheck(&ec_m);
    {
        int a = pthread_mutex_lock((pthread_mutex_t *)ec_m.native());
        int b = pthread_mutex_lock((pthread_mutex_t *)ec_m.native());
        pthread_mutex_unlock((pthread_mutex_t *)ec_m.native());
        if (a == 0 && b == EDEADLK)
            g_ec_ok = 1;
    }
    if (!g_ec_ok) {
        fprintf(stderr, "FUZZ10K: ERRORCHECK mutex self-check failed\n");
        return 2;
    }

    fprintf(g_log, "# FUZZ10K thread_async seed=%016llx scenarios=%d\n",
            FZ_SEED, FZ_N);
    fprintf(g_log, "# format: S <id> <api> <case> <result> <state_ok>\n");

    for (i = 1; i <= (unsigned long long)FZ_N; i++) {
        fz_state st;
        fz_case_def *d;

        rsel = fz_rnd(&rng);
        p1 = fz_rnd(&rng);
        p2 = fz_rnd(&rng);
        ci = (int)(rsel % (unsigned long long)ncases);
        d = &FZ_CASES[ci];

        g_tuple[(size_t)(i - 1)].case_index = ci;
        g_tuple[(size_t)(i - 1)].p1 = p1;
        g_tuple[(size_t)(i - 1)].p2 = p2;

        alarm(FZ_WATCHDOG_SECS);
        fz_state_init(&st);
        result = "FAIL";
        refused = 0;
        outcome = d->fn(&st, p1, p2, &result, &refused);
        alarm(0);
        sok = (fz_state_ok(&st) && outcome) ? 1 : 0;

        if (!sok)
            g_failures++;
        if (sok && refused)
            g_refusals++;
        if (sok)
            g_state_ok++;

        g_digest = fz_mix(g_digest, i);
        g_digest = fz_mix(g_digest, (unsigned long long)ci);
        g_digest = fz_mix(g_digest, p1);
        g_digest = fz_mix(g_digest, p2);
        g_digest = fz_mix(g_digest, fz_hash_str(result));
        g_digest = fz_mix(g_digest, (unsigned long long)refused);
        g_digest = fz_mix(g_digest, (unsigned long long)sok);

        fprintf(g_log, "S %llu %s %s %s %d\n", i, d->api, d->cname, result,
                sok);
    }

    /* Deterministic distinctness of (case, p1, p2) tuples via sort + adjacency. */
    qsort(g_tuple, (size_t)FZ_N, sizeof(g_tuple[0]), fz_tuple_cmp);
    for (i = 1; i < (unsigned long long)FZ_N; i++) {
        if (fz_tuple_cmp(&g_tuple[(size_t)(i - 1)], &g_tuple[(size_t)i]) == 0)
            dup++;
    }
    if (dup != 0ULL)
        g_failures++;

    ec_m.drop();

    snprintf(hex, sizeof(hex), "%016llx", g_digest);
    fprintf(g_log, "FUZZ10K_DISTINCT %llu\n", dup);
    fprintf(g_log, "FUZZ10K_DIGEST %s\n", hex);
    fprintf(g_log,
            "FUZZ10K thread_async %d 0 0 %llu %llu\n", FZ_N, g_failures,
            g_refusals);
    fclose(g_log);

    printf("FUZZ10K_DISTINCT %llu\n", dup);
    printf("FUZZ10K_DIGEST %s\n", hex);
    printf("FUZZ10K thread_async %d 0 0 %llu %llu\n", FZ_N, g_failures,
           g_refusals);
    return (g_failures != 0ULL || g_state_ok != (unsigned long long)FZ_N) ? 1 : 0;
}
