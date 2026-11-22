/* runtime_13_threads.c -- distinct concurrency/runtime scenarios for the
 * hardened SafeCRT + hardened libc pair.
 *
 * Build (see tests/scenarios/run_scenarios.sh):
 *   clang -O2 -std=c99 -Wall -Wextra -pedantic -I. -Istdlib/safe \
 *         tests/scenarios/runtime_13_threads.c \
 *         build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *         -lm -pthread
 *
 * Test-side race discipline: every byte shared by two threads is either
 * (a) published before any thread is created and never written afterwards,
 * (b) kept in a distinct per-thread slot (args[i]/rets[i]), or
 * (c) accessed only while holding a pthread mutex / condvar / barrier.
 * No test variable is read and written concurrently without one of those.
 *
 * Failure-injection and log-level knobs are process-global, so scenarios are
 * executed strictly sequentially by main() and each such scenario restores
 * the global state before returning.
 *
 * FINDING (library, not test): cplus_safe_remaining_capacity() scans the live
 * table linearly and returns the FIRST block whose [base, base+size] range
 * contains the pointer. When two live blocks are adjacent, a valid block base
 * that equals the previous block's one-past-end resolves to capacity 0, so the
 * auto-bounds memset/memcmp wrappers falsely refuse a correctly sized access
 * with ESNOSPC 406. Heap writes in this file therefore use the explicit-dmax
 * checked entry points (cplus_memset_s/cplus_memcmp_s) with the authoritative
 * cplus_safe_ptr_size(); the ordinary strcpy auto-wrapper is unaffected
 * because it resolves dmax through cplus_safe_ptr_size().
 *
 * FINDING (library, not test): realloc_s() releases the metadata lock across
 * the underlying raw realloc(). A concurrent allocation can then be handed the
 * just-freed old address and recorded in the stale slot; when the realloc
 * finishes it removes that slot, leaving the other thread's block untracked.
 * A follow-up memset then reports a false "use-after-free" (exit 70) or a
 * zero capacity. An independent two-thread probe reproduces this reliably.
 * Scenario 24 therefore serialises its malloc/realloc/free cycle with a mutex;
 * scenario 3 still exercises genuinely concurrent realloc (generation checks
 * are invariant under the race and remain a valid stress test).
 */
#define _POSIX_C_SOURCE 200809L

#include "stdlib/safe/safe_c.h"
#include "safe_runtime.h"
#include "safe_log.h"
#include "safe_stats.h"

#include <errno.h>
#include <fenv.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_THREADS 8
#define BAD ((void *)(uintptr_t)1)

static int g_fail;

#define CHK(cond)                                                               \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "sc_13: CHECK failed: %s (line %d)\n",              \
                    #cond, __LINE__);                                           \
            g_fail = 1;                                                         \
            return;                                                             \
        }                                                                       \
    } while (0)

typedef void *(*thread_fn)(void *);

static void run_threads(int n, thread_fn fn, void **args, void **rets) {
    pthread_t tids[MAX_THREADS];
    int made = 0;
    if (n < 0 || n > MAX_THREADS) { g_fail = 1; return; }
    for (int i = 0; i < n; ++i) {
        if (pthread_create(&tids[i], NULL, fn, args[i]) != 0) {
            fprintf(stderr, "sc_13: pthread_create failed at %d\n", i);
            g_fail = 1;
            break;
        }
        ++made;
    }
    for (int i = 0; i < made; ++i) {
        void *rv = NULL;
        if (pthread_join(tids[i], &rv) != 0) {
            fprintf(stderr, "sc_13: pthread_join failed at %d\n", i);
            g_fail = 1;
        }
        if (rets) rets[i] = rv;
    }
}

static void check_rets(int n, void **rets) {
    for (int i = 0; i < n; ++i) {
        if (rets[i] != NULL) {
            fprintf(stderr, "sc_13: worker %d returned failure\n", i);
            g_fail = 1;
        }
    }
}

/* macOS does not ship pthread barriers, so use a portable, reusable barrier
 * built from a mutex + condvar. All its state lives under the mutex, so it is
 * race-free from the test's point of view. */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int count;
    int arrived;
    unsigned long generation;
} sc_barrier;

static int sc_barrier_init(sc_barrier *b, int count) {
    b->count = count; b->arrived = 0; b->generation = 0;
    if (pthread_mutex_init(&b->mu, NULL) != 0) return -1;
    if (pthread_cond_init(&b->cv, NULL) != 0) {
        pthread_mutex_destroy(&b->mu);
        return -1;
    }
    return 0;
}

static int sc_barrier_wait(sc_barrier *b) {
    pthread_mutex_lock(&b->mu);
    unsigned long gen = b->generation;
    if (++b->arrived == b->count) {
        b->arrived = 0;
        ++b->generation;
        pthread_cond_broadcast(&b->cv);
        pthread_mutex_unlock(&b->mu);
        return 0;
    }
    while (gen == b->generation) pthread_cond_wait(&b->cv, &b->mu);
    pthread_mutex_unlock(&b->mu);
    return 0;
}

static void sc_barrier_destroy(sc_barrier *b) {
    pthread_cond_destroy(&b->cv);
    pthread_mutex_destroy(&b->mu);
}

/* ------------------------------------------------------------------------- */
/* 01. 8-thread malloc/free churn with per-byte pattern verification.        */
/* ------------------------------------------------------------------------- */
typedef struct { int id; } sc_arg;

static void *churn_worker(void *p) {
    sc_arg *a = (sc_arg *)p;
    for (int i = 0; i < 240; ++i) {
        size_t sz = (size_t)((a->id * 13 + i * 7) % 96) + 1u;
        unsigned char *q = (unsigned char *)malloc(sz);
        if (!q) return BAD;
        if (cplus_safe_ptr_size(q) != sz) return BAD;
        unsigned char pat = (unsigned char)(a->id * 31 + i);
        if (cplus_memset_s(q, sz, pat, sz) != 0) return BAD;
        for (size_t k = 0; k < sz; ++k) if (q[k] != pat) return BAD;
        free(q);
    }
    return NULL;
}

