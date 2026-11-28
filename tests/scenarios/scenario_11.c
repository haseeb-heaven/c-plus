/* scenario_11.c - deterministic use-after-free / dangling-alias corpus.
 *
 * Standalone C99.  Ordinary C spelling is hardened by stdlib/safe/safe_c.h:
 *   free()              -> free_s()               (tracked allocation table)
 *   memset/memcpy/      -> cplus_*_s_auto()       (UAF + capacity checked)
 *   memmove/memcmp
 *
 * For every tuple in a deterministic (size, alias_count, free_timing,
 * write_offset, write_len) table we allocate, capture base/interior/end
 * aliases, free through ordinary free(), and then try reads and writes through
 * the stale aliases using only ordinary memset/memcpy/memmove/memcmp.  The
 * hardened wrappers must refuse every access; cplus_safe_ptr_was_freed(base)
 * must be true; no raw dereference of freed memory is ever performed by the
 * test itself.
 *
 * Extra coverage: immediate address reuse + generation mismatch, many aliases,
 * interior aliases, stale one-past-the-end pointer, free in one function and
 * use in another (helper), and mutex/condvar-coordinated cross-thread free/use
 * for a subset (no test-side data race).
 *
 * A crash is funneled through signal handlers that print a FINDING with the
 * exact tuple currently under test.  A false "not freed" answer, a missing
 * UAF statistic, or an access that is not refused is also a FINDING.
 */
#define _POSIX_C_SOURCE 200809L

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_memory.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_stats.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NCASES      1200
#define MAX_ALIASES 9   /* <=8 interior/base aliases + optional end pointer */

/* free_timing codes */
enum { FT_IMMEDIATE = 0, FT_REUSE, FT_INTERIOR, FT_GENERATION, FT_END };

typedef struct {
    size_t size;          /* allocation size in bytes                       */
    int    alias_count;   /* number of captured base/interior aliases        */
    int    free_timing;   /* one of FT_*                                    */
    size_t write_offset;  /* offset used for the first interior alias        */
    size_t write_len;     /* length used in stale write/read attempts        */
} case_spec;

static case_spec g_cases[NCASES];

/* Static source buffer: deliberately NOT a tracked heap allocation, so a
 * stale one-past-the-end pointer can never alias it. */
static unsigned char g_src[8192];

/* ------------------------------------------------------------- reporting -- */
static int    g_findings = 0;
static char   g_case_msg[256];
static size_t g_case_msg_len = 0;

static void set_case_msg(int idx) {
    const case_spec *c = &g_cases[idx];
    g_case_msg_len = (size_t)snprintf(
        g_case_msg, sizeof(g_case_msg),
        "case %d [size=%lu, alias_count=%d, free_timing=%d, "
        "write_offset=%lu, write_len=%lu]",
        idx, (unsigned long)c->size, c->alias_count, c->free_timing,
        (unsigned long)c->write_offset, (unsigned long)c->write_len);
}

static void finding(const char *what) {
    ++g_findings;
    if (g_findings <= 64) {
        fprintf(stderr, "FINDING: %s: %s\n", g_case_msg, what);
    }
}

