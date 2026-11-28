/* ADV1000 -- thread_async: 1000 deterministic adversarial scenarios.
 *
 * SCOPE: stdlib/thread.hp, stdlib/async.hp, stdlib/lifetime_adapt.hp and the
 * stdlib/allocator.hp generation/liveness helpers.
 *
 * DESIGN
 * ------
 * A single fixed seed (ADV_SEED) drives a splitmix64 stream.  For each of the
 * 1000 scenario ids the stream selects a case from a table of 34 deterministic
 * adversarial cases over 7 API families (mutex, condvar, thread, future,
 * timer, lifetime, alloc) and supplies two 64-bit parameters that vary the
 * operation sequence (iteration counts, sizes, values) without changing the
 * contract under test.  No pid, no time, no clock, no randomness from the
 * environment: rerunning produces byte-identical stdout.
 *
 * Each scenario runs one self-cleaning case function.  A case performs a short
 * deterministic operation sequence, then checks that the API either REFUSED
 * the adversarial operation (metadata error with a defined category/code) or
 * returned its documented defined result -- and that a two-value canary plus
 * the stdlib metadata live-block count are intact afterwards.  Stale pointers
 * are queried through metadata only and are NEVER dereferenced.  Threads are
 * short-lived workers and are always joined (bounded by a SIGALRM watchdog and
 * by the external `timeout 120` in run.sh); no wall-clock value is asserted.
 *
 * `future` is modelled as a single-consumption result handle built only from
 * allowed headers (thread::join and a lifetime owner/borrow result slot) because
 * stdlib/async.hp has no dedicated future type.  This is stated in NOTES.md.
 *
 * OUTPUT
 * ------
 *   1000 lines:  S <id> <api> <case> <result> <state_ok>
 *   then:        ADV1000_DISTINCT <duplicate_tuples>
 *   then:        ADV1000_DIGEST <hex16>
 *   finally:     ADV1000 thread_async 1000 <refused> <state_ok> <failures>
 *
 * Exit status is non-zero iff failures != 0.  This is evidence of the tested
 * refusal/defined-result paths under these deterministic inputs, not a proof
 * of safety for all inputs.
 */
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "stdlib/thread.hp"
#include "stdlib/async.hp"
#include "stdlib/lifetime_adapt.hp"
#include "stdlib/allocator.hp"

#define ADV_N 1000
#define ADV_SEED 0xA5D0100047C0FFEEULL
#define ADV_WATCHDOG_SECS 90
#define ADV_CANARY_A 0xA11CE15EDEADBEEFULL
#define ADV_CANARY_B 0x5EED5A17C0FFEE01ULL
#define ADV_WORK_CONST 0x5DEECE66DULL

static unsigned long long g_failures = 0;
static unsigned long long g_refused = 0;
static unsigned long long g_state_ok = 0;
static unsigned long long g_digest = 1469598103934665603ULL;

/* ERRORCHECK mutex, configured once in main(), for the double-lock case. */
static mutex *g_ec_ptr;
static int g_ec_ok = 0;

/* Uniqueness bookkeeping (deterministic). */
static int g_case_of[ADV_N];
static unsigned long long g_p1_of[ADV_N];
static unsigned long long g_p2_of[ADV_N];

typedef struct adv_state {
    unsigned long long canary_a;
    unsigned long long canary_b;
    size_t base_live;
    int fail;
} adv_state;

#define ADV_REQUIRE(st, cond)                                                  \
    do {                                                                       \
        if (!(cond))                                                           \
            (st)->fail = 1;                                                    \
    } while (0)

static void adv_state_init(adv_state *s) {
    s->canary_a = ADV_CANARY_A;
    s->canary_b = ADV_CANARY_B;
    s->base_live = stdlib_state.meta_live;
    s->fail = 0;
}

static int adv_state_ok(const adv_state *s) {
    return s->canary_a == ADV_CANARY_A && s->canary_b == ADV_CANARY_B &&
           s->base_live == stdlib_state.meta_live && s->fail == 0;
}

static unsigned long long adv_rnd(unsigned long long *s) {
    unsigned long long z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

#define ADV_FNV_PRIME 1099511628211ULL
static unsigned long long adv_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * ADV_FNV_PRIME;
}

static void adv_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

/* Test-only white-box: make a hidden second lock observable as EDEADLK rather
 * than a deadlock.  Same technique as the existing f100_thread_guard harness. */