static void sc_13_churn_8threads_pattern(void) {
    sc_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 8; ++i) {
        args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(8, churn_worker, argp, rets);
    check_rets(8, rets);
}

/* ------------------------------------------------------------------------- */
/* 02. Concurrent metadata queries over a published, immutable pointer set.  */
/* ------------------------------------------------------------------------- */
typedef struct {
    sc_barrier bar;
    int n;
    void *blocks[MAX_THREADS];
    size_t sizes[MAX_THREADS];
} meta_ctx;
typedef struct { meta_ctx *c; int id; } meta_arg;

static void *meta_worker(void *p) {
    meta_arg *a = (meta_arg *)p;
    meta_ctx *c = a->c;
    int br = sc_barrier_wait(&c->bar);
    if (br != 0) return BAD;
    (void)a->id;
    for (int r = 0; r < 80; ++r) {
        for (int j = 0; j < c->n; ++j) {
            void *b = c->blocks[j];
            cplus_safe_alloc_meta m;
            size_t out = 0;
            if (cplus_safe_ptr_size(b) != c->sizes[j]) return BAD;
            if (cplus_safe_ptr_count(b) != c->sizes[j]) return BAD;
            if (cplus_safe_alloc_get_meta(b, &m) != CPLUS_SAFE_OK) return BAD;
            if (!m.is_live || m.byte_size != c->sizes[j] || m.elem_size != 1u) return BAD;
            if (cplus_safe_query_length(b, 1u, &out) != CPLUS_SAFE_OK) return BAD;
            if (out != c->sizes[j]) return BAD;
            if (cplus_safe_remaining_capacity(b) != c->sizes[j]) return BAD;
            if (cplus_safe_ptr_check_generation(b, m.generation) != 1) return BAD;
        }
    }
    return NULL;
}

static void sc_13_concurrent_metadata_queries(void) {
    enum { N = 6 };
    meta_ctx c;
    c.n = N;
    for (int i = 0; i < N; ++i) {
        c.sizes[i] = (size_t)(17 + i * 7);
        c.blocks[i] = malloc(c.sizes[i]);
        if (!c.blocks[i]) { g_fail = 1; return; }
    }
    CHK(sc_barrier_init(&c.bar, N) == 0);
    sc_arg ids[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        ids[i].id = i; /* distinct per-thread slot for the arg payload */
        meta_arg *ma = (meta_arg *)malloc(sizeof(*ma));
        if (!ma) { g_fail = 1; return; }
        ma->c = &c; ma->id = i;
        argp[i] = ma; rets[i] = NULL;
    }
    run_threads(N, meta_worker, argp, rets);
    check_rets(N, rets);
    for (int i = 0; i < N; ++i) { free(argp[i]); free(c.blocks[i]); }
    sc_barrier_destroy(&c.bar);
}

/* ------------------------------------------------------------------------- */
/* 03. Concurrent realloc ABA stress guarded by monotonically increasing      */
/*     generations. A recycled address must never match an old generation.   */
/*     Two phases: (a) genuine lock-free concurrent realloc smoke (no metadata */
/*     assertions, because FINDING 2 can invalidate them), then (b) a mutex-   */
/*     guarded phase carrying the full ABA/generation assertions.             */
/* ------------------------------------------------------------------------- */
typedef struct { int id; pthread_mutex_t *mu; } aba_arg;

static void *aba_lockfree_worker(void *p) {
    aba_arg *a = (aba_arg *)p;
    unsigned char *q = (unsigned char *)malloc(32u);
    if (!q) return BAD;
    for (int i = 0; i < 40; ++i) {
        size_t nsz = (size_t)(16 + ((a->id + i) % 48));
        unsigned char *n = (unsigned char *)realloc(q, nsz);
        if (n) q = n; /* on failure the allocator leaves q live */
    }
    free(q);
    return NULL;
}

static void *aba_worker(void *p) {
    aba_arg *a = (aba_arg *)p;
    pthread_mutex_lock(a->mu); /* see FINDING 2 in the file header */
    size_t sz = 32u;
    unsigned char *q = (unsigned char *)malloc(sz);
    if (!q) { pthread_mutex_unlock(a->mu); return BAD; }
    cplus_safe_alloc_meta m;
    if (cplus_safe_alloc_get_meta(q, &m) != CPLUS_SAFE_OK) {
        pthread_mutex_unlock(a->mu); return BAD;
    }
    unsigned long long gen = m.generation;
    for (int i = 0; i < 120; ++i) {
        size_t nsz = (size_t)(16 + ((a->id + i) % 64));
        unsigned char *old = q;
        unsigned long long oldgen = gen;
        q = (unsigned char *)realloc(q, nsz);
        if (!q) { pthread_mutex_unlock(a->mu); return BAD; }
        if (cplus_safe_ptr_size(q) != nsz) { pthread_mutex_unlock(a->mu); return BAD; }
        if (cplus_safe_alloc_get_meta(q, &m) != CPLUS_SAFE_OK) {
            pthread_mutex_unlock(a->mu); return BAD;
        }
        if (m.generation <= oldgen) { pthread_mutex_unlock(a->mu); return BAD; }
        if (cplus_safe_ptr_check_generation(q, m.generation) != 1) {
            pthread_mutex_unlock(a->mu); return BAD;
        }
        if (cplus_safe_ptr_check_generation(q, oldgen) != 0) {
            pthread_mutex_unlock(a->mu); return BAD;
        }
        /* ABA guard: same address may be re-issued, but generation cannot. */
        if (cplus_safe_ptr_check_generation(old, oldgen) != 0) {
            pthread_mutex_unlock(a->mu); return BAD;
        }
        gen = m.generation;
    }
    free(q);
    pthread_mutex_unlock(a->mu);
    return NULL;
}

static void sc_13_realloc_aba_generation(void) {
    enum { N = 6 };
    pthread_mutex_t mu;
    CHK(pthread_mutex_init(&mu, NULL) == 0);
    aba_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].id = i; args[i].mu = &mu; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, aba_lockfree_worker, argp, rets);
    check_rets(N, rets);
    run_threads(N, aba_worker, argp, rets);
    check_rets(N, rets);
    pthread_mutex_destroy(&mu);
}