static void crash_handler(int sig) {
    static const char p1[] = "FINDING: fatal signal ";
    static const char p2[] = " in ";
    char sbuf[16];
    size_t n = 0;
    int v = sig;
    if (v < 0) v = -v;
    do { sbuf[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < sizeof(sbuf));
    (void)!write(STDERR_FILENO, p1, sizeof(p1) - 1u);
    while (n > 0) (void)!write(STDERR_FILENO, &sbuf[--n], 1u);
    (void)!write(STDERR_FILENO, p2, sizeof(p2) - 1u);
    if (g_case_msg_len) (void)!write(STDERR_FILENO, g_case_msg, g_case_msg_len);
    (void)!write(STDERR_FILENO, "\n", 1u);
    _exit(70);
}

/* ------------------------------------------------------- case generation -- */
static const size_t k_sizes[10] = {
    16u, 24u, 32u, 48u, 64u, 96u, 128u, 192u, 256u, 512u
};

/* Mixed-radix decomposition: radices 10,8,5,2,3 (product 2400 > NCASES), so
 * the mapping i -> (f0,f1,f2,f3,f4) is injective and every tuple is distinct. */
static void build_cases(void) {
    int i;
    for (i = 0; i < NCASES; ++i) {
        int f0 =  i % 10;
        int f1 = (i / 10) % 8;
        int f2 = (i / 80) % 5;
        int f3 = (i / 400) % 2;
        int f4 = (i / 800) % 3;
        int wl = (f0 + f4) % 3;
        case_spec *c = &g_cases[i];
        c->size        = k_sizes[f0];
        c->alias_count = f1 + 1;                 /* 1 .. 8 */
        c->free_timing = f2;                     /* 0 .. 4 */
        c->write_offset = f3 ? (c->size - 1u) : 0u;
        c->write_len   = wl == 0 ? 1u : (wl == 1 ? c->size : c->size + 64u);
    }
}

static void verify_distinct(void) {
    int i, j, dup = 0;
    for (i = 0; i < NCASES; ++i) {
        for (j = i + 1; j < NCASES; ++j) {
            const case_spec *a = &g_cases[i], *b = &g_cases[j];
            if (a->size == b->size && a->alias_count == b->alias_count &&
                a->free_timing == b->free_timing &&
                a->write_offset == b->write_offset &&
                a->write_len == b->write_len) {
                set_case_msg(i);
                finding("duplicate tuple in generated table");
                ++dup;
                break;
            }
        }
        if (dup) break;
    }
}

/* ----------------------------------------------------- check result bits -- */
#define F_WAS_FREED  (1u << 0)
#define F_MEMSET     (1u << 1)
#define F_MEMCPY     (1u << 2)
#define F_MEMCMP     (1u << 3)
#define F_UAF_STAT   (1u << 4)
#define F_MEMMOVE    (1u << 5)
#define F_INTERIOR   (1u << 6)
#define F_GENERATION (1u << 7)
#define F_LIVENESS   (1u << 8)
#define F_META       (1u << 9)
#define F_ALLOC      (1u << 10)

static unsigned long stat_fail(const char *api) {
    cplus_safe_stat st;
    if (cplus_safe_stats_get(api, &st)) return st.fail;
    return 0;
}

static unsigned long uaf_stat_fail(void) { return stat_fail("use_after_free"); }

/* Verify every stale alias is refused.  `aliases[0]` is the exact freed base
 * pointer (the only alias cplus_safe_ptr_was_freed() can track via the bounded
 * recent-free cache); aliases[1..] are interior or one-past-the-end pointers,
 * whose refusal is proven by SafeCRT failure counters + strict capacity mode. */
static unsigned check_uaf(const case_spec *c, void **aliases, int n,
                          unsigned long long gen) {
    unsigned mask = 0;
    unsigned long s0, s1, m0, m1, c0, c1;
    int i;

    if (!cplus_safe_ptr_was_freed(aliases[0])) mask |= F_WAS_FREED;
    if (cplus_safe_ptr_is_live(aliases[0]))    mask |= F_LIVENESS;

    /* safe_c.h casts the memset/memcpy/memmove macro results to void, so
     * refusal is proven through the library's UAF telemetry: every one of
     * these four base accesses must be recognized as a use-after-free. */
    s0 = uaf_stat_fail();
    memset(aliases[0], 0x5A, c->write_len);
    memcpy(aliases[0], g_src, c->write_len);
    memmove(aliases[0], g_src, c->write_len);
    if (memcmp(aliases[0], g_src, c->write_len) >= 0) mask |= F_MEMCMP;
    s1 = uaf_stat_fail();
    if (s1 < s0 + 4u) mask |= F_UAF_STAT;

    /* Interior / stale-end aliases are not in the bounded recent-free cache,
     * so strict unknown-capacity mode is what refuses them.  Each such alias
     * must add two "mem*_s_auto" failures (memset, memcpy) and one "memcmp_s"
     * failure.  A successful write would also smash the heap and be caught by
     * the crash handlers, but the counters prove refusal without any read of
     * freed memory. */
    m0 = stat_fail("mem*_s_auto");
    c0 = stat_fail("memcmp_s");
    for (i = 1; i < n; ++i) {
        if (cplus_safe_ptr_is_live(aliases[i])) mask |= F_LIVENESS;
        memset(aliases[i], 0x3C, c->write_len);
        memcpy(aliases[i], g_src, c->write_len);
        if (memcmp(aliases[i], g_src, c->write_len) >= 0) mask |= F_INTERIOR;
    }
    m1 = stat_fail("mem*_s_auto");
    c1 = stat_fail("memcmp_s");
    if (n > 1) {
        if (m1 < m0 + 2u * (unsigned long)(n - 1)) mask |= F_INTERIOR;
        if (c1 < c0 + (unsigned long)(n - 1))       mask |= F_INTERIOR;
    }
    if (c->free_timing == FT_GENERATION || c->free_timing == FT_REUSE) {
        if (cplus_safe_ptr_check_generation(aliases[0], gen)) mask |= F_GENERATION;
    }
    return mask;
}

/* "free in one function, use in another" */
static void helper_free(void *p) {
    free(p);   /* ordinary free() -> free_s() */
}

static unsigned helper_use(const case_spec *c, void **aliases, int n,
                           unsigned long long gen) {
    return check_uaf(c, aliases, n, gen);
}

/* ------------------------------------------------- coordinated cross work -- */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t  cv_req;
    pthread_cond_t  cv_ack;
    int   req;
    int   ack;
    int   stop;
    int   idx;
    const case_spec *c;
    void *aliases[MAX_ALIASES];
    int   n;
    unsigned long long gen;
    unsigned result;
} cross_ctx;