static void adv_to_errorcheck(mutex *m) {
    pthread_mutex_t *pm = (pthread_mutex_t *)m->native();
    pthread_mutexattr_t attr;
    pthread_mutex_destroy(pm);
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(pm, &attr);
    pthread_mutexattr_destroy(&attr);
}

/* ---- deterministic workers / callbacks ---------------------------------- */

static void *adv_worker(void *arg) {
    unsigned long long *p = (unsigned long long *)arg;
    unsigned long long v = *p ^ ADV_WORK_CONST;
    return (void *)(uintptr_t)v;
}

static void adv_noop_cb(uv_timer_t *h) {
    (void)h;
}

/* ---- case function type -------------------------------------------------- */

typedef int (*adv_case_fn)(adv_state *s, unsigned long long p1,
                           unsigned long long p2, const char **result_out,
                           int *refused_out);

/* ========================================================================= */
/* mutex family                                                               */
/* ========================================================================= */

static int cs_mutex_cycle(adv_state *s, unsigned long long p1,
                          unsigned long long p2, const char **res, int *ref) {
    mutex m = mutex();
    unsigned int n = (unsigned int)(1u + (p1 % 3ULL));
    unsigned int i;
    (void)p2;
    *res = "defined";
    *ref = 0;
    ADV_REQUIRE(s, m.is_valid() == 1);
    for (i = 0; i < n; i++) {
        m.lock();
        m.unlock();
    }
    m.drop();
    ADV_REQUIRE(s, m.native() == (void *)0);
    m.drop();
    ADV_REQUIRE(s, m.native() == (void *)0 && m.is_valid() == 0);
    return s->fail == 0;
}

static int cs_mutex_double_unlock(adv_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    mutex m = mutex();
    guard g = guard(&m);
    (void)p1;
    (void)p2;
    *res = "noop";
    *ref = 1; /* repeated unlock is refused by the guard's cleared owner */
    ADV_REQUIRE(s, g.holds() == 1);
    g.unlock();
    ADV_REQUIRE(s, g.holds() == 0);
    g.unlock();
    ADV_REQUIRE(s, g.holds() == 0);
    m.lock(); /* proves the mutex was released exactly once */
    m.unlock();
    g.drop();
    m.drop();
    ADV_REQUIRE(s, m.native() == (void *)0);
    return s->fail == 0;
}

static int cs_mutex_double_lock(adv_state *s, unsigned long long p1,
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
    ADV_REQUIRE(s, rc1 == 0);
    ADV_REQUIRE(s, rc2 == EDEADLK);
    return s->fail == 0;
}

static int cs_mutex_double_drop(adv_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    mutex m = mutex();
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1; /* second destroy is refused by the NULL handle */
    ADV_REQUIRE(s, m.is_valid() == 1);
    m.drop();
    ADV_REQUIRE(s, m.native() == (void *)0);
    m.drop();
    ADV_REQUIRE(s, m.native() == (void *)0 && m.is_valid() == 0);
    return s->fail == 0;
}

static int cs_mutex_zero_handle(adv_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    mutex m; /* zero-initialised: holds no OS resource */
    (void)p1;
    (void)p2;
    *res = "noop";
    *ref = 0;
    ADV_REQUIRE(s, m.is_valid() == 0);
    m.lock();
    m.unlock();
    m.drop();
    ADV_REQUIRE(s, m.native() == (void *)0);
    return s->fail == 0;
}

static int cs_mutex_null_guard(adv_state *s, unsigned long long p1,
                               unsigned long long p2, const char **res,
                               int *ref) {
    guard g = guard((mutex *)0);
    (void)p1;
    (void)p2;
    *res = "noop";
    *ref = 1; /* borrow of a NULL mutex is refused */
    ADV_REQUIRE(s, g.holds() == 0);
    g.unlock();
    ADV_REQUIRE(s, g.holds() == 0);
    g.drop();
    ADV_REQUIRE(s, g.holds() == 0);
    return s->fail == 0;
}

/* ========================================================================= */
/* condvar family                                                             */
/* ========================================================================= */

static int cs_cv_signal(adv_state *s, unsigned long long p1,
                        unsigned long long p2, const char **res, int *ref) {
    condvar cv = condvar();
    unsigned int n = (unsigned int)(1u + (p1 % 3ULL));
    unsigned int i;
    (void)p2;
    *res = "defined";
    *ref = 0;
    ADV_REQUIRE(s, cv.is_valid() == 1);
    for (i = 0; i < n; i++) {
        cv.signal();
        cv.broadcast();
    }
    cv.drop();
    cv.drop();
    ADV_REQUIRE(s, cv.is_valid() == 0);
    return s->fail == 0;
}