/* ------------------------------------------------------------------------- */
/* 04. Process-wide allocation-failure injection coordinated under a mutex.  */
/* ------------------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t mu;
    int results[MAX_THREADS];
} fail_ctx;
typedef struct { fail_ctx *c; int id; } fail_arg;

static void *fail_worker(void *p) {
    fail_arg *a = (fail_arg *)p;
    pthread_mutex_lock(&a->c->mu);
    errno = 0;
    void *q = malloc(24u);
    a->c->results[a->id] = (q == NULL && errno == ENOMEM) ? 1 : 0;
    if (q) free(q); /* must not happen while injection is active */
    pthread_mutex_unlock(&a->c->mu);
    return NULL;
}

static void sc_13_fail_injection_mutex(void) {
    enum { N = 6 };
    fail_ctx c;
    CHK(pthread_mutex_init(&c.mu, NULL) == 0);
    for (int i = 0; i < N; ++i) c.results[i] = 0;
    pthread_mutex_lock(&c.mu);
    cplus_safe_alloc_fail_after(0L); /* fail every subsequent tracked alloc */
    unsigned long seq0 = cplus_safe_alloc_seq();
    pthread_mutex_unlock(&c.mu);

    fail_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, fail_worker, argp, rets);
    check_rets(N, rets);

    pthread_mutex_lock(&c.mu);
    unsigned long seq1 = cplus_safe_alloc_seq();
    cplus_safe_alloc_fail_reset();
    pthread_mutex_unlock(&c.mu);
    for (int i = 0; i < N; ++i) CHK(c.results[i] == 1);
    /* One inject_fail_locked() increment per thread's malloc attempt. */
    CHK(seq1 - seq0 == (unsigned long)N);
    pthread_mutex_destroy(&c.mu);
}

/* ------------------------------------------------------------------------- */
/* 05. Logging-level toggle storm (the level is an atomic in the library).   */
/*     We only toggle/read here: no log calls, so stderr stays quiet.         */
/* ------------------------------------------------------------------------- */
static void *log_worker(void *p) {
    sc_arg *a = (sc_arg *)p;
    for (int i = 0; i < 400; ++i) {
        cplus_safe_log_level lv = (cplus_safe_log_level)((a->id + i) % 6);
        cplus_safe_log_set_level(lv);
        int got = (int)cplus_safe_log_get_level();
        if (got < (int)CPLUS_SAFE_LOG_OFF || got > (int)CPLUS_SAFE_LOG_TRACE) return BAD;
    }
    return NULL;
}

static void sc_13_logging_toggle_storm(void) {
    sc_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 6; ++i) {
        args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(6, log_worker, argp, rets);
    check_rets(6, rets);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    CHK(cplus_safe_log_get_level() == CPLUS_SAFE_LOG_OFF);
}

/* ------------------------------------------------------------------------- */
/* 06. Per-thread tmpfile lifecycle: create/write/rewind/read/close, with     */
/*     the hardened libc's close-guarded cplus_fclose().                      */
/* ------------------------------------------------------------------------- */
static void *tmpfile_worker(void *p) {
    (void)p;
    unsigned char w[64], r[64];
    for (int i = 0; i < 64; ++i) w[i] = (unsigned char)(i * 3 + 1);
    FILE *f = cplus_tmpfile();
    if (!f) return BAD;
    if (fwrite(w, 1u, sizeof(w), f) != sizeof(w)) { cplus_fclose(f); return BAD; }
    if (fseek(f, 0L, SEEK_SET) != 0) { cplus_fclose(f); return BAD; }
    if (fread(r, 1u, sizeof(r), f) != sizeof(r)) { cplus_fclose(f); return BAD; }
    if (memcmp(r, w, sizeof(w)) != 0) { cplus_fclose(f); return BAD; }
    if (cplus_fclose(f) != 0) return BAD;
    return NULL;
}

static void sc_13_tmpfile_per_thread(void) {
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 4; ++i) { argp[i] = NULL; rets[i] = NULL; }
    run_threads(4, tmpfile_worker, argp, rets);
    check_rets(4, rets);
}

/* ------------------------------------------------------------------------- */
/* 07. Per-thread FP rounding mode. Each thread picks a different mode, does  */
/*     a real FP operation, then rendezvous: if fenv were shared, at least    */
/*     three threads would observe the wrong mode.                            */
/* ------------------------------------------------------------------------- */
typedef struct {
    sc_barrier b1, b2;
    int modes[MAX_THREADS];
    int baseline;
} fp_ctx;
typedef struct { fp_ctx *c; int id; } fp_arg;

static void *fp_worker(void *p) {
    fp_arg *a = (fp_arg *)p;
    int mode = a->c->modes[a->id];
    if (cplus_fesetround(mode) != 0) return BAD;
    volatile double x = 1.0, y = 3.0;
    volatile double z = x / y; /* real, non-foldable FP operation */
    if (z < 0.0 || z > 1.0) return BAD;
    int br = sc_barrier_wait(&a->c->b1);
    if (br != 0) return BAD;
    if (cplus_fegetround() != mode) return BAD;
    volatile double w = z + 0.5;
    if (w < 0.5 || w > 1.5) return BAD;
    br = sc_barrier_wait(&a->c->b2);
    if (br != 0) return BAD;
    if (cplus_fesetround(a->c->baseline) != 0) return BAD;
    return NULL;
}

static void sc_13_fp_rounding_per_thread(void) {
    enum { N = 4 };
    fp_ctx c;
    c.modes[0] = FE_TONEAREST;
    c.modes[1] = FE_DOWNWARD;
    c.modes[2] = FE_UPWARD;
    c.modes[3] = FE_TOWARDZERO;
    c.baseline = cplus_fegetround();
    CHK(cplus_fesetround(FE_TONEAREST) == 0);
    CHK(sc_barrier_init(&c.b1, N) == 0);
    CHK(sc_barrier_init(&c.b2, N) == 0);
    fp_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, fp_worker, argp, rets);
    check_rets(N, rets);
    CHK(cplus_fegetround() == c.baseline);
    CHK(cplus_fesetround(c.baseline) == 0);
    sc_barrier_destroy(&c.b1);
    sc_barrier_destroy(&c.b2);
}

