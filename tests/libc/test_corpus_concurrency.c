/* Adversarial concurrency corpus for the C+ hardened libc.
 *
 * Standalone C99 + pthreads; every byte of state
 * shared between threads in THIS test is protected by a mutex, a condition
 * variable, or a join/barrier edge. The stress comes from controlled
 * interleavings at the barriers and at the mutex-protected phase transitions
 * below -- the harness itself never introduces a data race or UB.
 *
 * Scenarios (all counts are deterministic):
 *   1. 8 threads x 500 malloc/write/realloc/free  = 4000 ops
 *   2. 8 threads x 200 alloc + metadata queries    = 1600 ops
 *   3. 4 log togglers x 200 + 4 instrumented x 200 = 1600 ops
 *   4. 8 threads x 25 per-thread tmpfile lifecycles = 200 ops
 *   5. 8 threads x 200 per-thread FP-rounding calls = 1600 ops
 *   6. 4 alloc workers x 300 + 3 injected phases   = 1203 ops
 *   ------------------------------------------------------------------
 *   TOTAL                                           10203 ops
 *
 * Each defect is reported as a "FINDING: ..." line with the exact scenario,
 * the offending thread / iteration, and the synchronization interleaving that
 * exposed it. Like the other adversarial corpora, the process returns 0 so an
 * entire corpus run completes even when a finding is printed.
 */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <assert.h>
#include <stdint.h>

#include <errno.h>
#include <fenv.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "install-include/cplus_libc.h"

/* ------------------------------------------------------------------ utils */

static pthread_mutex_t g_report_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_findings = 0;

static void finding(const char *fmt, ...) {
    va_list ap;
    pthread_mutex_lock(&g_report_lock);
    ++g_findings;
    printf("FINDING: ");
    va_start(ap, fmt);
    (void)vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_report_lock);
}

/* macOS does not expose pthread_barrier_t; use it on Linux and fall back to a
 * generation-counted mutex/condvar barrier elsewhere (still race-free). */
#if defined(__linux__) && !defined(__ANDROID__)
#define CPLUS_USE_PTHREAD_BARRIER 1
#else
#define CPLUS_USE_PTHREAD_BARRIER 0
#endif

typedef struct {
#if CPLUS_USE_PTHREAD_BARRIER
    pthread_barrier_t b;
#else
    pthread_mutex_t m;
    pthread_cond_t cv;
    unsigned n, generation, waiting;
#endif
} cor_barrier_t;

static void cor_barrier_init(cor_barrier_t *b, unsigned n) {
#if CPLUS_USE_PTHREAD_BARRIER
    (void)pthread_barrier_init(&b->b, NULL, n);
#else
    assert(pthread_mutex_init(&b->m, NULL) == 0);
    assert(pthread_cond_init(&b->cv, NULL) == 0);
    b->n = n;
    b->generation = 0u;
    b->waiting = 0u;
#endif
}

static void cor_barrier_wait(cor_barrier_t *b) {
#if CPLUS_USE_PTHREAD_BARRIER
    (void)pthread_barrier_wait(&b->b);
#else
    pthread_mutex_lock(&b->m);
    {
        unsigned gen = b->generation;
        if (++b->waiting == b->n) {
            b->waiting = 0u;
            ++b->generation;
            pthread_cond_broadcast(&b->cv);
        } else {
            while (gen == b->generation)
                pthread_cond_wait(&b->cv, &b->m);
        }
    }
    pthread_mutex_unlock(&b->m);
#endif
}

static void cor_barrier_destroy(cor_barrier_t *b) {
#if CPLUS_USE_PTHREAD_BARRIER
    (void)pthread_barrier_destroy(&b->b);
#else
    (void)pthread_cond_destroy(&b->cv);
    (void)pthread_mutex_destroy(&b->m);
#endif
}

/* A worker owns its slot exclusively until join; main reads it only after
 * joining, so no field below is ever touched by two threads at once. */
typedef struct {
    int id;
    uint64_t ops;
    uint64_t failures;
    int detail;
    cor_barrier_t *barrier;
    void *shared;
} cor_worker_t;

#define NW 8