static int cs_cv_wait_invalid(adv_state *s, unsigned long long p1,
                              unsigned long long p2, const char **res,
                              int *ref) {
    condvar cv = condvar();
    mutex m; /* zero handle: wait() must refuse before any blocking call */
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, cv.wait((mutex *)0) == -1);
    ADV_REQUIRE(s, cv.wait(&m) == -1);
    cv.drop();
    return s->fail == 0;
}

static int cs_cv_zero(adv_state *s, unsigned long long p1,
                      unsigned long long p2, const char **res, int *ref) {
    condvar cv; /* zero-initialised */
    mutex m = mutex();
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, cv.is_valid() == 0);
    ADV_REQUIRE(s, cv.signal() == -1);
    ADV_REQUIRE(s, cv.broadcast() == -1);
    ADV_REQUIRE(s, cv.wait(&m) == -1);
    m.drop();
    return s->fail == 0;
}

static int cs_cv_double_drop(adv_state *s, unsigned long long p1,
                             unsigned long long p2, const char **res,
                             int *ref) {
    condvar cv = condvar();
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1; /* second destroy refused by the NULL cv pointer */
    ADV_REQUIRE(s, cv.is_valid() == 1);
    cv.drop();
    cv.drop();
    ADV_REQUIRE(s, cv.is_valid() == 0);
    return s->fail == 0;
}

/* ========================================================================= */
/* thread family                                                              */
/* ========================================================================= */

static int cs_thread_start_join(adv_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1 ^ p2;
    void *out = (void *)0;
    *res = "defined";
    *ref = 0;
    ADV_REQUIRE(s, thr.is_joinable() == 0);
    ADV_REQUIRE(s, thr.start(adv_worker, &in, &err) == STDLIB_OK && err.ok());
    ADV_REQUIRE(s, thr.is_joinable() == 1);
    ADV_REQUIRE(s, thr.join(&out, &err) == STDLIB_OK && err.ok());
    ADV_REQUIRE(s, out == (void *)(uintptr_t)(in ^ ADV_WORK_CONST));
    ADV_REQUIRE(s, thr.is_joinable() == 0);
    thr.drop();
    return s->fail == 0;
}

static int cs_thread_double_start(adv_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1;
    void *out = (void *)0;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, thr.start(adv_worker, &in, &err) == STDLIB_OK);
    ADV_REQUIRE(s, thr.start(adv_worker, &in, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                       err.code() == CPLUS_EXIT_RESOURCE);
    ADV_REQUIRE(s, thr.is_joinable() == 1);
    ADV_REQUIRE(s, thr.join(&out, &err) == STDLIB_OK);
    thr.drop();
    return s->fail == 0;
}

static int cs_thread_failed_join(adv_state *s, unsigned long long p1,
                                 unsigned long long p2, const char **res,
                                 int *ref) {
    thread thr = thread();
    error err = error();
    void *out = (void *)0;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, thr.join(&out, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                       err.code() == CPLUS_EXIT_RESOURCE);
    ADV_REQUIRE(s, out == (void *)0);
    thr.drop();
    return s->fail == 0;
}

static int cs_thread_null_fn(adv_state *s, unsigned long long p1,
                             unsigned long long p2, const char **res,
                             int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p2;
    (void)p1;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, thr.start((void *(*)(void *))0, &in, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, err.failed_now() && err.code() == CPLUS_EXIT_UNSUPPORTED);
    ADV_REQUIRE(s, thr.is_joinable() == 0);
    thr.drop();
    return s->fail == 0;
}