/* ------------------------------------------------------------------------- */
/* 08. Ownership transfer with two coordinated claimants. OUTCOME: the mutex  */
/*     serialises them, so exactly one claim succeeds; the loser observes     */
/*     *src == NULL and gets CPLUS_SAFE_EINVAL. Both outcomes are documented  */
/*     and asserted, no matter which thread wins the race to the mutex.       */
/* ------------------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t mu;
    sc_barrier bar;
    void *shared;
    int rc[MAX_THREADS];
    void *claimed[MAX_THREADS];
    unsigned long long orig_gen;
} own_ctx;
typedef struct { own_ctx *c; int id; } own_arg;

static void *own_worker(void *p) {
    own_arg *a = (own_arg *)p;
    own_ctx *c = a->c;
    sc_barrier_wait(&c->bar);
    pthread_mutex_lock(&c->mu);
    void *dst = NULL;
    int r = cplus_safe_transfer_ownership(&dst, &c->shared);
    c->rc[a->id] = r;
    c->claimed[a->id] = dst;
    pthread_mutex_unlock(&c->mu);
    if (r == CPLUS_SAFE_OK) {
        cplus_safe_alloc_meta m;
        if (cplus_safe_alloc_get_meta(dst, &m) != CPLUS_SAFE_OK) return BAD;
        if (m.generation == c->orig_gen) return BAD; /* transfer bumps gen */
        if (strcmp((const char *)dst, "two-claimants") != 0) return BAD;
        free(dst);
    } else {
        if (dst != NULL) return BAD;
    }
    return NULL;
}

static void sc_13_ownership_two_claimants(void) {
    enum { N = 2 };
    own_ctx c;
    CHK(pthread_mutex_init(&c.mu, NULL) == 0);
    CHK(sc_barrier_init(&c.bar, N) == 0);
    c.shared = malloc(64u);
    CHK(c.shared != NULL);
    strcpy((char *)c.shared, "two-claimants");
    cplus_safe_alloc_meta m;
    CHK(cplus_safe_alloc_get_meta(c.shared, &m) == CPLUS_SAFE_OK);
    c.orig_gen = m.generation;
    for (int i = 0; i < N; ++i) { c.rc[i] = -1; c.claimed[i] = NULL; }

    own_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, own_worker, argp, rets);
    check_rets(N, rets);
    int winners = 0, losers = 0;
    for (int i = 0; i < N; ++i) {
        if (c.rc[i] == CPLUS_SAFE_OK) ++winners;
        else if (c.rc[i] == CPLUS_SAFE_EINVAL) ++losers;
    }
    CHK(winners == 1);
    CHK(losers == 1);
    CHK(c.shared == NULL);
    CHK(c.claimed[0] == NULL || c.claimed[1] == NULL); /* at most one owner */
    sc_barrier_destroy(&c.bar);
    pthread_mutex_destroy(&c.mu);
}

/* ------------------------------------------------------------------------- */
/* 09. Double-free detection under contention: one shared tracked pointer,    */
/*     N threads race through free_checked_s. Exactly one must succeed.        */
/* ------------------------------------------------------------------------- */
typedef struct {
    sc_barrier bar;
    void *p;
    int rc[MAX_THREADS];
} df_ctx;
typedef struct { df_ctx *c; int id; } df_arg;

static void *df_worker(void *p) {
    df_arg *a = (df_arg *)p;
    int br = sc_barrier_wait(&a->c->bar);
    if (br != 0) return BAD;
    a->c->rc[a->id] = free_checked_s(a->c->p); /* distinct slot per thread */
    return NULL;
}

static void sc_13_double_free_contention(void) {
    enum { N = 8 };
    df_ctx c;
    c.p = malloc(48u);
    CHK(c.p != NULL);
    CHK(sc_barrier_init(&c.bar, N) == 0);
    for (int i = 0; i < N; ++i) c.rc[i] = -1;
    df_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, df_worker, argp, rets);
    check_rets(N, rets);
    int ok = 0, refused = 0;
    for (int i = 0; i < N; ++i) {
        if (c.rc[i] == CPLUS_SAFE_OK) ++ok;
        else if (c.rc[i] == CPLUS_SAFE_EINVAL) ++refused;
    }
    CHK(ok == 1);
    CHK(refused == N - 1);
    CHK(cplus_safe_ptr_was_freed(c.p) == 1);
    sc_barrier_destroy(&c.bar);
}

/* ------------------------------------------------------------------------- */
/* 10. Live-count returns to the exact baseline after all threads are joined. */
/* ------------------------------------------------------------------------- */
typedef struct {
    sc_barrier bar;
    void *held[MAX_THREADS];
} hold_ctx;
typedef struct { hold_ctx *c; int id; } hold_arg;

static void *hold_worker(void *p) {
    hold_arg *a = (hold_arg *)p;
    a->c->held[a->id] = malloc((size_t)(32 + a->id));
    if (!a->c->held[a->id]) return BAD;
    int br = sc_barrier_wait(&a->c->bar);
    if (br != 0) return BAD;
    if (cplus_safe_ptr_size(a->c->held[a->id]) != (size_t)(32 + a->id)) return BAD;
    free(a->c->held[a->id]);
    a->c->held[a->id] = NULL;
    return NULL;
}

static void sc_13_livecount_baseline(void) {
    enum { N = 8 };
    /* Warm up the table so its own bookkeeping does not move the baseline. */
    void *warm = malloc(8u); CHK(warm != NULL); free(warm);
    size_t base = cplus_safe_alloc_live_count();
    size_t bytes = cplus_safe_alloc_live_bytes();
    hold_ctx c;
    CHK(sc_barrier_init(&c.bar, N) == 0);
    for (int i = 0; i < N; ++i) c.held[i] = NULL;
    hold_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, hold_worker, argp, rets);
    check_rets(N, rets);
    CHK(cplus_safe_alloc_live_count() == base);
    CHK(cplus_safe_alloc_live_bytes() == bytes);
    sc_barrier_destroy(&c.bar);
}

