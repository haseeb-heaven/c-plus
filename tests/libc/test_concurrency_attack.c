/* Adversarial concurrency attack on the C+ hardened libc surface.
 *
 * Standalone C99 + pthreads. All cross-thread state is protected by a mutex or
 * a pthread mutex/cond barrier; the test never introduces a genuine data race
 * or UB. Defects are reported as "FINDING: ..." lines; the harness still prints
 * "concurrency_attack: ok" and returns 0 so all attacks run to completion.
 *
 * Targets: cplus_malloc/calloc/realloc/free, metadata counters,
 * cplus_transfer_ownership, cplus_strdup, cplus_libc_log{,set_enabled},
 * cplus_fopen/cplus_fclose on per-thread streams, thread-local FP state,
 * realloc failure injection, and the wide-format swprintf/vswprintf wrappers.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "install-include/cplus_libc.h"

#define NTHREADS 8

/* ------------------------------------------------------------------ utils */

static pthread_mutex_t g_report_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_findings = 0;

static void finding(const char *fmt, ...) {
    va_list ap;
    pthread_mutex_lock(&g_report_lock);
    ++g_findings;
    printf("FINDING: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_report_lock);
}

/* macOS has no pthread_barrier_t; roll a mutex/cond barrier. */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv;
    unsigned n, generation, waiting;
} barrier_t;

static void barrier_init(barrier_t *b, unsigned n) {
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->cv, NULL);
    b->n = n;
    b->generation = 0;
    b->waiting = 0;
}

static void barrier_wait(barrier_t *b) {
    pthread_mutex_lock(&b->m);
    {
        unsigned gen = b->generation;
        if (++b->waiting == b->n) {
            b->waiting = 0;
            ++b->generation;
            pthread_cond_broadcast(&b->cv);
        } else {
            while (gen == b->generation)
                pthread_cond_wait(&b->cv, &b->m);
        }
    }
    pthread_mutex_unlock(&b->m);
}

static void barrier_destroy(barrier_t *b) {
    pthread_cond_destroy(&b->cv);
    pthread_mutex_destroy(&b->m);
}

typedef struct {
    int id;
    int failures;
    int detail; /* attack-specific counter (e.g. failed cplus_fclose calls) */
    int f[10];  /* per-check counters, for root-cause triage */
} ctx_t;

/* ================================================================== ATTACK 1
 * 8 threads x 2000 malloc -> write -> realloc -> free on thread-local
 * pointers. Check fill patterns and that live metadata returns to baseline.
 */
static void *attack1_worker(void *arg) {
    ctx_t *c = (ctx_t *)arg;
    unsigned char pat = (unsigned char)(c->id * 31 + 7);
    unsigned char pat2 = (unsigned char)(pat + 1u);
    int i;
    for (i = 0; i < 2000; ++i) {
        /* Alternate small<->large so the host realloc must MOVE the small
         * chunk, freeing the old address for another thread to reuse. */
        size_t s1 = (i & 1) ? (4096u + (size_t)((i * 7) % 64u) * 16u) : 48u;
        size_t s2 = (i & 1) ? 48u : (4096u + (size_t)((i * 13) % 64u) * 16u);
        size_t common, k;
        unsigned char *p = (unsigned char *)cplus_malloc(s1);
        unsigned char *q;
        if (!p) {
            ++c->failures;
            ++c->f[0]; /* malloc NULL */
            continue;
        }
        memset(p, pat, s1);
        for (k = 0; k < s1; ++k) {
            if (p[k] != pat) {
                ++c->failures;
                ++c->f[1]; /* malloc pattern */
                break;
            }
        }
        q = (unsigned char *)cplus_realloc(p, s2);
        if (!q) {
            ++c->failures;
            ++c->f[2]; /* realloc NULL */
            if (errno == EINVAL) ++c->f[5];
            else if (errno == ENOMEM) ++c->f[6];
            else ++c->f[7];
            if (!cplus_alloc_is_live(p)) ++c->f[8]; /* metadata lost too */
            cplus_free(p);
            continue;
        }
        common = s1 < s2 ? s1 : s2;
        for (k = 0; k < common; ++k) {
            if (q[k] != pat) {
                ++c->failures;
                ++c->f[3]; /* realloc did not preserve */
                break;
            }
        }
        for (k = common; k < s2; ++k) q[k] = pat2;
        for (k = common; k < s2; ++k) {
            if (q[k] != pat2) {
                ++c->failures;
                ++c->f[4]; /* new bytes not writable */
                break;
            }
        }
        cplus_free(q);
    }
    return NULL;
}