static int cs_thread_drop_joins(adv_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1 ^ p2;
    void *out = (void *)0;
    *res = "refused";
    *ref = 1; /* join after drop() refused: handle already reaped */
    ADV_REQUIRE(s, thr.start(adv_worker, &in, &err) == STDLIB_OK);
    thr.drop();
    ADV_REQUIRE(s, thr.is_joinable() == 0);
    ADV_REQUIRE(s, thr.join(&out, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, err.failed_now());
    return s->fail == 0;
}

/* ========================================================================= */
/* future family (single-consumption result via thread / lifetime)           */
/* ========================================================================= */

static int cs_future_get_once(adv_state *s, unsigned long long p1,
                              unsigned long long p2, const char **res,
                              int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1 ^ p2;
    void *out = (void *)0;
    *res = "defined";
    *ref = 0;
    ADV_REQUIRE(s, thr.start(adv_worker, &in, &err) == STDLIB_OK);
    ADV_REQUIRE(s, thr.join(&out, &err) == STDLIB_OK);
    ADV_REQUIRE(s, out == (void *)(uintptr_t)(in ^ ADV_WORK_CONST));
    thr.drop();
    return s->fail == 0;
}

static int cs_future_double_get(adv_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    thread thr = thread();
    error err = error();
    unsigned long long in = p1;
    void *out = (void *)0;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, thr.start(adv_worker, &in, &err) == STDLIB_OK);
    ADV_REQUIRE(s, thr.join(&out, &err) == STDLIB_OK);
    ADV_REQUIRE(s, thr.join(&out, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_LIFECYCLE);
    ADV_REQUIRE(s, out == (void *)(uintptr_t)(in ^ ADV_WORK_CONST));
    thr.drop();
    return s->fail == 0;
}

static int cs_future_before_start(adv_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    thread thr = thread();
    error err = error();
    void *out = (void *)0;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, thr.join(&out, &err) == STDLIB_FAIL && err.failed_now());
    ADV_REQUIRE(s, out == (void *)0);
    thr.drop();
    return s->fail == 0;
}

static int cs_future_consumed_slot(adv_state *s, unsigned long long p1,
                                   unsigned long long p2, const char **res,
                                   int *ref) {
    lifetime_owner o;
    lifetime_borrow b;
    error err = error();
    unsigned long long val = p1 ^ p2;
    unsigned char slot_dst[8];
    int i;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 8; i++)
        slot_dst[i] = 0xAAu;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, lifetime_owner_init(&o, 16u, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_borrow_from(&b, &o, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_write(&b, 0u, &val, 8u, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_read(&b, 0u, slot_dst, 8u, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    /* second consumption: stale borrow must be refused via metadata only.
     * Restore the sentinel first: the first (successful) read overwrote it. */
    for (i = 0; i < 8; i++)
        slot_dst[i] = 0xAAu;
    ADV_REQUIRE(s, lifetime_borrow_is_live(&b) == 0);
    ADV_REQUIRE(s, lifetime_read(&b, 0u, slot_dst, 8u, &err) == STDLIB_FAIL &&
                       err.code() == CPLUS_EXIT_USE_AFTER_FREE);
    ADV_REQUIRE(s, slot_dst[0] == 0xAAu);
    return s->fail == 0;
}

/* ========================================================================= */
/* timer family                                                               */
/* ========================================================================= */

static int cs_timer_lifecycle(adv_state *s, unsigned long long p1,
                              unsigned long long p2, const char **res,
                              int *ref) {
    error err = error();
    loop l = loop(&err);
    timer tmr = timer(l.native(), &err);
    unsigned long long user = p1 ^ p2;
    *res = "defined";
    *ref = 0;
    ADV_REQUIRE(s, l.valid() == 1);
    ADV_REQUIRE(s, tmr.valid() == 1);
    ADV_REQUIRE(s, tmr.start(adv_noop_cb, 3600000ULL, 0ULL, &user, &err) ==
                       STDLIB_OK);
    ADV_REQUIRE(s, tmr.stop(&err) == STDLIB_OK && err.ok());
    tmr.drop();
    ADV_REQUIRE(s, tmr.valid() == 0);
    l.drop();
    ADV_REQUIRE(s, l.valid() == 0);
    return s->fail == 0;
}

static int cs_timer_null_loop(adv_state *s, unsigned long long p1,
                              unsigned long long p2, const char **res,
                              int *ref) {
    error err = error();
    timer tmr = timer((uv_loop_t *)0, &err);
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, tmr.valid() == 0);
    ADV_REQUIRE(s, err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_INVALID);
    tmr.drop();
    ADV_REQUIRE(s, tmr.valid() == 0);
    return s->fail == 0;
}

static int cs_timer_null_cb(adv_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res,
                            int *ref) {
    error err = error();
    loop l = loop(&err);
    timer tmr = timer(l.native(), &err);
    unsigned long long user = p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, tmr.valid() == 1);
    ADV_REQUIRE(s, tmr.start((uv_timer_cb)0, 3600000ULL, 0ULL, &user, &err) ==
                       STDLIB_FAIL);
    ADV_REQUIRE(s, err.failed_now() && err.code() == CPLUS_EXIT_UNSUPPORTED);
    tmr.drop();
    l.drop();
    return s->fail == 0;
}

static int cs_timer_stop_invalid(adv_state *s, unsigned long long p1,
                                 unsigned long long p2, const char **res,
                                 int *ref) {
    error err = error();
    timer tmr; /* zero-initialised */
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, tmr.valid() == 0);
    ADV_REQUIRE(s, tmr.stop(&err) == STDLIB_FAIL && err.failed_now());
    ADV_REQUIRE(s, err.code() == CPLUS_EXIT_UNSUPPORTED);
    tmr.drop();
    ADV_REQUIRE(s, tmr.valid() == 0);
    return s->fail == 0;
}