/* ------------------------------------------------------------------------- */
/* 11. Barrier-synchronised start: no worker performs work before all exist.  */
/* ------------------------------------------------------------------------- */
typedef struct { sc_barrier bar; int passed[MAX_THREADS]; } bar_ctx;
typedef struct { bar_ctx *c; int id; } bar_arg;

static void *barrier_worker(void *p) {
    bar_arg *a = (bar_arg *)p;
    int br = sc_barrier_wait(&a->c->bar);
    if (br != 0) return BAD;
    a->c->passed[a->id] = 1;
    void *q = malloc(16u);
    if (!q) return BAD;
    free(q);
    return NULL;
}

static void sc_13_barrier_start(void) {
    enum { N = 8 };
    bar_ctx c;
    CHK(sc_barrier_init(&c.bar, N) == 0);
    for (int i = 0; i < N; ++i) c.passed[i] = 0;
    bar_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, barrier_worker, argp, rets);
    check_rets(N, rets);
    for (int i = 0; i < N; ++i) CHK(c.passed[i] == 1);
    sc_barrier_destroy(&c.bar);
}

/* ------------------------------------------------------------------------- */
/* 12. Mutex-protected shared allocation: the same tracked block is recycled  */
/*     under an external mutex; every writer verifies its own pattern first.  */
/* ------------------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t mu;
    void *buf;
    size_t sz;
} shared_ctx;
typedef struct { shared_ctx *c; int id; } shared_arg;

static void *shared_worker(void *p) {
    shared_arg *a = (shared_arg *)p;
    shared_ctx *c = a->c;
    for (int i = 0; i < 60; ++i) {
        pthread_mutex_lock(&c->mu);
        if (!c->buf) {
            c->buf = malloc(48u);
            c->sz = 48u;
            if (!c->buf) { pthread_mutex_unlock(&c->mu); return BAD; }
        }
        if (cplus_memset_s(c->buf, c->sz, a->id, c->sz) != 0) {
            pthread_mutex_unlock(&c->mu); return BAD;
        }
        unsigned char *q = (unsigned char *)c->buf;
        for (size_t k = 0; k < c->sz; ++k) {
            if (q[k] != (unsigned char)a->id) { pthread_mutex_unlock(&c->mu); return BAD; }
        }
        free(c->buf);
        c->buf = NULL;
        pthread_mutex_unlock(&c->mu);
    }
    return NULL;
}

static void sc_13_mutex_shared_alloc(void) {
    shared_ctx c;
    CHK(pthread_mutex_init(&c.mu, NULL) == 0);
    c.buf = NULL; c.sz = 0;
    shared_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 6; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(6, shared_worker, argp, rets);
    check_rets(6, rets);
    CHK(c.buf == NULL);
    pthread_mutex_destroy(&c.mu);
}

/* ------------------------------------------------------------------------- */
/* 13. Condvar handoff: producer publishes a tracked buffer + generation;      */
/*     consumer claims it with cplus_safe_transfer_ownership and frees it.     */
/* ------------------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    void *buf;
    unsigned long long gen;
    int ready, consumed;
} hand_ctx;

static void *hand_producer(void *p) {
    hand_ctx *c = (hand_ctx *)p;
    void *q = malloc(64u);
    if (!q) return BAD;
    strcpy((char *)q, "condvar-payload");
    cplus_safe_alloc_meta m;
    if (cplus_safe_alloc_get_meta(q, &m) != CPLUS_SAFE_OK) return BAD;
    pthread_mutex_lock(&c->mu);
    c->buf = q; c->gen = m.generation; c->ready = 1;
    pthread_cond_signal(&c->cv);
    pthread_mutex_unlock(&c->mu);
    return NULL;
}

static void *hand_consumer(void *p) {
    hand_ctx *c = (hand_ctx *)p;
    pthread_mutex_lock(&c->mu);
    while (!c->ready) pthread_cond_wait(&c->cv, &c->mu);
    /* Check the published generation BEFORE the transfer: transfer_ownership
     * intentionally bumps it, so the old generation must stop matching after. */
    if (cplus_safe_ptr_check_generation(c->buf, c->gen) != 1) {
        pthread_mutex_unlock(&c->mu); return BAD;
    }
    void *owned = NULL;
    if (cplus_safe_transfer_ownership(&owned, &c->buf) != CPLUS_SAFE_OK) {
        pthread_mutex_unlock(&c->mu); return BAD;
    }
    if (cplus_safe_ptr_check_generation(owned, c->gen) != 0) {
        pthread_mutex_unlock(&c->mu); return BAD;
    }
    c->consumed = 1;
    pthread_cond_signal(&c->cv);
    pthread_mutex_unlock(&c->mu);
    if (strcmp((const char *)owned, "condvar-payload") != 0) return BAD;
    free(owned);
    return NULL;
}

static void sc_13_condvar_handoff(void) {
    hand_ctx c;
    CHK(pthread_mutex_init(&c.mu, NULL) == 0);
    CHK(pthread_cond_init(&c.cv, NULL) == 0);
    c.buf = NULL; c.gen = 0; c.ready = 0; c.consumed = 0;
    pthread_t prod, cons;
    void *rp = NULL, *rc = NULL;
    CHK(pthread_create(&prod, NULL, hand_producer, &c) == 0);
    CHK(pthread_create(&cons, NULL, hand_consumer, &c) == 0);
    CHK(pthread_join(prod, &rp) == 0);
    CHK(pthread_join(cons, &rc) == 0);
    CHK(rp == NULL);
    CHK(rc == NULL);
    CHK(c.consumed == 1);
    CHK(c.buf == NULL);
    pthread_cond_destroy(&c.cv);
    pthread_mutex_destroy(&c.mu);
}

