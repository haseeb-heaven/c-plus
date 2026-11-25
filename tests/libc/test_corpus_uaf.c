/* Deterministic adversarial corpus for use-after-free / double-free detection.
 *
 * Standalone C99.  Links only build/libc/libcplus_hardened.a.  No public
 * header is included: every cplus_* entry point used here is declared locally
 * with the exact signature/ABI exported by the hardened allocator.
 *
 * The corpus is table driven.  build_cases() deterministically fills a table
 * of (size, allocator, free_method, query_after_free) tuples; run_case()
 * dispatches on the tuple.  Each case:
 *   1. allocates and verifies liveness + metadata,
 *   2. frees through a CORRECT, ONCE-ONLY defined API,
 *   3. re-queries is_live/size/generation and asserts NOT-live / 0 / rejected,
 *   4. attempts a double free through cplus_free_checked (never raw free),
 *   5. attempts an invalid free through cplus_free_checked (stack pointer).
 *
 * A stale "live" answer or an out-of-band counter change is reported as a
 * FINDING.  Fatal signals are funneled through a handler that also emits a
 * FINDING so a crash is never mistaken for a clean run.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------- local externs -- */
typedef struct cplus_alloc_meta {
    size_t byte_size;
    size_t elem_size;
    size_t elem_count;
    unsigned long long generation;
    int is_live;
} cplus_alloc_meta;

extern void *cplus_malloc(size_t);
extern void *cplus_calloc(size_t, size_t);
extern void *cplus_realloc(void *, size_t);
extern void cplus_free(void *);
extern int cplus_free_checked(void **);
extern int cplus_alloc_get_meta(const void *, cplus_alloc_meta *);
extern int cplus_alloc_is_live(const void *);
extern size_t cplus_alloc_size(const void *);
extern size_t cplus_alloc_elem_size(const void *);
extern size_t cplus_alloc_length(const void *);
extern int cplus_alloc_query_length(const void *, size_t, size_t *);
extern int cplus_alloc_check_generation(const void *, unsigned long long);
extern size_t cplus_alloc_live_count(void);
extern size_t cplus_alloc_live_bytes(void);
extern unsigned long cplus_alloc_double_free_count(void);
extern unsigned long cplus_alloc_invalid_free_count(void);

/* ------------------------------------------------------------ constants -- */
#define N_RANDOM   200
#define N_CALLOC   100
#define N_REALLOC  100
#define N_GEN      100
#define N_INTERIOR 100
#define N_CROSS    100
#define NCASES (N_RANDOM + N_CALLOC + N_REALLOC + N_GEN + N_INTERIOR + N_CROSS)

enum { CAT_RANDOM, CAT_CALLOC, CAT_REALLOC, CAT_GEN, CAT_INTERIOR, CAT_CROSS };
enum { AK_MALLOC, AK_CALLOC, AK_REALLOC_CHAIN };
enum { FK_CHECKED, FK_RAW };
enum { QK_FULL, QK_GEN_FORGERY, QK_INTERIOR, QK_CROSS };

#define CHAIN_MAX 8
#define BAD_SIZE ((size_t)-1)

typedef struct {
    int category;
    int allocator;
    int free_method;
    int query;
    size_t size;                 /* malloc size or calloc element size */
    size_t n;                    /* calloc nmemb */
    int chain_len;
    int chain[CHAIN_MAX];        /* realloc chain sizes */
} case_spec;

static case_spec g_cases[NCASES];
static int g_findings = 0;
static size_t g_base_live, g_base_bytes;
static unsigned long g_base_df, g_base_inv;

/* ------------------------------------------------------------- reporting -- */
static void finding_at(const char *kind, int idx, const char *what) {
    if (g_findings < 64) {
        fprintf(stderr, "FINDING: case %d [%s]: %s\n", idx, kind, what);
    }
    ++g_findings;
}

static void expect_ok(int cond, int idx, const char *what) {
    if (!cond) finding_at("assert", idx, what);
}

static void expect_sz(size_t got, size_t want, int idx, const char *what) {
    if (got != want && g_findings < 64) {
        fprintf(stderr, "FINDING: case %d [%s]: got %lu want %lu\n",
                idx, what, (unsigned long)got, (unsigned long)want);
        ++g_findings;
    }
}