static cross_ctx g_x;

static void *cross_worker(void *arg) {
    cross_ctx *x = (cross_ctx *)arg;
    for (;;) {
        pthread_mutex_lock(&x->mu);
        while (!x->req && !x->stop) pthread_cond_wait(&x->cv_req, &x->mu);
        if (x->stop) { pthread_mutex_unlock(&x->mu); break; }
        x->result = check_uaf(x->c, x->aliases, x->n, x->gen);
        x->req = 0;
        x->ack = 1;
        pthread_cond_signal(&x->cv_ack);
        pthread_mutex_unlock(&x->mu);
    }
    return NULL;
}

/* ------------------------------------------------------------ per case ---- */
static unsigned run_case(int idx, int use_cross) {
    const case_spec *c = &g_cases[idx];
    void *base;
    void *aliases[MAX_ALIASES];
    int n = 0, k;
    unsigned long long gen = 0;
    cplus_safe_alloc_meta meta;
    unsigned mask = 0;
    size_t off;

    base = malloc(c->size);            /* ordinary malloc() -> malloc_s() */
    if (!base) return F_ALLOC;

    /* Initialize live memory through the hardened wrapper, then record the
     * generation of the live block before freeing it. */
    memset(base, 0xA5, c->size);   /* live block: hardened write succeeds */
    if (cplus_safe_alloc_get_meta(base, &meta) != 0 || !meta.is_live) {
        mask |= F_META;
    } else {
        gen = meta.generation;
    }

    /* Capture base + interior aliases (all formed while the block is live). */
    aliases[n++] = base;
    if (c->alias_count > 1) {
        off = c->write_offset;
        if (off == 0 || off > c->size) off = c->size / 2u;
        if (off == 0 && c->size > 1u) off = 1u;
        aliases[n++] = (unsigned char *)base + off;
    }
    for (k = 2; k < c->alias_count && n < MAX_ALIASES; ++k) {
        off = (c->size * (size_t)k) / (size_t)c->alias_count;
        if (off == 0 && c->size > 1u) off = 1u;
        if (off > c->size) off = c->size;
        aliases[n++] = (unsigned char *)base + off;
    }
    if (c->free_timing == FT_END && n < MAX_ALIASES) {
        /* One-past-the-end: forming it is legal, it is never dereferenced. */
        aliases[n++] = (unsigned char *)base + c->size;
    }

    helper_free(base);   /* freed in this function ... */

    if (c->free_timing == FT_REUSE) {
        unsigned long s0;
        void *p2;
        /* Check the fresh free cache immediately, before any other free can
         * evict this pointer from the bounded 1024-slot recent-free table. */
        s0 = uaf_stat_fail();
        if (!cplus_safe_ptr_was_freed(base)) mask |= F_WAS_FREED;
        (void)memset(base, 0x5A, 1u);
        if (uaf_stat_fail() < s0 + 1u) mask |= F_UAF_STAT;
        /* Immediate reuse: same size is normally served from the same tcache
         * bin, so the stale alias address becomes the new live block.  The
         * generation must differ, which is how this ABA case is detected. */
        p2 = malloc(c->size);
        if (!p2) {
            mask |= F_ALLOC;
        } else {
            if (!cplus_safe_ptr_is_live(p2)) mask |= F_LIVENESS;
            if (cplus_safe_ptr_check_generation(p2, gen)) mask |= F_GENERATION;
            helper_free(p2);  /* re-arms the free cache for the stale alias */
        }
    }

    if (use_cross) {
        int i;
        pthread_mutex_lock(&g_x.mu);
        g_x.idx = idx;
        g_x.c = c;
        g_x.n = n;
        g_x.gen = gen;
        for (i = 0; i < n; ++i) g_x.aliases[i] = aliases[i];
        g_x.ack = 0;
        g_x.req = 1;
        pthread_cond_signal(&g_x.cv_req);
        while (!g_x.ack) pthread_cond_wait(&g_x.cv_ack, &g_x.mu);
        mask |= g_x.result;
        pthread_mutex_unlock(&g_x.mu);
    } else {
        mask |= helper_use(c, aliases, n, gen);   /* ... used in another */
    }
    return mask;
}