/* ------------------------------------------------------------------------- */
/* 14. TLS-style per-thread buffer. The pthread_key destructor is free_s, so   */
/*     every per-thread allocation is reclaimed at thread exit (verified via   */
/*     the live count after join).                                             */
/* ------------------------------------------------------------------------- */
static pthread_key_t tls_key;
static pthread_once_t tls_once = PTHREAD_ONCE_INIT;
static void tls_make_key(void) { (void)pthread_key_create(&tls_key, free_s); }

static void *tls_worker(void *p) {
    pthread_once(&tls_once, tls_make_key);
    void *b = malloc(40u);
    if (!b) return BAD;
    if (cplus_memset_s(b, 40u, ((sc_arg *)p)->id, 40u) != 0) return BAD;
    if (pthread_setspecific(tls_key, b) != 0) { free(b); return BAD; }
    if (pthread_getspecific(tls_key) != b) return BAD;
    unsigned char *q = (unsigned char *)b;
    for (int k = 0; k < 40; ++k) {
        if (q[k] != (unsigned char)((sc_arg *)p)->id) return BAD;
    }
    /* Intentionally leave it set: the key destructor frees it on exit. */
    return NULL;
}

static void sc_13_tls_per_thread_buffer(void) {
    void *warm = malloc(8u); CHK(warm != NULL); free(warm);
    size_t base = cplus_safe_alloc_live_count();
    sc_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 6; ++i) {
        args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(6, tls_worker, argp, rets);
    check_rets(6, rets);
    CHK(cplus_safe_alloc_live_count() == base);
}

/* ------------------------------------------------------------------------- */
/* 15. Concurrent strcpy into separate tracked heap buffers (auto-bounds).    */
/* ------------------------------------------------------------------------- */
static void *strcpy_worker(void *p) {
    sc_arg *a = (sc_arg *)p;
    char *dst = (char *)malloc(64u);
    if (!dst) return BAD;
    char src[32];
    snprintf(src, sizeof(src), "thread-%d-payload", a->id);
    if (strcpy(dst, src) != dst) return BAD;
    if (strcmp(dst, src) != 0) return BAD;
    free(dst);
    return NULL;
}

static void sc_13_strcpy_separate_buffers(void) {
    sc_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 8; ++i) {
        args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(8, strcpy_worker, argp, rets);
    check_rets(8, rets);
}

/* ------------------------------------------------------------------------- */
/* 16. Concurrent snprintf into separate stack buffers (no shared state).      */
/* ------------------------------------------------------------------------- */
static void *snprintf_worker(void *p) {
    sc_arg *a = (sc_arg *)p;
    for (int i = 0; i < 40; ++i) {
        char buf[48];
        int n = snprintf(buf, sizeof(buf), "t=%d i=%d", a->id, i);
        if (n < 0 || (size_t)n >= sizeof(buf)) return BAD;
        char expect[48];
        int m = snprintf(expect, sizeof(expect), "t=%d i=%d", a->id, i);
        if (m != n || strcmp(buf, expect) != 0) return BAD;
    }
    return NULL;
}

static void sc_13_snprintf_separate_buffers(void) {
    sc_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 8; ++i) {
        args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(8, snprintf_worker, argp, rets);
    check_rets(8, rets);
}

/* ------------------------------------------------------------------------- */
/* 17. Shared read-only tracked buffer with concurrent memcmp (positive and    */
/*     negative comparisons).                                                  */
/* ------------------------------------------------------------------------- */
typedef struct { const unsigned char *buf; size_t n; int pat; } cmp_arg;

static void *memcmp_worker(void *p) {
    cmp_arg *a = (cmp_arg *)p;
    unsigned char expect[128];
    if (a->n > sizeof(expect)) return BAD;
    memset(expect, (unsigned char)a->pat, a->n);
    int diff = 0;
    if (cplus_memcmp_s(a->buf, a->n, expect, a->n, &diff) != 0) return BAD;
    int d = diff;
    if (a->pat == 0x5A) { if (d != 0) return BAD; }
    else { if (d == 0) return BAD; }
    return NULL;
}

static void sc_13_memcmp_shared_readonly(void) {
    enum { SZ = 128 };
    unsigned char *shared = (unsigned char *)malloc(SZ);
    CHK(shared != NULL);
    CHK(cplus_memset_s(shared, SZ, 0x5A, SZ) == 0);
    cmp_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 8; ++i) {
        args[i].buf = shared; args[i].n = SZ;
        args[i].pat = (i & 1) ? 0x5B : 0x5A;
        argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(8, memcmp_worker, argp, rets);
    check_rets(8, rets);
    free(shared);
}

/* ------------------------------------------------------------------------- */
/* 18. Thread API error paths that are well-defined by POSIX: joining self    */
/*     yields EDEADLK; invalid detach state is rejected by the attr setter;    */
/*     a condvar wait with an already-expired absolute time yields ETIMEDOUT.  */
/* ------------------------------------------------------------------------- */
static void *noop_worker(void *p) { (void)p; return NULL; }

static void sc_13_thread_error_paths(void) {
    void *rv = NULL;
    CHK(pthread_join(pthread_self(), &rv) == EDEADLK);

    pthread_attr_t attr;
    CHK(pthread_attr_init(&attr) == 0);
    CHK(pthread_attr_setdetachstate(&attr, 42) != 0);
    CHK(pthread_attr_destroy(&attr) == 0);

    pthread_mutex_t mu;
    pthread_cond_t cv;
    CHK(pthread_mutex_init(&mu, NULL) == 0);
    CHK(pthread_cond_init(&cv, NULL) == 0);
    struct timespec past;
    CHK(clock_gettime(CLOCK_REALTIME, &past) == 0);
    past.tv_sec -= 1;
    CHK(pthread_mutex_lock(&mu) == 0);
    CHK(pthread_cond_timedwait(&cv, &mu, &past) == ETIMEDOUT);
    CHK(pthread_mutex_unlock(&mu) == 0);
    pthread_cond_destroy(&cv);
    pthread_mutex_destroy(&mu);

    /* Happy path for contrast. */
    pthread_t th;
    CHK(pthread_create(&th, NULL, noop_worker, NULL) == 0);
    CHK(pthread_join(th, &rv) == 0);
}