/* =============================================================== SCENARIO 1
 * 8 x 500 malloc -> write -> realloc -> free. Per-thread fill patterns must
 * survive the realloc; live_count/live_bytes must return exactly to baseline.
 * Interleaving: all 8 workers rendezvous at a start barrier, then churn the
 * shared hardened allocator concurrently with no further synchronization.
 */
static void *s1_worker(void *arg) {
    cor_worker_t *w = (cor_worker_t *)arg;
    unsigned char pat = (unsigned char)(w->id * 37 + 11);
    unsigned char pat2 = (unsigned char)(pat + 1u);
    int i;
    cor_barrier_wait(w->barrier);
    for (i = 0; i < 500; ++i) {
        size_t s1 = 32u + (size_t)((i * 7 + w->id) % 128u);
        size_t s2 = 16u + (size_t)((i * 13 + w->id * 3) % 256u);
        size_t common, k;
        unsigned char *p = (unsigned char *)cplus_malloc(s1);
        unsigned char *q;
        if (!p) {
            ++w->failures;
            ++w->ops;
            continue;
        }
        memset(p, pat, s1);
        for (k = 0; k < s1; ++k)
            if (p[k] != pat) { ++w->failures; break; }
        errno = 0;
        q = (unsigned char *)cplus_realloc(p, s2);
        if (!q) {
            ++w->failures;
            /* Distinguish a plain ENOMEM from a lost metadata slot: if the
             * just-allocated p is no longer live with EINVAL, a concurrent
             * realloc in another thread stole its side-table entry (ABA on the
             * old address released by the host realloc between the two
             * g_meta_lock critical sections in cplus_realloc). */
            if (errno == EINVAL && !cplus_alloc_is_live(p))
                ++w->detail;
            cplus_free(p);
            ++w->ops;
            continue;
        }
        common = s1 < s2 ? s1 : s2;
        for (k = 0; k < common; ++k)
            if (q[k] != pat) { ++w->failures; break; }
        for (k = common; k < s2; ++k) q[k] = pat2;
        for (k = common; k < s2; ++k)
            if (q[k] != pat2) { ++w->failures; break; }
        cplus_free(q);
        ++w->ops;
    }
    return NULL;
}

static uint64_t scenario1(void) {
    cor_worker_t w[NW];
    pthread_t t[NW];
    cor_barrier_t b;
    size_t base_live, base_bytes, end_live, end_bytes;
    unsigned long df0, inf0, df1, inf1;
    uint64_t ops = 0, failures = 0, meta_loss = 0;
    int i;
    memset(w, 0, sizeof(w));
    cor_barrier_init(&b, NW);
    for (i = 0; i < NW; ++i) { w[i].id = i; w[i].barrier = &b; }
    base_live = cplus_alloc_live_count();
    base_bytes = cplus_alloc_live_bytes();
    df0 = cplus_alloc_double_free_count();
    inf0 = cplus_alloc_invalid_free_count();
    for (i = 0; i < NW; ++i)
        assert(pthread_create(&t[i], NULL, s1_worker, &w[i]) == 0);
    for (i = 0; i < NW; ++i) (void)pthread_join(t[i], NULL);
    for (i = 0; i < NW; ++i) {
        ops += w[i].ops;
        failures += w[i].failures;
        meta_loss += (uint64_t)w[i].detail;
    }
    end_live = cplus_alloc_live_count();
    end_bytes = cplus_alloc_live_bytes();
    df1 = cplus_alloc_double_free_count();
    inf1 = cplus_alloc_invalid_free_count();
    if (df1 != df0 || inf1 != inf0)
        finding("scenario=1 (interleaving=post-join; victim thread calls "
                "cplus_free on a pointer whose metadata was stolen by the ABA "
                "race above) spurious bad-free counters: double_free %lu->%lu "
                "invalid_free %lu->%lu on first-and-only frees",
                df0, df1, inf0, inf1);
    if (meta_loss)
        finding("scenario=1 (interleaving=T0 cplus_realloc(X,n) releases host "
                "block X after its first g_meta_lock section; T1 cplus_malloc "
                "reuses address X and inserts metadata; T0 reacquires "
                "g_meta_lock, meta_find_addr(X) returns T1's slot and "
                "meta_remove() deletes T1's live metadata) cplus_realloc "
                "returned EINVAL with the original pointer no longer live "
                "-- metadata-loss ABA race: %llu occurrences",
                (unsigned long long)meta_loss);
    if (failures)
        finding("scenario=1 (8x500 malloc/realloc/free, interleaving=start-barrier "
                "then 8-way unsynchronized allocator churn) realloc/pattern "
                "integrity failures=%llu (of which %llu are the ABA metadata "
                "loss above)", (unsigned long long)failures,
                (unsigned long long)meta_loss);
    if (end_live != base_live || end_bytes != base_bytes)
        finding("scenario=1 (interleaving=join of 8 churn threads) metadata leak: "
                "live %lu->%lu bytes %lu->%lu",
                (unsigned long)base_live, (unsigned long)end_live,
                (unsigned long)base_bytes, (unsigned long)end_bytes);
    cor_barrier_destroy(&b);
    return ops;
}