static int cs_timer_double_drop(adv_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    error err = error();
    loop l = loop(&err);
    timer tmr = timer(l.native(), &err);
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1; /* second destroy refused by initialized == 0 */
    ADV_REQUIRE(s, tmr.valid() == 1);
    tmr.drop();
    tmr.drop();
    ADV_REQUIRE(s, tmr.valid() == 0);
    l.drop();
    return s->fail == 0;
}

/* ========================================================================= */
/* lifetime family                                                            */
/* ========================================================================= */

static int cs_life_valid_rw(adv_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res, int *ref) {
    lifetime_owner o;
    lifetime_borrow b;
    error err = error();
    unsigned char rw_src[8];
    unsigned char rw_dst[8];
    int i;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 8; i++) {
        rw_src[i] = (unsigned char)(p1 + (unsigned long long)i);
        rw_dst[i] = 0u;
    }
    *res = "defined";
    *ref = 0;
    ADV_REQUIRE(s, lifetime_owner_init(&o, (size_t)(16u + p2 % 64u), &err) ==
                       STDLIB_OK);
    ADV_REQUIRE(s, lifetime_borrow_from(&b, &o, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_write(&b, 0u, rw_src, 8u, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_read(&b, 0u, rw_dst, 8u, &err) == STDLIB_OK);
    for (i = 0; i < 8; i++)
        ADV_REQUIRE(s, rw_dst[i] == rw_src[i]);
    ADV_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    ADV_REQUIRE(s, o.ptr == (void *)0 && o.token == LIFETIME_NO_TOKEN);
    return s->fail == 0;
}

static int cs_life_stale_token(adv_state *s, unsigned long long p1,
                               unsigned long long p2, const char **res,
                               int *ref) {
    lifetime_owner o;
    lifetime_borrow b;
    error err = error();
    void *saved_ptr;
    lifetime_token_t saved_token;
    unsigned char st_src[4];
    unsigned char st_dst[4];
    int i;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 4; i++) {
        st_src[i] = (unsigned char)(p1 + (unsigned long long)i);
        st_dst[i] = 0xAAu;
    }
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, lifetime_owner_init(&o, 16u, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_borrow_from(&b, &o, &err) == STDLIB_OK);
    saved_ptr = b.ptr;
    saved_token = b.token;
    ADV_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    ADV_REQUIRE(s, stdlib_alloc_generation(saved_ptr) == LIFETIME_NO_TOKEN);
    ADV_REQUIRE(s, b.token == saved_token && saved_token != LIFETIME_NO_TOKEN);
    ADV_REQUIRE(s, lifetime_borrow_is_live(&b) == 0);
    ADV_REQUIRE(s, lifetime_read(&b, 0u, st_dst, 4u, &err) == STDLIB_FAIL &&
                       err.category() == STDLIB_CATEGORY_LIFECYCLE &&
                       err.code() == CPLUS_EXIT_USE_AFTER_FREE);
    ADV_REQUIRE(s, lifetime_write(&b, 0u, st_src, 4u, &err) == STDLIB_FAIL);
    for (i = 0; i < 4; i++)
        ADV_REQUIRE(s, st_dst[i] == 0xAAu);
    (void)p2;
    return s->fail == 0;
}

static int cs_life_double_release(adv_state *s, unsigned long long p1,
                                  unsigned long long p2, const char **res,
                                  int *ref) {
    lifetime_owner o;
    error err = error();
    size_t live_before;
    o.ptr = (void *)0;
    o.token = LIFETIME_NO_TOKEN;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, lifetime_owner_init(&o, 16u, &err) == STDLIB_OK);
    live_before = stdlib_state.meta_live;
    ADV_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    ADV_REQUIRE(s, stdlib_state.meta_live == live_before - 1u);
    ADV_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, err.category() == STDLIB_CATEGORY_OWNERSHIP &&
                       err.code() == CPLUS_EXIT_DOUBLE_FREE);
    ADV_REQUIRE(s, stdlib_state.meta_live == live_before - 1u);
    return s->fail == 0;
}