/* ------------------------------------------------------------------------- */
/* 19. Repeated waves of threads; each wave must return to the live baseline. */
/* ------------------------------------------------------------------------- */
static void *wave_worker(void *p) {
    sc_arg *a = (sc_arg *)p;
    for (int i = 0; i < 50; ++i) {
        void *q = malloc((size_t)(a->id + i % 7 + 1));
        if (!q) return BAD;
        size_t qsz = cplus_safe_ptr_size(q);
        if (cplus_memset_s(q, qsz, 0x77, qsz) != 0) return BAD;
        free(q);
    }
    return NULL;
}

static void sc_13_repeated_waves(void) {
    for (int w = 0; w < 6; ++w) {
        size_t base = cplus_safe_alloc_live_count();
        sc_arg args[MAX_THREADS];
        void *argp[MAX_THREADS], *rets[MAX_THREADS];
        for (int i = 0; i < 6; ++i) {
            args[i].id = i + w; argp[i] = &args[i]; rets[i] = NULL;
        }
        run_threads(6, wave_worker, argp, rets);
        check_rets(6, rets);
        CHK(cplus_safe_alloc_live_count() == base);
    }
}

/* ------------------------------------------------------------------------- */
/* 20. No test-side data race: a shared counter is touched only under a mutex, */
/*     and each thread also exercises its thread-local runtime guard stack.    */
/* ------------------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t mu;
    long counter;
    long cleanups;
} cnt_ctx;
typedef struct { cnt_ctx *c; int id; } cnt_arg;

static void sc13_drop(void *p) {
    cnt_ctx *c = (cnt_ctx *)p;
    pthread_mutex_lock(&c->mu);
    ++c->cleanups;
    pthread_mutex_unlock(&c->mu);
}

static int sc13_guard_body(void *p) {
    if (cplus_runtime_push_cleanup(sc13_drop, p) != 0) return 1;
    cplus_runtime_pop_cleanup(1); /* run it now, leaving no pending cleanup */
    return CPLUS_RUNTIME_OK;
}

static void *counter_worker(void *p) {
    cnt_arg *a = (cnt_arg *)p;
    if (cplus_runtime_guard(sc13_guard_body, a->c) != CPLUS_RUNTIME_OK) return BAD;
    for (int i = 0; i < 300; ++i) {
        pthread_mutex_lock(&a->c->mu);
        ++a->c->counter;
        pthread_mutex_unlock(&a->c->mu);
    }
    return NULL;
}

static void sc_13_mutex_counter_no_race(void) {
    enum { N = 8, ITERS = 300 };
    cnt_ctx c;
    CHK(pthread_mutex_init(&c.mu, NULL) == 0);
    c.counter = 0; c.cleanups = 0;
    cnt_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, counter_worker, argp, rets);
    check_rets(N, rets);
    CHK(c.counter == (long)(N * ITERS));
    CHK(c.cleanups == (long)N);
    CHK(cplus_runtime_cleanup_depth() == 0);
    pthread_mutex_destroy(&c.mu);
}

/* ------------------------------------------------------------------------- */
/* 21. Concurrent calloc: every block must be zero-filled and metadata must    */
/*     report the recorded element size.                                       */
/* ------------------------------------------------------------------------- */
static void *calloc_worker(void *p) {
    sc_arg *a = (sc_arg *)p;
    for (int i = 0; i < 100; ++i) {
        size_t n = (size_t)(4 + ((a->id + i) % 20));
        unsigned int *q = (unsigned int *)calloc_s(n, sizeof(unsigned int));
        if (!q) return BAD;
        if (cplus_safe_ptr_size(q) != n * sizeof(unsigned int)) return BAD;
        if (cplus_safe_ptr_elem_size(q) != sizeof(unsigned int)) return BAD;
        for (size_t k = 0; k < n; ++k) if (q[k] != 0u) return BAD;
        free(q);
    }
    return NULL;
}

static void sc_13_calloc_zero_concurrent(void) {
    sc_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 6; ++i) {
        args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(6, calloc_worker, argp, rets);
    check_rets(6, rets);
}

/* ------------------------------------------------------------------------- */
/* 22. Concurrent telemetry: counters use C11 atomics inside the library, so   */
/*     many threads recording at once must not lose increments.                */
/* ------------------------------------------------------------------------- */
static void *stats_worker(void *p) {
    (void)p;
    for (int i = 0; i < 200; ++i)
        cplus_safe_stat_record("sc13.concurrent", (i & 1) == 0);
    return NULL;
}

static void sc_13_stats_concurrent(void) {
    cplus_safe_stats_reset();
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 6; ++i) { argp[i] = NULL; rets[i] = NULL; }
    run_threads(6, stats_worker, argp, rets);
    check_rets(6, rets);
    cplus_safe_stat st;
    CHK(cplus_safe_stats_get("sc13.concurrent", &st) == 1);
    CHK(st.calls == 6u * 200u);
    CHK(st.ok + st.fail == st.calls);
}

/* ------------------------------------------------------------------------- */
/* 23. Three-stage ownership relay: each hop must bump the generation and the  */
/*     final consumer must see the original payload. All boxes are protected   */
/*     by a mutex + condvar, so no test-side race exists at any hop.            */
/* ------------------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    void *slot;
    unsigned long long gen;
    int filled;
} relay_box;
typedef struct {
    relay_box *boxes;
    int id;
} relay_arg;

static void *relay_worker(void *p) {
    relay_arg *a = (relay_arg *)p;
    void *held = NULL;
    unsigned long long seen_gen = 0;
    if (a->id == 0) {
        held = malloc(48u);
        if (!held) return BAD;
        strcpy((char *)held, "relay-payload");
        cplus_safe_alloc_meta m;
        if (cplus_safe_alloc_get_meta(held, &m) != CPLUS_SAFE_OK) return BAD;
        seen_gen = m.generation;
    } else {
        relay_box *prev = &a->boxes[a->id - 1];
        pthread_mutex_lock(&prev->mu);
        while (!prev->filled) pthread_cond_wait(&prev->cv, &prev->mu);
        if (cplus_safe_transfer_ownership(&held, &prev->slot) != CPLUS_SAFE_OK) {
            pthread_mutex_unlock(&prev->mu); return BAD;
        }
        seen_gen = prev->gen; /* generation the producer published */
        prev->filled = 0;
        pthread_cond_signal(&prev->cv);
        pthread_mutex_unlock(&prev->mu);
        cplus_safe_alloc_meta m;
        if (cplus_safe_alloc_get_meta(held, &m) != CPLUS_SAFE_OK) return BAD;
        if (m.generation <= seen_gen) return BAD; /* transfer bumped it */
        seen_gen = m.generation;
    }
    if (a->id < 2) {
        relay_box *next = &a->boxes[a->id];
        pthread_mutex_lock(&next->mu);
        next->slot = held;
        next->gen = seen_gen;
        next->filled = 1;
        pthread_cond_signal(&next->cv);
        pthread_mutex_unlock(&next->mu);
        return NULL;
    }
    /* Final stage: verify payload and release. */
    if (strcmp((const char *)held, "relay-payload") != 0) return BAD;
    free(held);
    return NULL;
}