static void crash_handler(int sig) {
    static const char msg[] = "FINDING: fatal signal\n";
    (void)sig;
    (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1u);
    _exit(70);
}

/* --------------------------------------------------------------- RNG ------ */
static unsigned long long g_rng = 0x12345678ULL;
static unsigned long long xr(void) {
    g_rng = g_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_rng >> 17;
}
static size_t rnd_sz(size_t n) { return (size_t)(xr() % (unsigned long long)n); }

/* ------------------------------------------------------- case generation -- */
static void build_cases(void) {
    int i = 0, k;
    unsigned long long r;
    size_t nb, es;

    /* 200 random sizes 0..8192 */
    for (k = 0; k < N_RANDOM; ++k, ++i) {
        case_spec *c = &g_cases[i];
        memset(c, 0, sizeof(*c));
        c->category = CAT_RANDOM;
        c->allocator = AK_MALLOC;
        c->free_method = (k & 1) ? FK_RAW : FK_CHECKED;
        c->query = QK_FULL;
        c->size = rnd_sz(8193u);
    }

    /* 100 calloc (n,size) shapes; product kept <= 8192, includes zero shapes */
    for (k = 0; k < N_CALLOC; ++k, ++i) {
        case_spec *c = &g_cases[i];
        memset(c, 0, sizeof(*c));
        c->category = CAT_CALLOC;
        c->allocator = AK_CALLOC;
        c->free_method = (k & 1) ? FK_RAW : FK_CHECKED;
        c->query = QK_FULL;
        if ((k % 10) == 0) {
            c->n = (k % 20) ? 0u : rnd_sz(17u); /* n==0 or size==0 */
            c->size = (k % 20) ? rnd_sz(8193u) : 0u;
        } else {
            c->n = 1u + rnd_sz(64u);
            c->size = rnd_sz(129u);
            while (c->size > 0u && c->n > 8192u / c->size) c->size /= 2u;
        }
    }

    /* 100 realloc grow/shrink chains */
    for (k = 0; k < N_REALLOC; ++k, ++i) {
        case_spec *c = &g_cases[i];
        size_t cur;
        int j;
        memset(c, 0, sizeof(*c));
        c->category = CAT_REALLOC;
        c->allocator = AK_REALLOC_CHAIN;
        c->free_method = (k & 1) ? FK_RAW : FK_CHECKED;
        c->query = QK_FULL;
        c->chain_len = 3 + (int)rnd_sz(4u); /* 3..6 */
        cur = rnd_sz(4097u);
        c->chain[0] = (int)cur;
        for (j = 1; j < c->chain_len; ++j) {
            size_t delta = 1u + rnd_sz(2048u);
            if ((j & 1) == 1) { /* grow */
                cur = (cur > 8192u - delta) ? 8192u : cur + delta;
            } else { /* shrink */
                cur = (cur > delta) ? cur - delta : 0u;
            }
            c->chain[j] = (int)cur;
        }
    }

    /* 100 generation forgery attempts (valid +/-1, 0, ULLONG_MAX) */
    for (k = 0; k < N_GEN; ++k, ++i) {
        case_spec *c = &g_cases[i];
        memset(c, 0, sizeof(*c));
        c->category = CAT_GEN;
        c->allocator = (k & 1) ? AK_CALLOC : AK_MALLOC;
        c->free_method = (k & 1) ? FK_RAW : FK_CHECKED;
        c->query = QK_GEN_FORGERY;
        c->size = rnd_sz(8193u);
        c->n = 1u + rnd_sz(16u);
        r = xr();
        if (r & 1u) c->size = 0u; /* occasional zero-shape */
    }

    /* 100 interior-pointer queries (ptr+1, ptr-1 where safe, ptr+size) */
    for (k = 0; k < N_INTERIOR; ++k, ++i) {
        case_spec *c = &g_cases[i];
        memset(c, 0, sizeof(*c));
        c->category = CAT_INTERIOR;
        c->allocator = (k & 1) ? AK_CALLOC : AK_MALLOC;
        c->free_method = (k & 1) ? FK_RAW : FK_CHECKED;
        c->query = QK_INTERIOR;
        c->size = 1u + rnd_sz(4096u);
        c->n = 1u + rnd_sz(16u);
    }

    /* 100 cross-thread free/query sequences */
    for (k = 0; k < N_CROSS; ++k, ++i) {
        case_spec *c = &g_cases[i];
        memset(c, 0, sizeof(*c));
        c->category = CAT_CROSS;
        c->allocator = (k % 3 == 0) ? AK_CALLOC : AK_MALLOC;
        c->free_method = (k & 1) ? FK_RAW : FK_CHECKED;
        c->query = QK_CROSS;
        c->size = rnd_sz(8193u);
        c->n = 1u + rnd_sz(16u);
    }

    if (i != NCASES) {
        fprintf(stderr, "FINDING: case table build mismatch (%d != %d)\n", i, NCASES);
        g_findings++;
    }

    /* Silence unused-variable diagnostics for helper locals kept for clarity. */
    nb = 0u; es = 0u; (void)nb; (void)es;
}