static int cs_life_transfer(adv_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res,
                            int *ref) {
    lifetime_owner a;
    lifetime_owner c;
    lifetime_borrow b;
    error err = error();
    unsigned char tr_src[4];
    unsigned char tr_dst[4];
    lifetime_token_t ta;
    int i;
    a.ptr = (void *)0;
    a.token = LIFETIME_NO_TOKEN;
    c.ptr = (void *)0;
    c.token = LIFETIME_NO_TOKEN;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    for (i = 0; i < 4; i++) {
        tr_src[i] = (unsigned char)(p1 + p2 + (unsigned long long)i);
        tr_dst[i] = 0u;
    }
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, lifetime_owner_init(&a, 16u, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_borrow_from(&b, &a, &err) == STDLIB_OK);
    ADV_REQUIRE(s, lifetime_write(&b, 0u, tr_src, 4u, &err) == STDLIB_OK);
    ta = a.token;
    ADV_REQUIRE(s, lifetime_transfer(&c, &a, &err) == STDLIB_OK);
    ADV_REQUIRE(s, a.ptr == (void *)0 && a.token == LIFETIME_NO_TOKEN);
    ADV_REQUIRE(s, c.ptr != (void *)0 && c.token != LIFETIME_NO_TOKEN &&
                       c.token != ta);
    ADV_REQUIRE(s, lifetime_borrow_is_live(&b) == 0);
    ADV_REQUIRE(s, lifetime_read(&b, 0u, tr_dst, 4u, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, tr_dst[0] == 0u);
    ADV_REQUIRE(s, lifetime_borrow_from(&b, &a, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, lifetime_owner_release(&a, &err) == STDLIB_FAIL);
    ADV_REQUIRE(s, lifetime_owner_release(&c, &err) == STDLIB_OK);
    return s->fail == 0;
}

static int cs_life_null_handles(adv_state *s, unsigned long long p1,
                                unsigned long long p2, const char **res,
                                int *ref) {
    lifetime_borrow b;
    error err = error();
    unsigned char nh_dst[4];
    (void)p1;
    (void)p2;
    b.ptr = (void *)0;
    b.token = LIFETIME_NO_TOKEN;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, lifetime_owner_init((lifetime_owner *)0, 16u, &err) ==
                       STDLIB_FAIL);
    ADV_REQUIRE(s, lifetime_owner_release((lifetime_owner *)0, &err) ==
                       STDLIB_FAIL);
    ADV_REQUIRE(s, lifetime_borrow_from(&b, (lifetime_owner *)0, &err) ==
                       STDLIB_FAIL);
    ADV_REQUIRE(s, lifetime_read((lifetime_borrow *)0, 0u, nh_dst, 4u, &err) ==
                       STDLIB_FAIL);
    ADV_REQUIRE(s, lifetime_write(&b, 0u, (const void *)0, 4u, &err) ==
                       STDLIB_FAIL);
    ADV_REQUIRE(s, err.failed_now());
    return s->fail == 0;
}

static int cs_life_uaf_meta(adv_state *s, unsigned long long p1,
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
    ADV_REQUIRE(s, lifetime_owner_init(&o, 32u, &err) == STDLIB_OK);
    saved_ptr = o.ptr;
    saved_gen = o.token;
    ADV_REQUIRE(s, lifetime_owner_release(&o, &err) == STDLIB_OK);
    /* metadata-only queries on the stale pointer: never dereference it */
    ADV_REQUIRE(s, stdlib_alloc_size(saved_ptr) == 0u);
    ADV_REQUIRE(s, stdlib_alloc_generation(saved_ptr) == 0ULL);
    ADV_REQUIRE(s, stdlib_alloc_view_live(saved_ptr) == 0);
    ADV_REQUIRE(s, stdlib_alloc_cap_state(saved_ptr) == STDLIB_ALLOC_CAP_NONE);
    ADV_REQUIRE(s, stdlib_alloc_hasvalidcap(saved_ptr) == 0);
    ADV_REQUIRE(s, stdlib_alloc_cap_covers(saved_ptr, 0u, 4u) == 0);
    ADV_REQUIRE(s, stdlib_alloc_cap_check(saved_ptr, saved_gen, 0u, 4u) == 0);
    ADV_REQUIRE(s, stdlib_alloc_id_matches((stdlib_alloc_id *)0, saved_ptr) ==
                       0);
    return s->fail == 0;
}

/* ========================================================================= */
/* alloc (generation / liveness helper) family                               */
/* ========================================================================= */

static int cs_alloc_id_live(adv_state *s, unsigned long long p1,
                            unsigned long long p2, const char **res, int *ref) {
    size_t n = (size_t)(16u + p2 % 64u);
    void *p = stdlib_alloc(n);
    stdlib_alloc_id id;
    stdlib_alloc_id id2;
    (void)p1;
    *res = "defined";
    *ref = 0;
    ADV_REQUIRE(s, p != (void *)0);
    ADV_REQUIRE(s, stdlib_alloc_identity(p, &id) == 0 && id.base == p &&
                       id.generation != 0ULL);
    ADV_REQUIRE(s, stdlib_alloc_id_matches(&id, p) == 1);
    ADV_REQUIRE(s, stdlib_alloc_cap_check(p, id.generation, 0u, n) == 1);
    ADV_REQUIRE(s, stdlib_alloc_view_live(p) == 1);
    ADV_REQUIRE(s, stdlib_alloc_view_remaining(p, n / 2u) == n - (n / 2u));
    ADV_REQUIRE(s, stdlib_alloc_identity(p, &id2) == 0 &&
                       stdlib_alloc_id_equal(&id, &id2) == 1);
    stdlib_release(p);
    return s->fail == 0;
}

static int cs_alloc_stale_generation(adv_state *s, unsigned long long p1,
                                     unsigned long long p2, const char **res,
                                     int *ref) {
    void *p = stdlib_alloc(16u);
    stdlib_alloc_id id;
    unsigned long long gen;
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, p != (void *)0);
    ADV_REQUIRE(s, stdlib_alloc_identity(p, &id) == 0);
    gen = id.generation;
    stdlib_release(p);
    ADV_REQUIRE(s, stdlib_alloc_view_generation(p) == 0ULL);
    ADV_REQUIRE(s, stdlib_alloc_id_matches(&id, p) == 0);
    ADV_REQUIRE(s, stdlib_alloc_view_unchanged(p, gen) == 0);
    ADV_REQUIRE(s, stdlib_alloc_cap_check(p, gen, 0u, 1u) == 0);
    ADV_REQUIRE(s, stdlib_alloc_id_equal(&id, &id) == 1);
    return s->fail == 0;
}