static void attack1(void) {
    ctx_t ctx[NTHREADS];
    pthread_t t[NTHREADS];
    size_t base_live, base_bytes, end_live, end_bytes;
    int i, failures = 0;
    memset(ctx, 0, sizeof(ctx));
    for (i = 0; i < NTHREADS; ++i) ctx[i].id = i;
    base_live = cplus_alloc_live_count();
    base_bytes = cplus_alloc_live_bytes();
    for (i = 0; i < NTHREADS; ++i)
        (void)pthread_create(&t[i], NULL, attack1_worker, &ctx[i]);
    for (i = 0; i < NTHREADS; ++i)
        (void)pthread_join(t[i], NULL);
    for (i = 0; i < NTHREADS; ++i) failures += ctx[i].failures;
    if (failures) {
        int k, tally[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (i = 0; i < NTHREADS; ++i)
            for (k = 0; k < 10; ++k) tally[k] += ctx[i].f[k];
        finding("attack1: %d failures (mallocNULL=%d mallocPat=%d reallocNULL=%d "
                "reallocPreserve=%d reallocNew=%d) [realloc errno EINVAL=%d ENOMEM=%d other=%d "
                "originalNoLongerLive=%d] under 8x2000 churn",
                failures, tally[0], tally[1], tally[2], tally[3], tally[4],
                tally[5], tally[6], tally[7], tally[8]);
    }
    end_live = cplus_alloc_live_count();
    end_bytes = cplus_alloc_live_bytes();
    if (end_live != base_live || end_bytes != base_bytes)
        finding("attack1: metadata leak: live %lu->%lu bytes %lu->%lu",
                (unsigned long)base_live, (unsigned long)end_live,
                (unsigned long)base_bytes, (unsigned long)end_bytes);
}

/* ================================================================== ATTACK 2
 * Deliberate contention: allocators churn while readers hammer the metadata
 * counters. Values must stay non-negative, bounded, and self-consistent
 * (live_bytes >= live_count because every live allocation is >= 1 byte).
 */
static void *attack2_churn(void *arg) {
    ctx_t *c = (ctx_t *)arg;
    int i;
    for (i = 0; i < 20000; ++i) {
        unsigned char *p = (unsigned char *)cplus_malloc(64);
        char *d;
        if (!p) {
            ++c->failures;
            continue;
        }
        memset(p, 0xA5, 64);
        if (cplus_alloc_size(p) != 64 || !cplus_alloc_is_live(p))
            ++c->failures;
        d = cplus_strdup("attack2");
        if (!d || strcmp(d, "attack2") != 0)
            ++c->failures;
        cplus_free(d);
        cplus_free(p);
    }
    return NULL;
}

static void *attack2_query(void *arg) {
    ctx_t *c = (ctx_t *)arg;
    int i;
    for (i = 0; i < 20000; ++i) {
        size_t lc = cplus_alloc_live_count();
        size_t lb = cplus_alloc_live_bytes();
        unsigned long df = cplus_alloc_double_free_count();
        unsigned long inf = cplus_alloc_invalid_free_count();
        if (lc > 100000u || lb > (size_t)100000u * 1024u) {
            ++c->failures;
            ++c->f[0];
            continue;
        }
        if (lc == 0u && lb != 0u) {
            ++c->failures;
            ++c->f[3]; /* live_count 0 but live_bytes > 0 */
        }
        if (lc != 0u && lb < lc) {
            ++c->failures;
            ++c->f[4]; /* live_bytes < live_count */
        }
        if (df > 1000ul || inf > 1000ul) {
            ++c->failures;
            ++c->f[2];
        }
    }
    return NULL;
}

static void attack2(void) {
    ctx_t ctx[NTHREADS];
    pthread_t t[NTHREADS];
    int i, failures = 0;
    size_t base_live;
    memset(ctx, 0, sizeof(ctx));
    for (i = 0; i < NTHREADS; ++i) ctx[i].id = i;
    base_live = cplus_alloc_live_count();
    for (i = 0; i < 4; ++i)
        (void)pthread_create(&t[i], NULL, attack2_churn, &ctx[i]);
    for (i = 4; i < NTHREADS; ++i)
        (void)pthread_create(&t[i], NULL, attack2_query, &ctx[i]);
    for (i = 0; i < NTHREADS; ++i)
        (void)pthread_join(t[i], NULL);
    for (i = 0; i < NTHREADS; ++i) failures += ctx[i].failures;
    if (failures) {
        int k, tally[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (i = 0; i < NTHREADS; ++i)
            for (k = 0; k < 10; ++k) tally[k] += ctx[i].f[k];
        finding("attack2: %d failures (bound=%d counterBound=%d) "
                "[lc0_lbNZ=%d lb_lt_lc=%d]",
                failures, tally[0], tally[2], tally[3], tally[4]);
    }
    if (cplus_alloc_live_count() != base_live)
        finding("attack2: live_count did not return to baseline");
}

/* ================================================================== ATTACK 3
 * Ownership transfer. Two sub-cases:
 *  3a: same (dst,src) pair; protected thread goes first, second waits via a
 *      mutex-protected flag then tries unprotected. Protected side must win,
 *      the second attempt must fail cleanly, and no double free may occur.
 *  3b: two DIFFERENT source variables that alias the same live allocation.
 *      Both callers pass the precondition checks and both can claim ownership
 *      because "claimed" is never recorded in the metadata side-table.
 */
typedef struct {
    pthread_mutex_t m;
    int a_done;
    int rc_a, rc_b;
    void *src, *dst;
} xfer_shared_t;

static void *xfer_a(void *arg) {
    xfer_shared_t *s = (xfer_shared_t *)arg;
    pthread_mutex_lock(&s->m);
    s->rc_a = cplus_transfer_ownership(&s->dst, &s->src);
    s->a_done = 1;
    pthread_mutex_unlock(&s->m);
    return NULL;
}

static void *xfer_b(void *arg) {
    xfer_shared_t *s = (xfer_shared_t *)arg;
    int rc;
    /* Wait until A has finished, using the same mutex: no overlapping access
     * to *dst and *src, hence no data race in the test itself. */
    for (;;) {
        pthread_mutex_lock(&s->m);
        if (s->a_done) {
            pthread_mutex_unlock(&s->m);
            break;
        }
        pthread_mutex_unlock(&s->m);
    }
    rc = cplus_transfer_ownership(&s->dst, &s->src); /* deliberately no mutex */
    pthread_mutex_lock(&s->m);
    s->rc_b = rc;
    pthread_mutex_unlock(&s->m);
    return NULL;
}

typedef struct {
    void **dst;
    void **src;
    barrier_t *b;
    int rc;
} xfer_ctx_t;

static void *xfer_two(void *arg) {
    xfer_ctx_t *c = (xfer_ctx_t *)arg;
    barrier_wait(c->b);
    c->rc = cplus_transfer_ownership(c->dst, c->src);
    return NULL;
}

static void attack3(void) {
    unsigned long df0, df1;
    /* 3a */
    {
        xfer_shared_t s;
        pthread_t ta, tb;
        void *p = cplus_malloc(16);
        memset(p, 0x3a, 16);
        memset(&s, 0, sizeof(s));
        pthread_mutex_init(&s.m, NULL);
        s.src = p;
        s.dst = NULL;
        df0 = cplus_alloc_double_free_count();
        (void)pthread_create(&ta, NULL, xfer_a, &s);
        (void)pthread_create(&tb, NULL, xfer_b, &s);
        (void)pthread_join(ta, NULL);
        (void)pthread_join(tb, NULL);
        if (s.rc_a != 0 || s.rc_b == 0 || s.dst != p || s.src != NULL)
            finding("attack3a: transfer result A=%d B=%d dst=%p src=%p (expected 0/EINVAL/p/NULL)",
                    s.rc_a, s.rc_b, s.dst, s.src);
        if (cplus_free_checked(&s.dst) != 0 || s.dst != NULL)
            finding("attack3a: free_checked on transferred pointer failed");
        cplus_free(p); /* genuine double free -> must be counted, not crash */
        df1 = cplus_alloc_double_free_count();
        if (df1 != df0 + 1ul)
            finding("attack3a: double-free count %lu->%lu (expected +1)", df0, df1);
        pthread_mutex_destroy(&s.m);
    }
    /* 3b: independent claimants of the same live allocation. No shared
     * caller variables -> no data race; only the library's metadata lock is
     * contended. Both calls are allowed to run to completion. */
    {
        barrier_t b;
        xfer_ctx_t ca, cb;
        pthread_t ta, tb;
        void *p = cplus_malloc(24);
        void *src_a = p, *dst_a = NULL;
        void *src_b = p, *dst_b = NULL;
        memset(p, 0x3b, 24);
        barrier_init(&b, 2);
        ca.dst = &dst_a; ca.src = &src_a; ca.b = &b; ca.rc = -99;
        cb.dst = &dst_b; cb.src = &src_b; cb.b = &b; cb.rc = -99;
        (void)pthread_create(&ta, NULL, xfer_two, &ca);
        (void)pthread_create(&tb, NULL, xfer_two, &cb);
        (void)pthread_join(ta, NULL);
        (void)pthread_join(tb, NULL);
        barrier_destroy(&b);
        if (ca.rc == 0 && cb.rc == 0)
            finding("attack3b: two independent source variables both claimed the "
                    "same live pointer (%p): rcA=%d rcB=%d, dstA==dstB==%p "
                    "-- transfer does not atomically consume the allocation "
                    "(alloc.c: meta_find + generation bump only), so a later "
                    "free of both owners is a double free", p, ca.rc, cb.rc, dst_a);
        /* Release the allocation exactly once; discard the other stale alias
         * without freeing it so the test itself never double-frees. */
        if (dst_a) cplus_free(dst_a);
        else if (dst_b) cplus_free(dst_b);
        if (src_a) cplus_free(src_a);
        if (src_b) cplus_free(src_b);
    }
}

/* ================================================================== ATTACK 4
 * Logging toggle storm: one thread flips the atomic enable flag while seven
 * threads call instrumented APIs. Return values must stay correct. stderr is
 * redirected to /dev/null in main() before this attack.
 */
typedef struct {
    pthread_mutex_t m;
    int workers_done;
} shared4_t;

typedef struct {
    ctx_t base;
    shared4_t *s;
    int wr_expect; /* host swprintf return count for the fixed test format */
} log_ctx_t;

static void *log_toggle(void *arg) {
    shared4_t *s = (shared4_t *)arg;
    int i;
    for (;;) {
        pthread_mutex_lock(&s->m);
        i = s->workers_done;
        pthread_mutex_unlock(&s->m);
        if (i >= 7) break;
        cplus_libc_log_set_enabled(0);
        cplus_libc_log_set_enabled(1);
    }
    cplus_libc_log_set_enabled(0);
    return NULL;
}

static void *log_worker(void *arg) {
    log_ctx_t *c = (log_ctx_t *)arg;
    wchar_t wb[32];
    char nb[32];
    int i;
    int wr_expect = c->wr_expect;
    for (i = 0; i < 300; ++i) {
        unsigned char *p = (unsigned char *)cplus_malloc(24);
        char *d = cplus_strdup("storm");
        int r = cplus_snprintf(nb, sizeof(nb), "%d", 42);
        int wr = cplus_swprintf(wb, 32, L"v=%d", 42);
        if (p) {
            memset(p, 0xC3, 24);
            cplus_free(p);
        } else {
            ++c->base.failures;
            ++c->base.f[0];
        }
        if (!d || strcmp(d, "storm") != 0) {
            ++c->base.failures;
            ++c->base.f[1];
        }
        cplus_free(d);
        if (r != 2 || strcmp(nb, "42") != 0) {
            ++c->base.failures;
            ++c->base.f[2];
        }
        if (wr != wr_expect || wcscmp(wb, L"v=42") != 0) {
            ++c->base.failures;
            ++c->base.f[3];
        }
        if (cplus_sinf(0.0f) != 0.0f) {
            ++c->base.failures;
            ++c->base.f[4];
        }
        if (cplus_round(2.6) != 3.0) {
            ++c->base.failures;
            ++c->base.f[5];
        }
    }
    pthread_mutex_lock(&c->s->m);
    ++c->s->workers_done;
    pthread_mutex_unlock(&c->s->m);
    return NULL;
}

static void attack4(void) {
    log_ctx_t ctx[NTHREADS];
    shared4_t s;
    pthread_t t[NTHREADS];
    int i, failures = 0;
    memset(ctx, 0, sizeof(ctx));
    memset(&s, 0, sizeof(s));
    pthread_mutex_init(&s.m, NULL);
    {
        wchar_t host_wb[32];
        int host_rc = swprintf(host_wb, 32, L"v=%d", 42);
        for (i = 0; i < 7; ++i) {
            ctx[i].base.id = i;
            ctx[i].s = &s;
            ctx[i].wr_expect = host_rc; /* macOS swprintf counts the NUL */
        }
    }
    (void)pthread_create(&t[7], NULL, log_toggle, &s);
    for (i = 0; i < 7; ++i)
        (void)pthread_create(&t[i], NULL, log_worker, &ctx[i]);
    for (i = 0; i < NTHREADS; ++i)
        (void)pthread_join(t[i], NULL);
    for (i = 0; i < 7; ++i) failures += ctx[i].base.failures;
    if (failures) {
        int k, tally[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (i = 0; i < 7; ++i)
            for (k = 0; k < 10; ++k) tally[k] += ctx[i].base.f[k];
        finding("attack4: %d failures (malloc=%d strdup=%d snprintf=%d swprintf=%d "
                "sinf=%d round=%d) during log toggle storm",
                failures, tally[0], tally[1], tally[2], tally[3], tally[4], tally[5]);
    }
    cplus_libc_log_set_enabled(0);
    pthread_mutex_destroy(&s.m);
}

/* ================================================================== ATTACK 5
 * Per-thread FILE streams. (a) cplus_fopen with unique paths, (b) raw tmpfile()
 * handles closed via cplus_fclose, including a deterministic FILE* address
 * reuse probe. Expect no cross-thread contamination and no false double-close.
 */
static void *file_worker(void *arg) {
    ctx_t *c = (ctx_t *)arg;
    char path[128];
    int i;
    for (i = 0; i < 20; ++i) {
        char wbuf[32], rbuf[32];
        FILE *fp;
        int rc;
        (void)snprintf(path, sizeof(path), "/tmp/cplus_conc_%ld_%d_%d.tmp",
                       (long)getpid(), c->id, i);
        fp = cplus_fopen(path, "w+");
        if (!fp) { ++c->failures; continue; }
        memset(wbuf, 0, sizeof(wbuf));
        (void)snprintf(wbuf, sizeof(wbuf), "T%d-%d", c->id, i);
        if (fwrite(wbuf, 1, strlen(wbuf), fp) != strlen(wbuf)) ++c->failures;
        rewind(fp);
        memset(rbuf, 0, sizeof(rbuf));
        if (!fgets(rbuf, sizeof(rbuf), fp) || strcmp(rbuf, wbuf) != 0)
            ++c->failures;
        rc = cplus_fclose(fp);
        if (rc != 0) ++c->detail;
        (void)cplus_remove(path);
    }
    for (i = 0; i < 80; ++i) {
        char wbuf[16], rbuf[16];
        FILE *fp = tmpfile();
        int rc;
        if (!fp) { ++c->failures; continue; }
        memset(wbuf, 0, sizeof(wbuf));
        (void)snprintf(wbuf, sizeof(wbuf), "X%d", i);
        if (fwrite(wbuf, 1, strlen(wbuf), fp) != strlen(wbuf)) ++c->failures;
        rewind(fp);
        memset(rbuf, 0, sizeof(rbuf));
        if (!fgets(rbuf, sizeof(rbuf), fp) || strcmp(rbuf, wbuf) != 0)
            ++c->failures;
        rc = cplus_fclose(fp);
        if (rc != 0) {
            ++c->detail;
            (void)fclose(fp); /* release: library refused to close a fresh stream */
        }
    }
    return NULL;
}

static void attack5(void) {
    ctx_t ctx[NTHREADS];
    int i, bad = 0;
    memset(ctx, 0, sizeof(ctx));
    for (i = 0; i < NTHREADS; ++i) ctx[i].id = i;
    {
        pthread_t t[NTHREADS];
        for (i = 0; i < NTHREADS; ++i)
            (void)pthread_create(&t[i], NULL, file_worker, &ctx[i]);
        for (i = 0; i < NTHREADS; ++i)
            (void)pthread_join(t[i], NULL);
    }
    for (i = 0; i < NTHREADS; ++i) {
        bad += ctx[i].detail;
        if (ctx[i].failures)
            finding("attack5: thread %d had %d FILE read/write failures", i, ctx[i].failures);
    }
    if (bad)
        finding("attack5: cplus_fclose returned EOF on %d freshly-opened, "
                "never-before-closed streams (raw tmpfile handles). "
                "stdio.c:cplus_fclose rejects any stream whose FILE* address "
                "is still in g_closed_streams, so FILE* address reuse yields a "
                "false double-close report and the stream is leaked.", bad);

    /* Single-threaded address-reuse probe: close a tmpfile, immediately open
     * another (the host allocator commonly reuses the FILE* address), then try
     * to close the brand-new stream through cplus_fclose. */
    {
        int k, reuse_seen = 0, reuse_rejected = 0;
        for (k = 0; k < 8; ++k) {
            FILE *a = tmpfile();
            FILE *b;
            if (!a) continue;
            (void)cplus_fclose(a); /* marks address a as closed */
            b = tmpfile();
            if (!b) continue;
            if (b == a) {
                int rc;
                ++reuse_seen;
                errno = 0;
                rc = cplus_fclose(b);
                if (rc != 0) {
                    ++reuse_rejected;
                    finding("attack5-reuse: cplus_fclose rejected a brand-new tmpfile() "
                            "reusing closed FILE* address %p (rc=%d, errno=%d EBADF=%d) "
                            "-- stale closed-stream entry is never cleared by "
                            "cplus_fclose or by the host allocator",
                            (void *)a, rc, errno, EBADF);
                    (void)fclose(b);
                }
            } else {
                (void)fclose(b);
            }
        }
        if (!reuse_seen)
            printf("concurrency_attack: note: FILE* address reuse probe saw 0 reuses\n");
        else if (!reuse_rejected)
            printf("concurrency_attack: note: FILE* reuse probe: %d reuses, all closed cleanly\n",
                   reuse_seen);
    }
}

/* ================================================================== ATTACK 6
 * Thread-local FP state: each thread selects a different rounding mode, calls
 * cplus_sinf/cplus_round, and verifies its own mode is unchanged. Main's mode
 * must also be unchanged after joining.
 */
static void *fp_worker(void *arg) {
    ctx_t *c = (ctx_t *)arg;
    static const int modes[4] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    int mode = modes[c->id % 4];
    int i;
    if (cplus_fesetround(mode) != 0) return NULL; /* platform does not support it */
    for (i = 0; i < 2000; ++i) {
        volatile float x = (float)i * 0.125f;
        volatile double y = 2.3 + (double)i;
        (void)cplus_sinf(x);
        if (cplus_round(y) != (double)(i + 2) && i < 100) ++c->failures;
        (void)cplus_sqrt((double)(i + 1));
        if (cplus_fegetround() != mode) {
            ++c->failures;
            break;
        }
    }
    return NULL;
}

static void attack6(void) {
    ctx_t ctx[NTHREADS];
    pthread_t t[NTHREADS];
    int i, failures = 0;
    int before = cplus_fegetround();
    memset(ctx, 0, sizeof(ctx));
    for (i = 0; i < NTHREADS; ++i) {
        ctx[i].id = i;
        (void)pthread_create(&t[i], NULL, fp_worker, &ctx[i]);
    }
    for (i = 0; i < NTHREADS; ++i)
        (void)pthread_join(t[i], NULL);
    for (i = 0; i < NTHREADS; ++i) failures += ctx[i].failures;
    if (failures)
        finding("attack6: %d FP-state/round-result failures", failures);
    if (cplus_fegetround() != before)
        finding("attack6: main thread rounding mode changed %d -> %d",
                before, cplus_fegetround());
}

/* ================================================================== ATTACK 7
 * Failure-injection stress. Main flips cplus_alloc_fail_after(0) under a mutex
 * while four threads allocate/realloc/free; realloc failure must preserve the
 * original pointer and its contents. Also probe negative-count semantics.
 */
typedef struct {
    pthread_mutex_t m;
    long nulls_seen;
} shared7_t;

typedef struct {
    ctx_t base;
    shared7_t *s;
    barrier_t *b;
} inj_ctx_t;

static void *inject_worker(void *arg) {
    inj_ctx_t *c = (inj_ctx_t *)arg;
    int i;
    barrier_wait(c->b);
    for (i = 0; i < 2000; ++i) {
        unsigned char *p = (unsigned char *)cplus_malloc(48);
        unsigned char *q;
        int j;
        if (!p) {
            pthread_mutex_lock(&c->s->m);
            ++c->s->nulls_seen;
            pthread_mutex_unlock(&c->s->m);
            continue;
        }
        memset(p, 0x77, 48);
        q = (unsigned char *)cplus_realloc(p, 80);
        if (!q) {
            /* Original must remain live and intact on realloc failure. */
            if (!cplus_alloc_is_live(p) || cplus_alloc_size(p) != 48) {
                ++c->base.failures;
                ++c->base.f[0]; /* original lost on realloc failure */
            }
            for (j = 0; j < 48; ++j) {
                if (p[j] != 0x77) {
                    ++c->base.failures;
                    ++c->base.f[1]; /* original data changed on realloc failure */
                    break;
                }
            }
            pthread_mutex_lock(&c->s->m);
            ++c->s->nulls_seen;
            pthread_mutex_unlock(&c->s->m);
            cplus_free(p);
            continue;
        }
        for (j = 0; j < 48; ++j) {
            if (q[j] != 0x77) {
                ++c->base.failures;
                ++c->base.f[2]; /* realloc did not preserve */
                break;
            }
        }
        memset(q + 48, 0x88, 32);
        cplus_free(q);
    }
    return NULL;
}

static void attack7(void) {
    inj_ctx_t ctx[NTHREADS];
    shared7_t s;
    barrier_t b;
    pthread_t t[NTHREADS];
    int i, failures = 0;
    size_t base_live, base_bytes;
    unsigned long df0, inf0;
    memset(ctx, 0, sizeof(ctx));
    memset(&s, 0, sizeof(s));
    pthread_mutex_init(&s.m, NULL);
    barrier_init(&b, 5); /* 4 workers + main */
    for (i = 0; i < 4; ++i) {
        ctx[i].base.id = i;
        ctx[i].s = &s;
        ctx[i].b = &b;
    }
    base_live = cplus_alloc_live_count();
    base_bytes = cplus_alloc_live_bytes();
    df0 = cplus_alloc_double_free_count();
    inf0 = cplus_alloc_invalid_free_count();

    /* Deterministic negative-count probe: "-1 off" is the documented internal
     * sentinel, but cplus_alloc_fail_after(-1) maps to "fail everything". */
    cplus_alloc_fail_reset();
    cplus_alloc_fail_after(-1);
    {
        void *probe = cplus_malloc(8);
        if (!probe)
            finding("attack7-sentinel: cplus_alloc_fail_after(-1) made every "
                    "cplus_malloc fail (alloc.c: g_inject = count < 0 ? 0 : count, "
                    "while inject_fail_locked() treats -1 as 'off')");
        else
            cplus_free(probe);
    }
    cplus_alloc_fail_reset();

    for (i = 0; i < 4; ++i)
        (void)pthread_create(&t[i], NULL, inject_worker, &ctx[i]);
    barrier_wait(&b); /* all four workers are now inside inject_worker */
    /* Flip injection on under the coordination mutex. */
    pthread_mutex_lock(&s.m);
    cplus_alloc_fail_after(0);
    pthread_mutex_unlock(&s.m);
    /* Let the workers observe failures, then reset. Bounded spin. */
    for (i = 0; i < 100000; ++i) {
        long n;
        pthread_mutex_lock(&s.m);
        n = s.nulls_seen;
        pthread_mutex_unlock(&s.m);
        if (n > 100) break;
    }
    pthread_mutex_lock(&s.m);
    cplus_alloc_fail_reset();
    pthread_mutex_unlock(&s.m);
    for (i = 0; i < 4; ++i)
        (void)pthread_join(t[i], NULL);
    barrier_destroy(&b);
    for (i = 0; i < 4; ++i) failures += ctx[i].base.failures;
    if (failures) {
        int k, tally[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (i = 0; i < 4; ++i)
            for (k = 0; k < 10; ++k) tally[k] += ctx[i].base.f[k];
        finding("attack7: %d failures (metaLostOnFail=%d dataChangedOnFail=%d "
                "reallocPreserve=%d)", failures, tally[0], tally[1], tally[2]);
    }
    if (cplus_alloc_live_count() != base_live || cplus_alloc_live_bytes() != base_bytes)
        finding("attack7: live metadata did not return to baseline after injection");
    if (cplus_alloc_double_free_count() != df0 || cplus_alloc_invalid_free_count() != inf0)
        finding("attack7: double/invalid-free counters changed during injection");
    pthread_mutex_destroy(&s.m);
}

/* ================================================================== ATTACK 8
 * Wide-format wrappers: valid conversions and defined-UB rejection paths.
 */
static int vswp(wchar_t *s, size_t n, const wchar_t *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vswprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

static void attack8(void) {
    wchar_t wb[32], hb[32];
    int a = 0, b = 0, r, host_rc;

    /* The wrapper is platform-backed: compare against the host return count
     * (macOS swprintf counts the terminating NUL; ISO C99 does not). */
    host_rc = swprintf(hb, 32, L"n=%d", 42);
    errno = 0;
    r = cplus_swprintf(wb, 32, L"n=%d", 42);
    if (r != host_rc || wcscmp(wb, L"n=42") != 0 || wcscmp(wb, hb) != 0)
        finding("attack8: cplus_swprintf valid path rc=%d (host %d) buf='%ls'", r, host_rc, wb);

    errno = 0;
    r = cplus_swprintf(wb, 32, NULL);
    if (r != -1 || errno != EINVAL)
        finding("attack8: cplus_swprintf(NULL fmt) rc=%d errno=%d (want -1/EINVAL)", r, errno);

    errno = 0;
    r = cplus_swprintf(NULL, 32, L"n=%d", 1);
    if (r != -1 || errno != EINVAL)
        finding("attack8: cplus_swprintf(NULL dst,n>0) rc=%d errno=%d (want -1/EINVAL)", r, errno);

    errno = 0;
    r = vswp(wb, 32, L"%d-%d", 7, 9);
    host_rc = swprintf(hb, 32, L"%d-%d", 7, 9);
    if (r != host_rc || wcscmp(wb, L"7-9") != 0)
        finding("attack8: cplus_vswprintf valid path rc=%d (host %d) buf='%ls'", r, host_rc, wb);

    errno = 0;
    r = cplus_swscanf(L"11 22", L"%d %d", &a, &b);
    if (r != 2 || a != 11 || b != 22)
        finding("attack8: cplus_swscanf rc=%d a=%d b=%d", r, a, b);

    errno = 0;
    r = cplus_swscanf(NULL, L"%d", &a);
    if (r != EOF || errno != EINVAL)
        finding("attack8: cplus_swscanf(NULL str) rc=%d errno=%d (want EOF/EINVAL)", r, errno);
}

int main(void) {
    /* Keep log-storm output from flooding the harness. */
    (void)freopen("/dev/null", "w", stderr);

    attack1();
    attack2();
    attack3();
    attack4();
    attack5();
    attack6();
    attack7();
    attack8();

    printf("concurrency_attack: ok\n");
    fflush(stdout);
    if (g_findings)
        printf("concurrency_attack: %d finding(s) reported above\n", g_findings);
    return 0;
}