/* ------------------------------------------------------------ verification -- */
/* Returns the observed generation (0 if get_meta failed). */
static unsigned long long verify_live(const void *p, size_t eb, size_t ee,
                                      size_t ec, int idx) {
    cplus_alloc_meta m;
    size_t cnt = 12345u;
    unsigned long long g = 0;

    expect_ok(cplus_alloc_is_live(p) == 1, idx, "is_live(alloc) != 1");
    expect_sz(cplus_alloc_size(p), eb, idx, "size(alloc)");
    expect_sz(cplus_alloc_elem_size(p), ee, idx, "elem_size(alloc)");
    expect_sz(cplus_alloc_length(p), ec, idx, "length(alloc)");
    expect_ok(cplus_alloc_query_length(p, 1u, &cnt) == 0 && cnt == eb,
              idx, "query_length(alloc)");

    if (cplus_alloc_get_meta(p, &m) == 0) {
        expect_ok(m.is_live == 1, idx, "meta.is_live(alloc) != 1");
        expect_sz(m.byte_size, eb, idx, "meta.byte_size(alloc)");
        expect_sz(m.elem_size, ee, idx, "meta.elem_size(alloc)");
        expect_sz(m.elem_count, ec, idx, "meta.elem_count(alloc)");
        g = m.generation;
    } else {
        finding_at("assert", idx, "get_meta(alloc) rejected");
        memset(&m, 0, sizeof(m));
    }
    expect_ok(g > 0u, idx, "generation == 0");
    expect_ok(cplus_alloc_check_generation(p, g) == 1, idx, "generation(valid) mismatch");

    /* Generation forgery while live: all wrong values must be rejected. */
    expect_ok(cplus_alloc_check_generation(p, g + 1u) == 0, idx, "gen+1 accepted (live)");
    if (g > 0u)
        expect_ok(cplus_alloc_check_generation(p, g - 1u) == 0, idx, "gen-1 accepted (live)");
    expect_ok(cplus_alloc_check_generation(p, 0u) == 0, idx, "gen 0 accepted (live)");
    expect_ok(cplus_alloc_check_generation(p, ULLONG_MAX) == 0, idx, "gen MAX accepted (live)");
    return g;
}

static void verify_dead(const void *p, unsigned long long gen, int idx) {
    cplus_alloc_meta m;
    size_t cnt = 999u;
    int rc;

    expect_ok(cplus_alloc_is_live(p) == 0, idx, "STALE LIVE after free");
    expect_sz(cplus_alloc_size(p), 0u, idx, "size after free");
    expect_sz(cplus_alloc_elem_size(p), 0u, idx, "elem_size after free");
    expect_sz(cplus_alloc_length(p), 0u, idx, "length after free");
    rc = cplus_alloc_query_length(p, 1u, &cnt);
    expect_ok(rc != 0 && cnt == 0u, idx, "query_length after free not rejected");

    memset(&m, 0xCD, sizeof(m));
    rc = cplus_alloc_get_meta(p, &m);
    expect_ok(rc != 0, idx, "get_meta after free accepted");
    expect_ok(m.is_live == 0 && m.byte_size == 0u && m.elem_size == 0u &&
              m.elem_count == 0u && m.generation == 0u,
              idx, "meta after free not cleared");

    /* Forged/valid generations must all be rejected against a freed pointer. */
    expect_ok(cplus_alloc_check_generation(p, gen) == 0, idx, "gen(valid) accepted after free");
    expect_ok(cplus_alloc_check_generation(p, gen + 1u) == 0, idx, "gen+1 accepted after free");
    if (gen > 0u)
        expect_ok(cplus_alloc_check_generation(p, gen - 1u) == 0, idx, "gen-1 accepted after free");
    expect_ok(cplus_alloc_check_generation(p, 0u) == 0, idx, "gen 0 accepted after free");
    expect_ok(cplus_alloc_check_generation(p, ULLONG_MAX) == 0, idx, "gen MAX accepted after free");
}