/* =============================================================== SCENARIO 2
 * 8 x 200 coordinated operations. Each operation allocates its own pointer
 * AND queries the metadata API (per-pointer and global counters) while the
 * other seven threads are concurrently allocating/freeing. All returned
 * values must be self-consistent and within sane bounds ("never absurd").
 */
static void *s2_worker(void *arg) {
    cor_worker_t *w = (cor_worker_t *)arg;
    unsigned long df0, inf0;
    int i;
    cor_barrier_wait(w->barrier);
    /* Baseline captured after the barrier is private to this thread; no other
     * worker increments the invalid-free counters in this scenario. */
    df0 = cplus_alloc_double_free_count();
    inf0 = cplus_alloc_invalid_free_count();
    for (i = 0; i < 200; ++i) {
        size_t sz = 16u + (size_t)((i * 5 + w->id) % 96u);
        unsigned char *p = (unsigned char *)cplus_malloc(sz);
        cplus_alloc_meta meta;
        size_t out = 0;
        size_t lc, lb;
        unsigned long df, inf;
        if (!p) { ++w->failures; ++w->ops; continue; }
        memset(p, (unsigned char)(w->id + 1), sz);

        if (cplus_alloc_size(p) != sz) ++w->failures;
        if (cplus_alloc_is_live(p) != 1) ++w->failures;
        if (cplus_alloc_elem_size(p) != 1u) ++w->failures;
        if (cplus_alloc_length(p) != sz) ++w->failures;
        if (cplus_alloc_query_length(p, 1u, &out) != 0 || out != sz)
            ++w->failures;
        memset(&meta, 0, sizeof(meta));
        if (cplus_alloc_get_meta(p, &meta) != 0 || !meta.is_live ||
            meta.byte_size != sz || meta.elem_size != 1u ||
            meta.elem_count != sz || meta.generation == 0ull)
            ++w->failures;
        if (meta.generation &&
            cplus_alloc_check_generation(p, meta.generation) != 1)
            ++w->failures;

        lc = cplus_alloc_live_count();
        lb = cplus_alloc_live_bytes();
        df = cplus_alloc_double_free_count();
        inf = cplus_alloc_invalid_free_count();
        /* Absolute sanity: no wraparound, no impossible live_bytes < live_count
         * (each live allocation occupies >= 1 byte), no spurious bad frees. */
        if (lc > 100000u || lb > (size_t)100000u * 1024u) ++w->failures;
        if ((lc == 0u && lb != 0u) || (lc != 0u && lb < lc)) ++w->failures;
        if (cplus_alloc_size(p) == (size_t)-1) ++w->failures;
        if (df != df0 || inf != inf0) ++w->failures;

        cplus_free(p);
        ++w->ops;
    }
    return NULL;
}

static uint64_t scenario2(void) {
    cor_worker_t w[NW];
    pthread_t t[NW];
    cor_barrier_t b;
    uint64_t ops = 0, failures = 0;
    size_t base_live;
    int i;
    memset(w, 0, sizeof(w));
    cor_barrier_init(&b, NW);
    for (i = 0; i < NW; ++i) { w[i].id = i; w[i].barrier = &b; }
    base_live = cplus_alloc_live_count();
    for (i = 0; i < NW; ++i)
        assert(pthread_create(&t[i], NULL, s2_worker, &w[i]) == 0);
    for (i = 0; i < NW; ++i) (void)pthread_join(t[i], NULL);
    for (i = 0; i < NW; ++i) { ops += w[i].ops; failures += w[i].failures; }
    if (failures)
        finding("scenario=2 (8x200 metadata queries while 7 peers allocate, "
                "interleaving=start-barrier then concurrent metadata reads) "
                "absurd/inconsistent metadata values=%llu",
                (unsigned long long)failures);
    if (cplus_alloc_live_count() != base_live)
        finding("scenario=2 (interleaving=post-join) live_count did not return "
                "to baseline");
    cor_barrier_destroy(&b);
    return ops;
}

