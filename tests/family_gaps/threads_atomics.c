/* Family-gap evidence: C11 <threads.h> + <stdatomic.h>.
 *
 * THREADS: exercises the complete C11 threads surface through the namespaced
 *   cplus_threads.h adapter (libc/install-include, implemented in
 *   libc/src/thread/threads_c11.c as a thin adapter over the pthread-backed
 *   cplus_pthread_* wrappers in libc/src/thread/thread.c). Canonical ISO C11
 *   spellings (thrd_create, mtx_t, ONCE_FLAG_INIT, ...) are the source-level
 *   aliases defined by that header and are used here too. The pthread-backed
 *   cplus_pthread_* wrappers are also called directly below.
 *
 * ATOMICS — why there is NO cplus_atomic_* C ABI symbol (evidence from
 *   docs/libc/iso_c_inventory.json, "stdatomic.h" entries):
 *   The inventory records 49 stdatomic.h APIs: 34 UNSUPPORTED (functions such
 *   as atomic_init/load/store, the compare-exchange family, the fetch_*
 *   family, atomic_flag_* ops, the fences and kill_dependency, plus C23
 *   additions) and 15 HOST/COMPILER-PROVIDED (types atomic_int/atomic_flag/
 *   memory_order and the ATOMIC_*_INIT macros). The verbatim limitation on the
 *   unsupported function entries is:
 *     "No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic
 *      macros expand to compiler type-generic builtins and cannot be
 *      represented as a single C99 function. Host <stdatomic.h>/__atomic_*
 *      builtins are used internally only."
 *   In other words these names are type-generic _Generic macros: the emitted
 *   instruction sequence depends on the operand's C type (int vs long vs
 *   pointer vs atomic_flag). A single, fixed-signature C ABI function cannot
 *   encode that choice, so the library deliberately exports no cplus_atomic_*
 *   symbol; it uses the compiler builtins internally only (see libc/log/log.c,
 *   which uses atomic_int/atomic_store). The atomics section below therefore
 *   exercises the HOST COMPILER's _Atomic int -- spelled atomic_int, the
 *   <stdatomic.h> typedef for _Atomic int -- with atomic_fetch_add. This is
 *   compiler-provided and is NOT a function supplied by this library.
 *
 * RACE-FREEDOM: all mutable thread-shared state is either protected by a
 *   C11 mutex / pthread mutex or is an _Atomic object. The CHECK bookkeeping
 *   counters are themselves atomic, so CHECK is safe to call from workers.
 *   Every worker is joined before its results are read (the one detached
 *   worker publishes completion through an _Atomic flag), so there are no
 *   test-side data races.
 *
 * Build (family-gaps convention, plain + UBSan, both libs):
 *   clang -O2 -std=c99 -Wall -Wextra -pedantic -Ilibc -Ilibc/install-include \
 *     tests/family_gaps/threads_atomics.c \
 *     build/libc/libcplus_hardened.a -lm -pthread -o threads_atomics
 *   (link build/safe/libcplus_safecrt.a before the hardened lib for the
 *    "both libs" variant; add -fsanitize=undefined
 *    -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero for UBSan.)
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <pthread.h>  /* host pthreads: only for declaring cplus_pthread_* */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Host compiler atomics. atomic_int is the <stdatomic.h> typedef for
 * _Atomic int; using the typedef avoids -Wc11-extensions under -std=c99.
 * These are compiler builtins, NOT symbols from this library. */
#include <stdatomic.h>

#include "cplus_threads.h"

/* extern declarations matching libc/src/thread/thread.c (pthread-backed) */
extern int cplus_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
extern int cplus_pthread_join(pthread_t, void **);
extern int cplus_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern int cplus_pthread_mutex_lock(pthread_mutex_t *);
extern int cplus_pthread_mutex_unlock(pthread_mutex_t *);
extern int cplus_pthread_mutex_destroy(pthread_mutex_t *);
extern int cplus_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
extern int cplus_pthread_cond_signal(pthread_cond_t *);
extern int cplus_pthread_cond_broadcast(pthread_cond_t *);
extern int cplus_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
extern int cplus_pthread_cond_destroy(pthread_cond_t *);

/* ---- check harness (atomic counters => race-free from worker threads) ---- */
static atomic_int g_checks;
static atomic_int g_failed;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        atomic_fetch_add(&g_checks, 1);                                        \
        if (!(cond)) {                                                         \
            atomic_fetch_add(&g_failed, 1);                                    \
            fprintf(stderr, "FINDING: %s (line %d)\n", (name), __LINE__);      \
        }                                                                      \
    } while (0)