static void free_once(void **slot, int method) {
    if (method == FK_CHECKED) {
        (void)cplus_free_checked(slot);
    } else {
        cplus_free(*slot); /* exactly one defined raw free */
        *slot = NULL;
    }
}

/* Double free must be refused by the checked API and accounted for. */
static void do_double_free(void *p, int idx) {
    void *alias = p;
    unsigned long before = cplus_alloc_double_free_count();
    int rc = cplus_free_checked(&alias);
    expect_ok(rc != 0, idx, "double free was accepted");
    expect_ok(alias == NULL, idx, "double free did not clear inout");
    expect_ok(cplus_alloc_double_free_count() == before + 1u,
              idx, "double_free_count did not increase via checked API");
}

/* Invalid (never-allocated) free must be refused and accounted for. */
static void do_invalid_free(int idx) {
    int stack_obj = 7;
    void *bad = &stack_obj;
    unsigned long before = cplus_alloc_invalid_free_count();
    int rc = cplus_free_checked(&bad);
    expect_ok(rc != 0, idx, "invalid free was accepted");
    expect_ok(bad == NULL, idx, "invalid free did not clear inout");
    expect_ok(cplus_alloc_invalid_free_count() == before + 1u,
              idx, "invalid_free_count did not increase via checked API");
}

static void check_lifecycle_balance(int idx) {
    expect_ok(cplus_alloc_live_count() == g_base_live, idx, "live_count leak/stale entry");
    expect_ok(cplus_alloc_live_bytes() == g_base_bytes, idx, "live_bytes leak/stale entry");
}

/* -------------------------------------------------------------- categories -- */
static void run_random(const case_spec *c, int idx) {
    void *slot = cplus_malloc(c->size);
    size_t eb = c->size ? c->size : 1u;
    unsigned long long gen;
    void *alias;
    expect_ok(slot != NULL, idx, "malloc returned NULL");
    if (!slot) return;
    gen = verify_live(slot, eb, 1u, eb, idx);
    alias = slot;
    free_once(&slot, c->free_method);
    (void)alias;
    verify_dead(alias, gen, idx);
    do_double_free(alias, idx);
    do_invalid_free(idx);
    check_lifecycle_balance(idx);
}

static void run_calloc(const case_spec *c, int idx) {
    void *slot = cplus_calloc(c->n, c->size);
    size_t eb, ee, ec;
    unsigned long long gen;
    void *alias;
    if (c->n == 0u || c->size == 0u) {
        eb = 1u; ee = 1u; ec = 1u;
    } else {
        eb = c->n * c->size; ee = c->size; ec = c->n;
    }
    expect_ok(slot != NULL, idx, "calloc returned NULL");
    if (!slot) return;
    gen = verify_live(slot, eb, ee, ec, idx);
    alias = slot;
    free_once(&slot, c->free_method);
    verify_dead(alias, gen, idx);
    do_double_free(alias, idx);
    do_invalid_free(idx);
    check_lifecycle_balance(idx);
}

static void run_realloc(const case_spec *c, int idx) {
    void *slot = cplus_malloc((size_t)c->chain[0] ? (size_t)c->chain[0] : 1u);
    size_t eb = (size_t)c->chain[0] ? (size_t)c->chain[0] : 1u;
    unsigned long long gen;
    void *alias;
    int j;
    expect_ok(slot != NULL, idx, "realloc-chain base malloc returned NULL");
    if (!slot) return;
    gen = verify_live(slot, eb, 1u, eb, idx);

    for (j = 1; j < c->chain_len; ++j) {
        void *old = slot;
        size_t nb = (size_t)c->chain[j] ? (size_t)c->chain[j] : 1u;
        slot = cplus_realloc(slot, (size_t)c->chain[j]);
        expect_ok(slot != NULL, idx, "realloc in chain returned NULL");
        if (!slot) { check_lifecycle_balance(idx); return; }
        gen = verify_live(slot, nb, 1u, nb, idx);
        if ((char *)slot != (char *)old) {
            /* A moved-away address must not keep claiming liveness. */
            expect_ok(cplus_alloc_is_live(old) == 0, idx, "old realloc address still live");
            expect_sz(cplus_alloc_size(old), 0u, idx, "old realloc address size");
        }
    }
    alias = slot;
    free_once(&slot, c->free_method);
    verify_dead(alias, gen, idx);
    do_double_free(alias, idx);
    do_invalid_free(idx);
    check_lifecycle_balance(idx);
}