/* =============================================================== SCENARIO 3
 * 4 threads flip cplus_libc_log_set_enabled (an atomic in log.c) while 4
 * threads call instrumented APIs. Return values must stay correct regardless
 * of whether logging is enabled mid-call. stderr is redirected to /dev/null
 * only for this scenario and restored afterwards (so UBSan output stays
 * visible in other scenarios).
 */
static void *s3_toggle(void *arg) {
    cor_worker_t *w = (cor_worker_t *)arg;
    int i;
    cor_barrier_wait(w->barrier);
    for (i = 0; i < 200; ++i) {
        cplus_libc_log_set_enabled(0);
        cplus_libc_log_set_enabled(1);
        ++w->ops;
    }
    return NULL;
}

static void *s3_worker(void *arg) {
    cor_worker_t *w = (cor_worker_t *)arg;
    char nb[32];
    wchar_t wb[32];
    int i;
    cor_barrier_wait(w->barrier);
    for (i = 0; i < 200; ++i) {
        unsigned char *p = (unsigned char *)cplus_malloc(24);
        char *d = cplus_strdup("storm");
        int r = cplus_snprintf(nb, sizeof(nb), "%d", 42);
        int wr = cplus_swprintf(wb, 32, L"v=%d", 42);
        float sf = cplus_sinf(0.0f);
        double rd = cplus_round(2.25);
        if (p) { memset(p, 0xC3, 24); cplus_free(p); }
        else ++w->failures;
        if (!d || strcmp(d, "storm") != 0) ++w->failures;
        cplus_free(d);
        if (r != 2 || strcmp(nb, "42") != 0) ++w->failures;
        /* macOS swprintf counts the terminating NUL, ISO C99 does not; use
         * the host's own return value for this fixed format as the oracle. */
        if (wr != w->detail || wcscmp(wb, L"v=42") != 0) ++w->failures;
        if (sf != 0.0f || rd != 2.0) ++w->failures; /* round(2.25) == 2 */
        ++w->ops;
    }
    return NULL;
}

static uint64_t scenario3(void) {
    cor_worker_t w[NW];
    pthread_t t[NW];
    cor_barrier_t b;
    uint64_t ops = 0, failures = 0;
    int saved_stderr, nullfd, i;
    wchar_t host_wb[32];
    int host_rc = swprintf(host_wb, 32, L"v=%d", 42);
    memset(w, 0, sizeof(w));
    cor_barrier_init(&b, NW);
    for (i = 0; i < NW; ++i) {
        w[i].id = i;
        w[i].barrier = &b;
        w[i].detail = host_rc; /* stable before pthread_create: no race */
    }

    saved_stderr = dup(STDERR_FILENO);
    nullfd = open("/dev/null", O_WRONLY);
    if (nullfd >= 0) {
        (void)dup2(nullfd, STDERR_FILENO);
        (void)close(nullfd);
    }
    for (i = 0; i < 4; ++i)
        assert(pthread_create(&t[i], NULL, s3_toggle, &w[i]) == 0);
    for (i = 4; i < NW; ++i)
        assert(pthread_create(&t[i], NULL, s3_worker, &w[i]) == 0);
    for (i = 0; i < NW; ++i) (void)pthread_join(t[i], NULL);
    cplus_libc_log_set_enabled(0); /* restore default for subsequent scenarios */
    if (saved_stderr >= 0) {
        (void)dup2(saved_stderr, STDERR_FILENO);
        (void)close(saved_stderr);
    }

    for (i = 0; i < NW; ++i) { ops += w[i].ops; failures += w[i].failures; }
    if (failures)
        finding("scenario=3 (4 togglers + 4 instrumented workers x 200, "
                "interleaving=start-barrier then atomic log flag flips during "
                "API calls) return-value failures=%llu",
                (unsigned long long)failures);
    cor_barrier_destroy(&b);
    return ops;
}