/* --------------------------------------------------------- finding decode -- */
static void report(unsigned mask) {
    if (mask & F_WAS_FREED)  finding("cplus_safe_ptr_was_freed returned false (not freed)");
    if (mask & F_MEMSET)     finding("stale memset was not refused");
    if (mask & F_MEMCPY)     finding("stale memcpy was not refused");
    if (mask & F_MEMMOVE)    finding("stale memmove was not refused");
    if (mask & F_MEMCMP)     finding("stale memcmp was not refused");
    if (mask & F_UAF_STAT)   finding("hardened access did not record a UAF detection");
    if (mask & F_INTERIOR)   finding("interior/end alias write was not refused");
    if (mask & F_GENERATION) finding("stale generation was not detected as mismatched");
    if (mask & F_LIVENESS)   finding("freed pointer still reported live");
    if (mask & F_META)       finding("live allocation metadata unavailable");
    if (mask & F_ALLOC)      finding("allocation or live memset failed");
}

/* ------------------------------------------------------------------ main -- */
int main(void) {
    pthread_t th;
    int i, cross_n = 0;

    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS,  crash_handler);
    signal(SIGILL,  crash_handler);
    signal(SIGFPE,  crash_handler);

    for (i = 0; i < (int)sizeof(g_src); ++i) g_src[i] = (unsigned char)(i * 7u + 3u);

    /* Recoverable reporting only: a refused access must not terminate us. */
    cplus_runtime_set_violation_trap(0);
    /* A freed interior/end alias has no tracked capacity; refuse rather than
     * falling back to raw-C behaviour. */
    cplus_safe_set_unknown_capacity_mode(1);

    build_cases();
    verify_distinct();

    pthread_mutex_init(&g_x.mu, NULL);
    pthread_cond_init(&g_x.cv_req, NULL);
    pthread_cond_init(&g_x.cv_ack, NULL);
    (void)pthread_create(&th, NULL, cross_worker, &g_x);

    for (i = 0; i < NCASES; ++i) {
        int use_cross = (i % 37 == 0);
        set_case_msg(i);
        if (use_cross) ++cross_n;
        report(run_case(i, use_cross));
    }

    pthread_mutex_lock(&g_x.mu);
    g_x.stop = 1;
    pthread_cond_broadcast(&g_x.cv_req);
    pthread_mutex_unlock(&g_x.mu);
    (void)pthread_join(th, NULL);
    pthread_cond_destroy(&g_x.cv_ack);
    pthread_cond_destroy(&g_x.cv_req);
    pthread_mutex_destroy(&g_x.mu);

    if (g_findings == 0) {
        printf("scenario_11: %d cases ok\n", NCASES);
        return 0;
    }
    fprintf(stderr, "FINDING: %d total findings (%d cross-thread cases exercised)\n",
            g_findings, cross_n);
    return 1;
}