static void run_gen(const case_spec *c, int idx) {
    void *slot;
    size_t eb, ee, ec;
    unsigned long long gen, forged[8];
    void *alias;
    size_t k;
    if (c->allocator == AK_CALLOC) {
        slot = cplus_calloc(c->n, c->size);
        if (c->size == 0u) { eb = 1u; ee = 1u; ec = 1u; }
        else { eb = c->n * c->size; ee = c->size; ec = c->n; }
    } else {
        slot = cplus_malloc(c->size);
        eb = c->size ? c->size : 1u; ee = 1u; ec = eb;
    }
    expect_ok(slot != NULL, idx, "gen-case alloc returned NULL");
    if (!slot) return;
    gen = verify_live(slot, eb, ee, ec, idx);

    forged[0] = gen;
    forged[1] = gen + 1u;
    forged[2] = (gen > 0u) ? gen - 1u : 0u;
    forged[3] = 0u;
    forged[4] = ULLONG_MAX;
    forged[5] = gen ^ (1ULL << 63);
    forged[6] = gen + 0x9E3779B97F4A7C15ULL;
    forged[7] = ~gen;
    for (k = 0; k < 8u; ++k) {
        int want = (k == 0u) ? 1 : 0;
        expect_ok(cplus_alloc_check_generation(slot, forged[k]) == want,
                  idx, "generation forgery while live");
    }

    alias = slot;
    free_once(&slot, c->free_method);
    for (k = 0; k < 8u; ++k)
        expect_ok(cplus_alloc_check_generation(alias, forged[k]) == 0,
                  idx, "generation forgery accepted after free");
    verify_dead(alias, gen, idx);
    do_double_free(alias, idx);
    do_invalid_free(idx);
    check_lifecycle_balance(idx);
}

static void run_interior(const case_spec *c, int idx) {
    void *slot;
    size_t eb, ee, ec;
    unsigned long long gen;
    const void *probes[3];
    int np = 0, j;
    void *alias;
    cplus_alloc_meta m;
    if (c->allocator == AK_CALLOC) {
        slot = cplus_calloc(c->n, c->size);
        eb = c->n * c->size; ee = c->size; ec = c->n;
    } else {
        slot = cplus_malloc(c->size);
        eb = c->size; ee = 1u; ec = eb;
    }
    expect_ok(slot != NULL, idx, "interior-case alloc returned NULL");
    if (!slot) return;
    gen = verify_live(slot, eb, ee, ec, idx);

    probes[np++] = (const char *)slot + 1;
    if ((uintptr_t)slot > 4096u) probes[np++] = (const char *)slot - 1;
    probes[np++] = (const char *)slot + eb;
    for (j = 0; j < np; ++j) {
        expect_ok(cplus_alloc_is_live(probes[j]) == 0, idx, "interior pointer reported LIVE");
        expect_sz(cplus_alloc_size(probes[j]), 0u, idx, "interior pointer size");
        expect_sz(cplus_alloc_elem_size(probes[j]), 0u, idx, "interior pointer elem_size");
        expect_sz(cplus_alloc_length(probes[j]), 0u, idx, "interior pointer length");
        memset(&m, 0xCD, sizeof(m));
        expect_ok(cplus_alloc_get_meta(probes[j], &m) != 0 && m.is_live == 0,
                  idx, "interior pointer meta accepted");
        expect_ok(cplus_alloc_check_generation(probes[j], gen) == 0,
                  idx, "interior pointer generation accepted");
    }

    alias = slot;
    free_once(&slot, c->free_method);
    verify_dead(alias, gen, idx);

    /* Interior pointers stay inert after free too (queries only). */
    for (j = 0; j < np; ++j)
        expect_ok(cplus_alloc_is_live(probes[j]) == 0, idx, "interior LIVE after free");

    /* Free-checked on an unaligned interior pointer is an invalid free. */
    {
        void *bad = (char *)alias + 1;
        unsigned long before = cplus_alloc_invalid_free_count();
        int rc = cplus_free_checked(&bad);
        expect_ok(rc != 0 && bad == NULL, idx, "interior free accepted");
        expect_ok(cplus_alloc_invalid_free_count() == before + 1u,
                  idx, "interior invalid_free_count not increased");
    }
    do_double_free(alias, idx);
    do_invalid_free(idx);
    check_lifecycle_balance(idx);
}