/* =============================================================== SCENARIO 4
 * 8 threads x 25 = 200 per-thread tmpfile() lifecycles. Each stream is used
 * exclusively by its owning thread (write -> rewind -> read -> close), so any
 * content mismatch is genuine cross-thread corruption, not a test race. The
 * host FILE* is paired with the host fclose() because tmpfile() itself is a
 * host call; cplus_fclose() is a different wrapper (for cplus_fopen streams).
 */
static void *s4_worker(void *arg) {
    cor_worker_t *w = (cor_worker_t *)arg;
    int i;
    cor_barrier_wait(w->barrier);
    for (i = 0; i < 25; ++i) {
        char wbuf[32], rbuf[32];
        FILE *fp = tmpfile();
        size_t n;
        if (!fp) { ++w->failures; ++w->ops; continue; }
        memset(wbuf, 0, sizeof(wbuf));
        n = (size_t)snprintf(wbuf, sizeof(wbuf), "T%d-%d", w->id, i);
        if (fwrite(wbuf, 1, n, fp) != n) ++w->failures;
        rewind(fp);
        memset(rbuf, 0, sizeof(rbuf));
        if (!fgets(rbuf, sizeof(rbuf), fp) || strcmp(rbuf, wbuf) != 0)
            ++w->failures;
        if (fclose(fp) != 0) ++w->failures;
        ++w->ops;
    }
    return NULL;
}

static uint64_t scenario4(void) {
    cor_worker_t w[NW];
    pthread_t t[NW];
    cor_barrier_t b;
    uint64_t ops = 0, failures = 0;
    int i;
    memset(w, 0, sizeof(w));
    cor_barrier_init(&b, NW);
    for (i = 0; i < NW; ++i) { w[i].id = i; w[i].barrier = &b; }
    for (i = 0; i < NW; ++i)
        assert(pthread_create(&t[i], NULL, s4_worker, &w[i]) == 0);
    for (i = 0; i < NW; ++i) (void)pthread_join(t[i], NULL);
    for (i = 0; i < NW; ++i) { ops += w[i].ops; failures += w[i].failures; }
    if (failures)
        finding("scenario=4 (8x25 per-thread tmpfile lifecycles, "
                "interleaving=start-barrier then concurrent independent FILE "
                "streams) content/close failures=%llu (cross-thread corruption)",
                (unsigned long long)failures);
    cor_barrier_destroy(&b);
    return ops;
}

/* =============================================================== SCENARIO 5
 * Per-thread FP rounding mode. The hardware FP environment is thread-local,
 * so 8 threads can each own a different mode with no data race. Each thread
 * verifies its own mode survives cplus_sinf/cplus_round calls; main verifies
 * its mode is unchanged after all joins.
 */
static const int s5_modes[4] = { FE_UPWARD, FE_DOWNWARD, FE_TONEAREST, FE_TOWARDZERO };

static void *s5_worker(void *arg) {
    cor_worker_t *w = (cor_worker_t *)arg;
    int mode = s5_modes[w->id % 4];
    int supported;
    int i;
    cor_barrier_wait(w->barrier);
    /* A platform that accepts fesetround() but silently ignores it must not
     * manufacture a false finding: confirm the mode actually took. */
    supported = (cplus_fesetround(mode) == 0 && cplus_fegetround() == mode);
    for (i = 0; i < 200; ++i) {
        volatile float x = (float)i * 0.125f;
        volatile double y = 2.3 + (double)i;
        volatile float r = cplus_sinf(x);
        volatile double d = cplus_round(y);
        (void)r;
        if (d != (double)(i + 2)) ++w->failures;
        if (supported && cplus_fegetround() != mode) {
            ++w->failures;
            break;
        }
        ++w->ops;
    }
    if (supported) (void)cplus_fesetround(FE_TONEAREST);
    return NULL;
}