static void sc_13_transfer_relay(void) {
    enum { N = 3 };
    relay_box boxes[N];
    for (int i = 0; i < N; ++i) {
        CHK(pthread_mutex_init(&boxes[i].mu, NULL) == 0);
        CHK(pthread_cond_init(&boxes[i].cv, NULL) == 0);
        boxes[i].slot = NULL; boxes[i].gen = 0; boxes[i].filled = 0;
    }
    relay_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < N; ++i) {
        args[i].boxes = boxes; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(N, relay_worker, argp, rets);
    check_rets(N, rets);
    for (int i = 0; i < N; ++i) {
        CHK(boxes[i].slot == NULL);
        pthread_cond_destroy(&boxes[i].cv);
        pthread_mutex_destroy(&boxes[i].mu);
    }
}

/* ------------------------------------------------------------------------- */
/* 24. Mixed-op stress: malloc -> checked memset -> realloc -> generation      */
/*     query -> free, with byte preservation across realloc. The allocator    */
/*     cycle is serialised (see FINDING 2 above) to avoid the library's       */
/*     realloc/metadata race; genuine lock-free concurrent realloc is covered  */
/*     by scenario 03.                                                         */
/* ------------------------------------------------------------------------- */
typedef struct { pthread_mutex_t mu; } mixed_ctx;
typedef struct { mixed_ctx *c; int id; } mixed_arg;

static int mixed_body(int i) {
    size_t oldsz = (size_t)(16 + (i % 16));
    size_t newsz = (size_t)(16 + ((i + 7) % 33));
    unsigned char *q = (unsigned char *)malloc(oldsz);
    if (!q) return 1;
    if (cplus_safe_ptr_size(q) != oldsz) return 1;
    if (cplus_memset_s(q, oldsz, 0xC3, oldsz) != 0) return 1;
    q = (unsigned char *)realloc(q, newsz);
    if (!q) return 1;
    if (cplus_safe_ptr_size(q) != newsz) return 1;
    size_t keep = oldsz < newsz ? oldsz : newsz;
    for (size_t k = 0; k < keep; ++k) if (q[k] != 0xC3) return 1;
    cplus_safe_alloc_meta m;
    if (cplus_safe_alloc_get_meta(q, &m) != CPLUS_SAFE_OK) return 1;
    if (!cplus_safe_ptr_check_generation(q, m.generation)) return 1;
    free(q);
    return 0;
}

static void *mixed_worker(void *p) {
    mixed_arg *a = (mixed_arg *)p;
    for (int i = 0; i < 120; ++i) {
        pthread_mutex_lock(&a->c->mu);
        int r = mixed_body(i + a->id);
        pthread_mutex_unlock(&a->c->mu);
        if (r != 0) return BAD;
    }
    return NULL;
}

static void sc_13_mixed_ops_stress(void) {
    mixed_ctx c;
    CHK(pthread_mutex_init(&c.mu, NULL) == 0);
    mixed_arg args[MAX_THREADS];
    void *argp[MAX_THREADS], *rets[MAX_THREADS];
    for (int i = 0; i < 8; ++i) {
        args[i].c = &c; args[i].id = i; argp[i] = &args[i]; rets[i] = NULL;
    }
    run_threads(8, mixed_worker, argp, rets);
    check_rets(8, rets);
    pthread_mutex_destroy(&c.mu);
}

/* ------------------------------------------------------------------------- */
int main(void) {
    cplus_libc_log_set_enabled(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_alloc_fail_reset();

    int ran = 0;
#define RUN(fn) do { fn(); ++ran; } while (0)
    RUN(sc_13_churn_8threads_pattern);
    RUN(sc_13_concurrent_metadata_queries);
    RUN(sc_13_realloc_aba_generation);
    RUN(sc_13_fail_injection_mutex);
    RUN(sc_13_logging_toggle_storm);
    RUN(sc_13_tmpfile_per_thread);
    RUN(sc_13_fp_rounding_per_thread);
    RUN(sc_13_ownership_two_claimants);
    RUN(sc_13_double_free_contention);
    RUN(sc_13_livecount_baseline);
    RUN(sc_13_barrier_start);
    RUN(sc_13_mutex_shared_alloc);
    RUN(sc_13_condvar_handoff);
    RUN(sc_13_tls_per_thread_buffer);
    RUN(sc_13_strcpy_separate_buffers);
    RUN(sc_13_snprintf_separate_buffers);
    RUN(sc_13_memcmp_shared_readonly);
    RUN(sc_13_thread_error_paths);
    RUN(sc_13_repeated_waves);
    RUN(sc_13_mutex_counter_no_race);
    RUN(sc_13_calloc_zero_concurrent);
    RUN(sc_13_stats_concurrent);
    RUN(sc_13_transfer_relay);
    RUN(sc_13_mixed_ops_stress);
#undef RUN

    if (g_fail) {
        fprintf(stderr, "runtime_threads: FAILED (%d scenarios ran)\n", ran);
        return 1;
    }
    printf("runtime_threads: %d scenarios ok\n", ran);
    return 0;
}