/* --------------------------------------------------------------- cross-thread -- */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv_work;
    pthread_cond_t cv_done;
    int work_ready;
    int done;
    int stop;
    void *ptr;
    int mode;       /* 0: worker free+query; 1: worker query; 2: worker double-free */
    int rc;
    int live_seen;
    size_t size_seen;
} cross_ctx;

static cross_ctx g_cross;

static void cross_init(void) {
    memset(&g_cross, 0, sizeof(g_cross));
    (void)pthread_mutex_init(&g_cross.m, NULL);
    (void)pthread_cond_init(&g_cross.cv_work, NULL);
    (void)pthread_cond_init(&g_cross.cv_done, NULL);
}

static void *cross_worker(void *arg) {
    cross_ctx *c = (cross_ctx *)arg;
    for (;;) {
        void *p;
        int mode, rc = -1, live = -1;
        size_t sz = BAD_SIZE;

        (void)pthread_mutex_lock(&c->m);
        while (!c->work_ready && !c->stop)
            (void)pthread_cond_wait(&c->cv_work, &c->m);
        if (c->stop) {
            (void)pthread_mutex_unlock(&c->m);
            break;
        }
        p = c->ptr;
        mode = c->mode;
        (void)pthread_mutex_unlock(&c->m);

        /* Metadata table has its own internal lock; our mutex above makes the
         * handoff race-free, so these are coordinated, not genuine races. */
        if (mode == 0) {
            void *tmp = p;
            rc = cplus_free_checked(&tmp);
            live = cplus_alloc_is_live(p);
            sz = cplus_alloc_size(p);
        } else if (mode == 1) {
            live = cplus_alloc_is_live(p);
            sz = cplus_alloc_size(p);
        } else if (mode == 2) {
            void *tmp = p; /* main already freed p */
            rc = cplus_free_checked(&tmp);
        }

        (void)pthread_mutex_lock(&c->m);
        c->rc = rc;
        c->live_seen = live;
        c->size_seen = sz;
        c->work_ready = 0;
        c->done = 1;
        (void)pthread_cond_signal(&c->cv_done);
        (void)pthread_mutex_unlock(&c->m);
    }
    return NULL;
}

static void cross_dispatch(void *p, int mode, int *rc, int *live, size_t *sz) {
    cross_ctx *c = &g_cross;
    (void)pthread_mutex_lock(&c->m);
    c->ptr = p;
    c->mode = mode;
    c->rc = -1;
    c->live_seen = -1;
    c->size_seen = BAD_SIZE;
    c->done = 0;
    c->work_ready = 1;
    (void)pthread_cond_signal(&c->cv_work);
    (void)pthread_mutex_unlock(&c->m);

    (void)pthread_mutex_lock(&c->m);
    while (!c->done)
        (void)pthread_cond_wait(&c->cv_done, &c->m);
    *rc = c->rc;
    *live = c->live_seen;
    *sz = c->size_seen;
    c->done = 0;
    (void)pthread_mutex_unlock(&c->m);
}

static void cross_stop(void) {
    cross_ctx *c = &g_cross;
    (void)pthread_mutex_lock(&c->m);
    c->stop = 1;
    (void)pthread_cond_broadcast(&c->cv_work);
    (void)pthread_mutex_unlock(&c->m);
}