static uint64_t scenario5(void) {
    cor_worker_t w[NW];
    pthread_t t[NW];
    cor_barrier_t b;
    uint64_t ops = 0, failures = 0;
    int before, after, i;
    memset(w, 0, sizeof(w));
    cor_barrier_init(&b, NW);
    for (i = 0; i < NW; ++i) { w[i].id = i; w[i].barrier = &b; }
    before = cplus_fegetround();
    for (i = 0; i < NW; ++i)
        assert(pthread_create(&t[i], NULL, s5_worker, &w[i]) == 0);
    for (i = 0; i < NW; ++i) (void)pthread_join(t[i], NULL);
    for (i = 0; i < NW; ++i) { ops += w[i].ops; failures += w[i].failures; }
    after = cplus_fegetround();
    if (failures)
        finding("scenario=5 (8x200 per-thread rounding modes, "
                "interleaving=start-barrier then 4 distinct thread-local modes "
                "running cplus_sinf/cplus_round) mode/result failures=%llu",
                (unsigned long long)failures);
    if (after != before)
        finding("scenario=5 (interleaving=main joined all FP workers) main "
                "rounding mode changed %d -> %d", before, after);
    cor_barrier_destroy(&b);
    return ops;
}

/* =============================================================== SCENARIO 6
 * Coordinated failure injection. Four workers run a fixed 300-iteration
 * alloc/realloc/free loop. At deterministic iteration boundaries the workers
 * request phase changes; one injector thread applies cplus_alloc_fail_after()
 * / cplus_alloc_fail_reset() while the workers are live. All phase state is
 * under one mutex + condvar (the "applied" handshake makes the interleaving
 * exact). A failed realloc must leave the original allocation live and intact;
 * after the final reset all metadata must return to baseline.
 */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv;
    cor_barrier_t *start;
    int phase;           /* highest phase requested by any worker (under m) */
    int applied;         /* highest phase applied by injector (under m)      */
    int workers_done;    /* under m */
    int total_workers;
    long nulls_seen;     /* under m */
} inj_t;

static void inj_request(inj_t *s, int phase) {
    pthread_mutex_lock(&s->m);
    if (s->phase < phase) {
        s->phase = phase;
        pthread_cond_broadcast(&s->cv);
    }
    while (s->applied < phase)
        pthread_cond_wait(&s->cv, &s->m);
    pthread_mutex_unlock(&s->m);
}

static void *s6_worker(void *arg) {
    cor_worker_t *w = (cor_worker_t *)arg;
    inj_t *s = (inj_t *)w->shared;
    int i, j;
    cor_barrier_wait(w->barrier);
    for (i = 0; i < 300; ++i) {
        unsigned char *p, *q;
        if (i == 75) inj_request(s, 1);       /* fail_after(3): partial */
        else if (i == 150) inj_request(s, 2); /* fail_after(0): fail all */
        else if (i == 225) inj_request(s, 3); /* fail_reset(): recover */

        p = (unsigned char *)cplus_malloc(48);
        if (!p) {
            pthread_mutex_lock(&s->m);
            ++s->nulls_seen;
            pthread_mutex_unlock(&s->m);
            ++w->ops;
            continue;
        }
        memset(p, 0x77, 48);
        q = (unsigned char *)cplus_realloc(p, 80);
        if (!q) {
            /* On injection-induced realloc failure the original pointer must
             * remain live, correctly sized, and byte-for-byte intact. */
            if (!cplus_alloc_is_live(p) || cplus_alloc_size(p) != 48u) {
                ++w->failures;
                ++w->detail; /* stale/lost metadata, i.e. the ABA race */
            }
            for (j = 0; j < 48; ++j)
                if (p[j] != 0x77) { ++w->failures; break; }
            pthread_mutex_lock(&s->m);
            ++s->nulls_seen;
            pthread_mutex_unlock(&s->m);
            cplus_free(p);
            ++w->ops;
            continue;
        }
        for (j = 0; j < 48; ++j)
            if (q[j] != 0x77) { ++w->failures; break; }
        memset(q + 48, 0x88, 32);
        cplus_free(q);
        ++w->ops;
    }
    pthread_mutex_lock(&s->m);
    ++s->workers_done;
    pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->m);
    return NULL;
}