/* ---- shared state (mutex-protected or atomic) --------------------------- */
static mtx_t g_mtx;
static long g_counter = 0;            /* guarded by g_mtx */
static thrd_t g_id0, g_id1;          /* guarded by g_mtx */

static mtx_t g_cv_mtx;
static cnd_t g_cv;
static int g_ready = 0;              /* guarded by g_cv_mtx */

static tss_t g_tss_key;
static atomic_int g_dtor_calls;

static once_flag g_once = ONCE_FLAG_INIT;
static atomic_int g_once_runs;

static atomic_int g_detached_done;

static atomic_int g_atomic_counter;   /* compiler-provided _Atomic int */

/* pthread-wrapper-only state */
static pthread_cond_t g_pcond;
static pthread_mutex_t g_pmutex;
static atomic_int g_pready;

/* --------------------------------------------------------------- workers */
static int counter_worker(void *arg)
{
    long n = *(long *)arg;
    long i;
    for (i = 0; i < n; ++i) {
        CHECK(mtx_lock(&g_mtx) == thrd_success, "mtx_lock in counter_worker");
        ++g_counter;
        CHECK(mtx_unlock(&g_mtx) == thrd_success, "mtx_unlock in counter_worker");
    }
    return 42;
}

static int exit_worker(void *arg)
{
    (void)arg;
    thrd_exit(77);            /* must not return */
    return -1;                /* unreachable */
}

static int id_worker(void *arg)
{
    thrd_t *slot = (thrd_t *)arg;
    thrd_t self = thrd_current();
    CHECK(mtx_lock(&g_mtx) == thrd_success, "mtx_lock in id_worker");
    *slot = self;
    CHECK(mtx_unlock(&g_mtx) == thrd_success, "mtx_unlock in id_worker");
    return 0;
}

static int signal_waiter(void *arg)
{
    int *out = (int *)arg;
    CHECK(mtx_lock(&g_cv_mtx) == thrd_success, "mtx_lock in signal_waiter");
    while (!g_ready)
        CHECK(cnd_wait(&g_cv, &g_cv_mtx) == thrd_success, "cnd_wait");
    CHECK(mtx_unlock(&g_cv_mtx) == thrd_success, "mtx_unlock in signal_waiter");
    *out = 0;
    return 0;
}

static int timed_waiter(void *arg)
{
    struct timespec deadline;
    int rc;
    (void)arg;
    CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0, "clock_gettime (timed waiter)");
    deadline.tv_nsec += 100000000L; /* 100 ms */
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        deadline.tv_sec += 1;
    }
    CHECK(mtx_lock(&g_cv_mtx) == thrd_success, "mtx_lock in timed_waiter");
    rc = cnd_timedwait(&g_cv, &g_cv_mtx, &deadline);
    while (rc == thrd_success) /* tolerate spurious wakeups */
        rc = cnd_timedwait(&g_cv, &g_cv_mtx, &deadline);
    CHECK(mtx_unlock(&g_cv_mtx) == thrd_success, "mtx_unlock in timed_waiter");
    return rc;
}

static void tss_dtor(void *p)
{
    free(p);
    atomic_fetch_add(&g_dtor_calls, 1);
}

static int tss_worker(void *arg)
{
    int *p;
    (void)arg;
    p = (int *)malloc(sizeof *p);
    CHECK(p != NULL, "malloc in tss_worker");
    if (p == NULL) return -1;
    *p = 7;
    CHECK(tss_set(g_tss_key, p) == thrd_success, "tss_set");
    CHECK(*(int *)tss_get(g_tss_key) == 7, "tss_get after set");
    return 0;
}

static void once_init(void) { atomic_fetch_add(&g_once_runs, 1); }
static void once_noop(void) { }

static int once_worker(void *arg)
{
    (void)arg;
    call_once(&g_once, once_init);
    return 0;
}

static int detach_worker(void *arg)
{
    (void)arg;
    thrd_yield();
    atomic_store(&g_detached_done, 1);
    return 0;
}

static int atomic_worker(void *arg)
{
    long n = *(long *)arg;
    long i;
    for (i = 0; i < n; ++i)
        atomic_fetch_add(&g_atomic_counter, 1); /* compiler builtin */
    return 0;
}

/* ---- direct cplus_pthread_* wrapper workers ---------------------------- */
struct pw_arg { int val; };

static void *pworker(void *arg)
{
    struct pw_arg *a = (struct pw_arg *)arg;
    return (void *)(intptr_t)(a->val + 1);
}