static void run_cross(const case_spec *c, int idx) {
    void *slot;
    size_t eb, ee, ec;
    unsigned long long gen;
    void *alias;
    int mode = idx % 3;
    int rc = -1, live = -1, mrc, mlive;
    size_t sz = BAD_SIZE, msz;
    unsigned long df_before = cplus_alloc_double_free_count();

    if (c->allocator == AK_CALLOC) {
        slot = cplus_calloc(c->n, c->size);
        if (c->size == 0u) { eb = 1u; ee = 1u; ec = 1u; }
        else { eb = c->n * c->size; ee = c->size; ec = c->n; }
    } else {
        slot = cplus_malloc(c->size);
        eb = c->size ? c->size : 1u; ee = 1u; ec = eb;
    }
    expect_ok(slot != NULL, idx, "cross-case alloc returned NULL");
    if (!slot) return;
    gen = verify_live(slot, eb, ee, ec, idx);
    alias = slot;

    if (mode == 0) {
        /* Worker frees and queries; main queries + double-frees afterwards. */
        cross_dispatch(slot, 0, &rc, &live, &sz);
        expect_ok(rc == 0, idx, "worker free_checked failed");
        expect_ok(live == 0, idx, "worker saw STALE LIVE after free");
        expect_sz(sz, 0u, idx, "worker size after free");
        verify_dead(alias, gen, idx);
        do_double_free(alias, idx);
    } else if (mode == 1) {
        /* Main frees; worker queries from the other thread. */
        free_once(&slot, c->free_method);
        cross_dispatch(alias, 1, &mrc, &mlive, &msz);
        expect_ok(mlive == 0, idx, "worker saw STALE LIVE after main free");
        expect_sz(msz, 0u, idx, "worker size after main free");
        verify_dead(alias, gen, idx);
        do_double_free(alias, idx);
    } else {
        /* Main frees; worker performs the double-free attempt. */
        free_once(&slot, c->free_method);
        cross_dispatch(alias, 2, &rc, &live, &sz);
        expect_ok(rc != 0, idx, "worker double free was accepted");
        verify_dead(alias, gen, idx);
        expect_ok(cplus_alloc_double_free_count() == df_before + 1u,
                  idx, "cross double_free_count not increased via checked API");
    }
    do_invalid_free(idx);
    check_lifecycle_balance(idx);
}

/* ----------------------------------------------------------------- driver -- */
static void run_case(int idx) {
    const case_spec *c = &g_cases[idx];
    switch (c->category) {
        case CAT_RANDOM:   run_random(c, idx);   break;
        case CAT_CALLOC:   run_calloc(c, idx);   break;
        case CAT_REALLOC:  run_realloc(c, idx);  break;
        case CAT_GEN:      run_gen(c, idx);      break;
        case CAT_INTERIOR: run_interior(c, idx); break;
        case CAT_CROSS:    run_cross(c, idx);    break;
        default:
            finding_at("assert", idx, "unknown category");
            break;
    }
}

int main(void) {
    struct sigaction sa;
    pthread_t w1, w2;
    int i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_handler;
    (void)sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGSEGV, &sa, NULL);
    (void)sigaction(SIGBUS, &sa, NULL);
    (void)sigaction(SIGABRT, &sa, NULL);
    (void)sigaction(SIGFPE, &sa, NULL);

    build_cases();
    g_base_live = cplus_alloc_live_count();
    g_base_bytes = cplus_alloc_live_bytes();
    g_base_df = cplus_alloc_double_free_count();
    g_base_inv = cplus_alloc_invalid_free_count();

    cross_init();
    (void)pthread_create(&w1, NULL, cross_worker, &g_cross);
    (void)pthread_create(&w2, NULL, cross_worker, &g_cross);

    for (i = 0; i < NCASES; ++i) run_case(i);

    cross_stop();
    (void)pthread_join(w1, NULL);
    (void)pthread_join(w2, NULL);

    check_lifecycle_balance(-1);
    expect_ok(cplus_alloc_double_free_count() == g_base_df + (unsigned long)NCASES,
              -1, "final double_free_count total");
    expect_ok(cplus_alloc_invalid_free_count() ==
              g_base_inv + (unsigned long)NCASES + (unsigned long)N_INTERIOR,
              -1, "final invalid_free_count total");

    if (g_findings == 0) {
        printf("corpus_uaf: %d cases ok\n", NCASES);
        return 0;
    }
    fprintf(stderr, "FINDING: %d total findings\n", g_findings);
    return 1;
}