static void *s6_injector(void *arg) {
    inj_t *s = (inj_t *)arg;
    int handled = 0;
    cor_barrier_wait(s->start);
    pthread_mutex_lock(&s->m);
    while (s->workers_done < s->total_workers || handled < s->phase) {
        while (s->phase <= handled && s->workers_done < s->total_workers)
            pthread_cond_wait(&s->cv, &s->m);
        while (handled < s->phase) {
            ++handled;
            if (handled == 1) (void)cplus_alloc_fail_after(3);
            else if (handled == 2) (void)cplus_alloc_fail_after(0);
            else if (handled == 3) (void)cplus_alloc_fail_reset();
            s->applied = handled;
            pthread_cond_broadcast(&s->cv); /* release workers in inj_request */
        }
    }
    pthread_mutex_unlock(&s->m);
    return NULL;
}

static uint64_t scenario6(void) {
    cor_worker_t w[4];
    pthread_t t[4], tinj;
    cor_barrier_t b;
    inj_t s;
    uint64_t ops = 0, failures = 0, meta_loss = 0;
    size_t base_live, base_bytes;
    unsigned long df0, inf0;
    int i;
    memset(w, 0, sizeof(w));
    memset(&s, 0, sizeof(s));
    cor_barrier_init(&b, 5); /* 4 workers + injector */
    assert(pthread_mutex_init(&s.m, NULL) == 0);
    assert(pthread_cond_init(&s.cv, NULL) == 0);
    s.start = &b;
    s.total_workers = 4;

    base_live = cplus_alloc_live_count();
    base_bytes = cplus_alloc_live_bytes();
    df0 = cplus_alloc_double_free_count();
    inf0 = cplus_alloc_invalid_free_count();

    for (i = 0; i < 4; ++i) {
        w[i].id = i;
        w[i].barrier = &b;
        w[i].shared = &s;
    }
    assert(pthread_create(&tinj, NULL, s6_injector, &s) == 0);
    for (i = 0; i < 4; ++i)
        assert(pthread_create(&t[i], NULL, s6_worker, &w[i]) == 0);
    for (i = 0; i < 4; ++i) (void)pthread_join(t[i], NULL);
    (void)pthread_join(tinj, NULL);

    for (i = 0; i < 4; ++i) {
        ops += w[i].ops;
        failures += w[i].failures;
        meta_loss += (uint64_t)w[i].detail;
    }
    ops += 3u; /* the three injected phase transitions */

    if (failures)
        finding("scenario=6 (4x300 alloc workers + injected fail_after(3)/"
                "fail_after(0)/fail_reset, interleaving=mutex+condvar phase "
                "handshake at worker iters 75/150/225, concurrent reallocs "
                "outside injection windows) realloc/pattern corruption "
                "failures=%llu (of which %llu are realloc failures with a "
                "no-longer-live original -- the scenario-1 ABA metadata loss)",
                (unsigned long long)failures, (unsigned long long)meta_loss);
    if (cplus_alloc_live_count() != base_live ||
        cplus_alloc_live_bytes() != base_bytes)
        finding("scenario=6 (interleaving=post-reset+join) live metadata did "
                "not return to baseline: live %lu->%lu bytes %lu->%lu "
                "(nulls_seen=%ld)",
                (unsigned long)base_live,
                (unsigned long)cplus_alloc_live_count(),
                (unsigned long)base_bytes,
                (unsigned long)cplus_alloc_live_bytes(), s.nulls_seen);
    if (cplus_alloc_double_free_count() != df0 ||
        cplus_alloc_invalid_free_count() != inf0)
        finding("scenario=6 (interleaving=post-reset+join) double/invalid-free "
                "counters changed: df %lu->%lu inf %lu->%lu",
                df0, cplus_alloc_double_free_count(),
                inf0, cplus_alloc_invalid_free_count());

    (void)pthread_cond_destroy(&s.cv);
    (void)pthread_mutex_destroy(&s.m);
    cor_barrier_destroy(&b);
    return ops;
}

/* ------------------------------------------------------------------- main */

int main(void) {
    uint64_t total = 0;
    total += scenario1();
    total += scenario2();
    total += scenario3();
    total += scenario4();
    total += scenario5();
    total += scenario6();

    assert(total > 0u);
    if (g_findings)
        printf("corpus_concurrency: %lu finding(s) reported above\n", g_findings);
    printf("corpus_concurrency: %llu ops ok\n", (unsigned long long)total);
    fflush(stdout);
    return 0;
}