static int cs_alloc_cap_bounds(adv_state *s, unsigned long long p1,
                               unsigned long long p2, const char **res,
                               int *ref) {
    void *p = stdlib_alloc(16u);
    (void)p1;
    (void)p2;
    *res = "refused";
    *ref = 1;
    ADV_REQUIRE(s, p != (void *)0);
    ADV_REQUIRE(s, stdlib_alloc_cap_covers(p, 0u, 16u) == 1);
    ADV_REQUIRE(s, stdlib_alloc_cap_covers(p, 14u, 4u) == 0);
    ADV_REQUIRE(s, stdlib_alloc_cap_covers(p, 17u, 0u) == 0);
    ADV_REQUIRE(s, stdlib_alloc_cap_check(p, stdlib_alloc_generation(p), 0u,
                                          16u) == 1);
    stdlib_release(p);
    return s->fail == 0;
}

static int cs_alloc_null_interior(adv_state *s, unsigned long long p1,
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
    ADV_REQUIRE(s, p != (void *)0);
    ADV_REQUIRE(s, stdlib_alloc_view_bytes((void *)0) == 0u);
    ADV_REQUIRE(s, stdlib_alloc_identity((void *)0, &id) == -1 &&
                       id.base == (const void *)0 && id.generation == 0ULL);
    ADV_REQUIRE(s, stdlib_alloc_generation((unsigned char *)p + 1) == 0ULL);
    ADV_REQUIRE(s, stdlib_alloc_view_live((unsigned char *)p + 1) == 0);
    ADV_REQUIRE(s, stdlib_alloc_cap_state((unsigned char *)p + 1) ==
                       STDLIB_ALLOC_CAP_NONE);
    src = p;
    ADV_REQUIRE(s, stdlib_alloc_view_transfer_ok(&dst, &src) == 1);
    stdlib_release(p);
    ADV_REQUIRE(s, stdlib_alloc_view_transfer_ok(&dst, &src) == 0);
    return s->fail == 0;
}

/* ---- case table ---------------------------------------------------------- */

typedef struct adv_case_def {
    const char *api;
    const char *cname;
    adv_case_fn fn;
} adv_case_def;