static void *pcond_waiter(void *arg)
{
    pthread_mutex_t *pm = (pthread_mutex_t *)arg;
    CHECK(cplus_pthread_mutex_lock(pm) == 0, "cplus_pthread_mutex_lock (cond waiter)");
    while (!atomic_load(&g_pready))
        CHECK(cplus_pthread_cond_wait(&g_pcond, pm) == 0, "cplus_pthread_cond_wait");
    CHECK(cplus_pthread_mutex_unlock(pm) == 0, "cplus_pthread_mutex_unlock (cond waiter)");
    return NULL;
}

int main(void)
{
    thrd_t t1, t2, te, tdet, tw;
    struct timespec zero, rem, past, future;
    long a1 = 1000, a2 = 1000, per = 1000;
    int r1 = -1, r2 = -1, re = -1, wt_rc = -1, waiter_rc = -1;
    int i;
    mtx_t rec;

    memset(&zero, 0, sizeof zero);

    /* (1) thrd_current / thrd_equal / thrd_sleep / thrd_yield ------------- */
    CHECK(thrd_equal(thrd_current(), thrd_current()) != 0, "thrd_current/thrd_equal self");
    CHECK(thrd_sleep(&zero, &rem) == 0, "thrd_sleep(0)");
    thrd_yield();
    CHECK(1, "thrd_yield returned");

    /* (2) mtx_init + thrd_create/thrd_join (mutex-protected counter) ------ */
    CHECK(mtx_init(&g_mtx, mtx_plain) == thrd_success, "mtx_init(plain)");
    CHECK(thrd_create(&t1, counter_worker, &a1) == thrd_success, "thrd_create #1");
    CHECK(thrd_create(&t2, counter_worker, &a2) == thrd_success, "thrd_create #2");
    CHECK(thrd_join(t1, &r1) == thrd_success, "thrd_join #1");
    CHECK(thrd_join(t2, &r2) == thrd_success, "thrd_join #2");
    CHECK(g_counter == 2000, "mutex-protected counter");
    CHECK(r1 == 42 && r2 == 42, "thread return values");
    mtx_destroy(&g_mtx);

    /* (3) thrd_exit result propagates through thrd_join ------------------- */
    CHECK(thrd_create(&te, exit_worker, NULL) == thrd_success, "thrd_create (exit)");
    CHECK(thrd_join(te, &re) == thrd_success, "thrd_join (exit)");
    CHECK(re == 77, "thrd_exit result");

    /* (4) thrd_current inside workers => distinct thread IDs ------------- */
    CHECK(mtx_init(&g_mtx, mtx_plain) == thrd_success, "mtx_init (ids)");
    CHECK(thrd_create(&t1, id_worker, &g_id0) == thrd_success, "thrd_create (id #1)");
    CHECK(thrd_create(&t2, id_worker, &g_id1) == thrd_success, "thrd_create (id #2)");
    CHECK(thrd_join(t1, NULL) == thrd_success, "thrd_join (id #1)");
    CHECK(thrd_join(t2, NULL) == thrd_success, "thrd_join (id #2)");
    CHECK(thrd_equal(g_id0, g_id1) == 0, "distinct thread IDs");
    mtx_destroy(&g_mtx);

    /* (5) mtx_trylock / mtx_timedlock / recursive / error codes ---------- */
    CHECK(mtx_init(&rec, 0x80) == thrd_error, "mtx_init(bad flags) -> thrd_error");
    CHECK(mtx_init(&g_mtx, mtx_plain) == thrd_success, "mtx_init (trylock)");
    CHECK(mtx_lock(&g_mtx) == thrd_success, "mtx_lock");
    CHECK(mtx_trylock(&g_mtx) == thrd_busy, "mtx_trylock on locked -> thrd_busy");
    CHECK(clock_gettime(CLOCK_REALTIME, &past) == 0, "clock_gettime (past)");
    past.tv_sec -= 1; /* expired CLOCK_REALTIME deadline */
    CHECK(mtx_timedlock(&g_mtx, &past) == thrd_timedout, "mtx_timedlock(past) -> timedout");
    CHECK(mtx_unlock(&g_mtx) == thrd_success, "mtx_unlock");
    CHECK(clock_gettime(CLOCK_REALTIME, &future) == 0, "clock_gettime (future)");
    future.tv_sec += 1;
    CHECK(mtx_timedlock(&g_mtx, &future) == thrd_success, "mtx_timedlock(future) on free mutex");
    CHECK(mtx_unlock(&g_mtx) == thrd_success, "mtx_unlock after timedlock");
    mtx_destroy(&g_mtx);

    CHECK(mtx_init(&rec, mtx_recursive) == thrd_success, "mtx_init(recursive)");
    CHECK(mtx_lock(&rec) == thrd_success, "recursive lock #1");
    CHECK(mtx_lock(&rec) == thrd_success, "recursive lock #2");
    CHECK(mtx_unlock(&rec) == thrd_success, "recursive unlock #1");
    CHECK(mtx_unlock(&rec) == thrd_success, "recursive unlock #2");
    mtx_destroy(&rec);

    /* (6) cnd_init/signal/broadcast/wait/timedwait/destroy --------------- */
    CHECK(mtx_init(&g_cv_mtx, mtx_plain) == thrd_success, "mtx_init (cond)");
    CHECK(cnd_init(&g_cv) == thrd_success, "cnd_init");
    g_ready = 0; /* written before any waiter exists */
    CHECK(thrd_create(&tw, signal_waiter, &waiter_rc) == thrd_success, "thrd_create (waiter)");
    CHECK(mtx_lock(&g_cv_mtx) == thrd_success, "mtx_lock before signal");
    g_ready = 1;
    CHECK(cnd_signal(&g_cv) == thrd_success, "cnd_signal");
    CHECK(cnd_broadcast(&g_cv) == thrd_success, "cnd_broadcast");
    CHECK(mtx_unlock(&g_cv_mtx) == thrd_success, "mtx_unlock after signal");
    CHECK(thrd_join(tw, NULL) == thrd_success, "thrd_join (waiter)");
    CHECK(waiter_rc == 0 && g_ready == 1, "cnd_wait predicate");

    CHECK(thrd_create(&tw, timed_waiter, &wt_rc) == thrd_success, "thrd_create (timed waiter)");
    CHECK(thrd_join(tw, &wt_rc) == thrd_success, "thrd_join (timed waiter)");
    CHECK(wt_rc == thrd_timedout, "cnd_timedwait -> timedout");

    CHECK(mtx_lock(&g_cv_mtx) == thrd_success, "mtx_lock before main timedwait");
    CHECK(cnd_timedwait(&g_cv, &g_cv_mtx, &past) == thrd_timedout, "cnd_timedwait(past)");
    CHECK(mtx_unlock(&g_cv_mtx) == thrd_success, "mtx_unlock after main timedwait");
    cnd_destroy(&g_cv);
    mtx_destroy(&g_cv_mtx);

    /* (7) tss_create/get/set/delete + destructor at thread exit ---------- */
    CHECK(tss_create(&g_tss_key, tss_dtor) == thrd_success, "tss_create");
    CHECK(tss_get(g_tss_key) == NULL, "tss_get unset");
    CHECK(tss_set((tss_t)123456789UL, (void *)1) == thrd_error, "tss_set(bad key) -> error");
    atomic_store(&g_dtor_calls, 0);
    CHECK(thrd_create(&tw, tss_worker, NULL) == thrd_success, "thrd_create (tss)");
    CHECK(thrd_join(tw, NULL) == thrd_success, "thrd_join (tss)");
    CHECK(atomic_load(&g_dtor_calls) == 1, "tss destructor ran at thread exit");
    tss_delete(g_tss_key);

    /* (8) call_once from several threads runs the initializer once ------- */
    {
        thrd_t once_threads[4];
        atomic_store(&g_once_runs, 0);
        for (i = 0; i < 4; ++i)
            CHECK(thrd_create(&once_threads[i], once_worker, NULL) == thrd_success, "thrd_create (once)");
        for (i = 0; i < 4; ++i)
            CHECK(thrd_join(once_threads[i], NULL) == thrd_success, "thrd_join (once)");
        CHECK(atomic_load(&g_once_runs) == 1, "call_once ran exactly once");
        call_once(&g_once, once_noop);
        CHECK(atomic_load(&g_once_runs) == 1, "call_once second call is a no-op");
    }

    /* (9) thrd_detach: completion observed via _Atomic flag -------------- */
    {
        struct timespec nap;
        int spins;
        atomic_store(&g_detached_done, 0);
        memset(&nap, 0, sizeof nap);
        nap.tv_nsec = 1000000L; /* 1 ms */
        CHECK(thrd_create(&tdet, detach_worker, NULL) == thrd_success, "thrd_create (detach)");
        CHECK(thrd_detach(tdet) == thrd_success, "thrd_detach");
        for (spins = 0; spins < 1000 && !atomic_load(&g_detached_done); ++spins)
            (void)thrd_sleep(&nap, NULL);
        CHECK(atomic_load(&g_detached_done) == 1, "detached worker published completion");
    }

    /* (10) direct pthread-backed cplus_pthread_* wrappers ---------------- */
    {
        pthread_mutex_t pm;
        pthread_cond_t pc;
        pthread_t pt, cw;
        struct pw_arg pw = { 41 };
        void *prv = NULL;

        CHECK(cplus_pthread_mutex_init(&pm, NULL) == 0, "cplus_pthread_mutex_init");
        CHECK(cplus_pthread_mutex_lock(&pm) == 0, "cplus_pthread_mutex_lock");
        CHECK(cplus_pthread_mutex_unlock(&pm) == 0, "cplus_pthread_mutex_unlock");
        CHECK(cplus_pthread_mutex_destroy(&pm) == 0, "cplus_pthread_mutex_destroy");

        CHECK(cplus_pthread_cond_init(&pc, NULL) == 0, "cplus_pthread_cond_init");
        CHECK(cplus_pthread_cond_signal(&pc) == 0, "cplus_pthread_cond_signal");
        CHECK(cplus_pthread_cond_broadcast(&pc) == 0, "cplus_pthread_cond_broadcast");
        CHECK(cplus_pthread_cond_destroy(&pc) == 0, "cplus_pthread_cond_destroy");

        CHECK(cplus_pthread_create(&pt, NULL, pworker, &pw) == 0, "cplus_pthread_create");
        CHECK(cplus_pthread_join(pt, &prv) == 0, "cplus_pthread_join");
        CHECK((int)(intptr_t)prv == 42, "cplus_pthread worker result");

        atomic_store(&g_pready, 0);
        CHECK(cplus_pthread_mutex_init(&g_pmutex, NULL) == 0, "cplus_pthread_mutex_init (cond)");
        CHECK(cplus_pthread_cond_init(&g_pcond, NULL) == 0, "cplus_pthread_cond_init (cond)");
        CHECK(cplus_pthread_create(&cw, NULL, pcond_waiter, &g_pmutex) == 0,
              "cplus_pthread_create (cond waiter)");
        CHECK(cplus_pthread_mutex_lock(&g_pmutex) == 0, "cplus_pthread_mutex_lock (signal)");
        atomic_store(&g_pready, 1);
        CHECK(cplus_pthread_cond_signal(&g_pcond) == 0, "cplus_pthread_cond_signal (waiter)");
        CHECK(cplus_pthread_mutex_unlock(&g_pmutex) == 0, "cplus_pthread_mutex_unlock (signal)");
        CHECK(cplus_pthread_join(cw, NULL) == 0, "cplus_pthread_join (cond waiter)");
        CHECK(atomic_load(&g_pready) == 1, "pthread cond predicate after signal");
        CHECK(cplus_pthread_cond_destroy(&g_pcond) == 0, "cplus_pthread_cond_destroy (cond)");
        CHECK(cplus_pthread_mutex_destroy(&g_pmutex) == 0, "cplus_pthread_mutex_destroy (cond)");
    }

    /* (11) host-compiler atomics (_Atomic int / atomic_fetch_add) ---------
     * Not a cplus_libc API: see the rationale block at the top of this file.
     */
    {
        atomic_int a = 0;       /* == _Atomic int from the host compiler */
        int old;

        old = atomic_fetch_add(&a, 5);
        CHECK(old == 0 && atomic_load(&a) == 5, "atomic_fetch_add(+5)");
        old = atomic_fetch_add(&a, 7);
        CHECK(old == 5 && atomic_load(&a) == 12, "atomic_fetch_add(+7)");
        atomic_store(&a, 0);
        CHECK(atomic_load(&a) == 0, "atomic_store/atomic_load");

        {
            thrd_t atomic_threads[4];
            atomic_store(&g_atomic_counter, 0);
            for (i = 0; i < 4; ++i)
                CHECK(thrd_create(&atomic_threads[i], atomic_worker, &per) == thrd_success,
                      "thrd_create (atomic worker)");
            for (i = 0; i < 4; ++i)
                CHECK(thrd_join(atomic_threads[i], NULL) == thrd_success, "thrd_join (atomic worker)");
            CHECK(atomic_load(&g_atomic_counter) == 4000, "atomic_fetch_add across 4 threads");
        }
    }

    {
        int total = atomic_load(&g_checks);
        int failed = atomic_load(&g_failed);
        printf("threads_atomics: %d/%d checks ok\n", total - failed, total);
        return failed ? 1 : 0;
    }
}