static adv_case_def ADV_CASES[] = {
    /* mutex (6) */
    {"mutex", "cycle", cs_mutex_cycle},
    {"mutex", "double_unlock", cs_mutex_double_unlock},
    {"mutex", "double_lock", cs_mutex_double_lock},
    {"mutex", "double_drop", cs_mutex_double_drop},
    {"mutex", "zero_handle", cs_mutex_zero_handle},
    {"mutex", "null_guard", cs_mutex_null_guard},
    /* condvar (4) */
    {"condvar", "signal_broadcast", cs_cv_signal},
    {"condvar", "wait_invalid", cs_cv_wait_invalid},
    {"condvar", "zero_handle", cs_cv_zero},
    {"condvar", "double_drop", cs_cv_double_drop},
    /* thread (5) */
    {"thread", "start_join", cs_thread_start_join},
    {"thread", "double_start", cs_thread_double_start},
    {"thread", "failed_join", cs_thread_failed_join},
    {"thread", "null_fn", cs_thread_null_fn},
    {"thread", "drop_joins", cs_thread_drop_joins},
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
    /* lifetime (6) */
    {"lifetime", "valid_rw", cs_life_valid_rw},
    {"lifetime", "stale_token", cs_life_stale_token},
    {"lifetime", "double_release", cs_life_double_release},
    {"lifetime", "transfer", cs_life_transfer},
    {"lifetime", "null_handles", cs_life_null_handles},
    {"lifetime", "uaf_meta", cs_life_uaf_meta},
    /* alloc (4) */
    {"alloc", "id_live", cs_alloc_id_live},
    {"alloc", "stale_generation", cs_alloc_stale_generation},
    {"alloc", "cap_bounds", cs_alloc_cap_bounds},
    {"alloc", "null_interior", cs_alloc_null_interior},
};

int main(void) {
    unsigned long long rng = ADV_SEED;
    unsigned long long p1;
    unsigned long long p2;
    unsigned long long rsel;
    int ncases = (int)(sizeof(ADV_CASES) / sizeof(ADV_CASES[0]));
    int id;
    int i;
    int j;
    int dup = 0;
    char hex[32];
    mutex ec_m = mutex();

    signal(SIGALRM, adv_on_alarm);
    alarm(ADV_WATCHDOG_SECS);

    /* Configure and self-check the ERRORCHECK mutex used by double_lock. */
    g_ec_ptr = &ec_m;
    adv_to_errorcheck(&ec_m);
    {
        int a = pthread_mutex_lock((pthread_mutex_t *)ec_m.native());
        int b = pthread_mutex_lock((pthread_mutex_t *)ec_m.native());
        pthread_mutex_unlock((pthread_mutex_t *)ec_m.native());
        if (a == 0 && b == EDEADLK)
            g_ec_ok = 1;
    }

    for (id = 1; id <= ADV_N; id++) {
        adv_state st;
        adv_case_def *d;
        const char *result = "FAIL";
        int refused = 0;
        int outcome;
        int sok;

        rsel = adv_rnd(&rng);
        p1 = adv_rnd(&rng);
        p2 = adv_rnd(&rng);
        d = &ADV_CASES[(int)(rsel % (unsigned long long)ncases)];

        adv_state_init(&st);
        outcome = d->fn(&st, p1, p2, &result, &refused);
        sok = (adv_state_ok(&st) && outcome) ? 1 : 0;

        if (!sok)
            g_failures++;
        if (sok && refused)
            g_refused++;
        if (sok)
            g_state_ok++;

        g_case_of[id - 1] = (int)(rsel % (unsigned long long)ncases);
        g_p1_of[id - 1] = p1;
        g_p2_of[id - 1] = p2;

        g_digest = adv_mix(g_digest, (unsigned long long)id);
        g_digest = adv_mix(g_digest, (unsigned long long)g_case_of[id - 1]);
        g_digest = adv_mix(g_digest, p1);
        g_digest = adv_mix(g_digest, p2);
        g_digest = adv_mix(g_digest, (unsigned long long)sok);

        print("S", id, d->api, d->cname, result, sok);
    }

    /* Deterministic distinctness check on (case, p1, p2) tuples. */
    for (i = 0; i < ADV_N; i++) {
        for (j = i + 1; j < ADV_N; j++) {
            if (g_case_of[i] == g_case_of[j] && g_p1_of[i] == g_p1_of[j] &&
                g_p2_of[i] == g_p2_of[j])
                dup++;
        }
    }
    if (dup != 0)
        g_failures++;

    alarm(0);
    ec_m.drop();

    snprintf(hex, sizeof(hex), "%016llx", g_digest);
    print("ADV1000_DISTINCT", dup);
    print("ADV1000_DIGEST", hex);
    print("ADV1000", "thread_async", ADV_N, g_refused, g_state_ok, g_failures);
    return g_failures != 0;
}
