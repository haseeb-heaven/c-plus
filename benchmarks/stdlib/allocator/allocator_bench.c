/* allocator_bench.c - reproducible C+ allocator performance benchmark.
 *
 * Subject under test: the C+ hardened libc allocator from cplus_libc.h
 * (cplus_malloc/cplus_calloc/cplus_realloc/cplus_free and the metadata query
 * API).  That is a first-party C+ stdlib API; no third-party benchmark
 * framework and no scripting language are used.
 *
 * Three allocator variants are compared on every workload:
 *
 *   raw     - host malloc/realloc/free, NO C+ side table (tracking disabled).
 *             Control/baseline only; not a C+ allocator.
 *   tracked - cplus_malloc/cplus_calloc/cplus_realloc/cplus_free.  Every call
 *             maintains the thread-safe metadata side table (tracking enabled)
 *             but the benchmark makes no debug queries.
 *   query   - same tracked allocator PLUS a debug query after every operation
 *             (cplus_alloc_size, cplus_alloc_is_live, cplus_alloc_live_count).
 *             The delta (query - tracked) isolates the cost of the debug
 *             metadata queries; (tracked - raw) bounds the cost of keeping the
 *             side table (it also includes a global lock + checks, see NOTES).
 *
 * Variants are interleaved round-robin across >=7 measured rounds using a
 * fixed xorshift32 seed (reset every round), with untimed warmup rounds.
 * Only the designated run phase is timed; prepare/finish are untimed.  The
 * program itself computes min / median(p50) / max / p95, so no Python (or any
 * other external post-processing) is required.
 *
 * Build (from repo root, using the supported C+ compiler):
 *   build/c+ build benchmarks/stdlib/allocator/allocator_bench.c \
 *     --release --sanitize=none -Ilibc/install-include \
 *     -o benchmarks/stdlib/allocator/allocator_bench
 *
 * Run (see run.sh for the full matrix):
 *   ./benchmarks/stdlib/allocator/allocator_bench \
 *     --workload small --runs 9 --warmup 2 --ops 200000 --seed 314159265
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "cplus_libc.h"

/* ------------------------------------------------------------------ */
/* Deterministic PRNG (fixed seed, reset per measured round)           */
/* ------------------------------------------------------------------ */
static uint32_t g_rng;
static void rng_seed(uint32_t s) { g_rng = s ? s : 1u; }
static uint32_t rng_next(void) {
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

/* Volatile sinks keep the allocator calls from being optimized away. */
static volatile uint64_t g_sink;
static volatile size_t   g_query_sink;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
}

/* ------------------------------------------------------------------ */
/* Allocator variant abstraction                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    void *(*alloc_fn)(size_t);
    void *(*realloc_fn)(void *, size_t);
    void (*free_fn)(void *);
    int query; /* 1 => issue debug metadata queries on every op */
} allocator_api;

static void do_queries(const allocator_api *a, void *p) {
    if (!a->query)
        return;
    g_query_sink += cplus_alloc_size(p);
    g_query_sink += (size_t)cplus_alloc_is_live(p);
    g_query_sink += cplus_alloc_live_count();
}

/* A debug query that can be issued on the free path (global liveness count). */
static void do_free_queries(const allocator_api *a) {
    if (!a->query)
        return;
    g_query_sink += cplus_alloc_live_count();
}

/* Host allocation for benchmark bookkeeping arrays only (never timed). */
static void *host_alloc(size_t n) { return malloc(n); }
static void  host_free(void *p) { free(p); }

static const allocator_api VARIANTS[] = {
    {"raw",     malloc,       realloc,       free,       0},
    {"tracked", cplus_malloc, cplus_realloc, cplus_free, 0},
    {"query",   cplus_malloc, cplus_realloc, cplus_free, 1},
};
#define NVARIANTS ((int)(sizeof(VARIANTS) / sizeof(VARIANTS[0])))

/* ------------------------------------------------------------------ */
/* Workloads.  prepare/finish are untimed; only run is timed.          */
/* `param` is a size hint (bytes) where relevant.                      */
/* ------------------------------------------------------------------ */
typedef void (*phase_fn)(const allocator_api *, size_t, size_t);

/* Shared scratch state (single-threaded benchmark). */
static void  **g_arr;
static size_t  g_arr_ops;

/* ---- alloc: allocation throughput (free happens in finish) ------- */
static void prep_alloc(const allocator_api *a, size_t ops, size_t param) {
    (void)a; (void)param;
    g_arr_ops = ops;
    g_arr = (void **)host_alloc(ops * sizeof(void *));
    if (!g_arr) { fprintf(stderr, "bookkeeping alloc failed\n"); exit(3); }
}
static void run_alloc(const allocator_api *a, size_t ops, size_t param) {
    size_t i;
    size_t sz = param ? param : 32u;
    for (i = 0; i < ops; ++i) {
        void *p = a->alloc_fn(sz);
        if (!p) { fprintf(stderr, "alloc failed at %zu\n", i); exit(3); }
        g_arr[i] = p;
        do_queries(a, p);
        g_sink ^= (uint64_t)(uintptr_t)p;
    }
}
static void fin_alloc(const allocator_api *a, size_t ops, size_t param) {
    size_t i;
    (void)param;
    for (i = 0; i < g_arr_ops && i < ops; ++i)
        a->free_fn(g_arr[i]);
    host_free(g_arr);
    g_arr = NULL;
}

/* ---- free: free throughput (alloc happens in prepare) ------------ */
static void prep_free(const allocator_api *a, size_t ops, size_t param) {
    size_t i;
    size_t sz = param ? param : 32u;
    g_arr_ops = ops;
    g_arr = (void **)host_alloc(ops * sizeof(void *));
    if (!g_arr) { fprintf(stderr, "bookkeeping alloc failed\n"); exit(3); }
    for (i = 0; i < ops; ++i) {
        void *p = a->alloc_fn(sz);
        if (!p) { fprintf(stderr, "alloc failed at %zu\n", i); exit(3); }
        g_arr[i] = p;
        do_queries(a, p);
    }
}
static void run_free(const allocator_api *a, size_t ops, size_t param) {
    size_t i;
    (void)param;
    for (i = 0; i < ops; ++i)
        a->free_fn(g_arr[i]);
}
static void fin_free(const allocator_api *a, size_t ops, size_t param) {
    (void)a; (void)ops; (void)param;
    host_free(g_arr);
    g_arr = NULL;
}

/* ---- realloc: repeated grow/shrink of one block ------------------- */
static void  *g_realloc_p;
static void prep_realloc(const allocator_api *a, size_t ops, size_t param) {
    size_t sz = param ? param : 65536u;
    (void)ops;
    g_realloc_p = a->alloc_fn(sz);
    if (!g_realloc_p) { fprintf(stderr, "alloc failed\n"); exit(3); }
}
static void run_realloc(const allocator_api *a, size_t ops, size_t param) {
    size_t i;
    size_t sz = param ? param : 65536u;
    void *p = g_realloc_p;
    for (i = 0; i < ops; ++i) {
        size_t target = (i & 1u) ? sz : sz * 2u;
        void *q = a->realloc_fn(p, target);
        if (!q) { fprintf(stderr, "realloc failed at %zu\n", i); exit(3); }
        p = q;
        do_queries(a, p);
    }
    g_realloc_p = p;
    g_sink ^= (uint64_t)(uintptr_t)p;
}
static void fin_realloc(const allocator_api *a, size_t ops, size_t param) {
    (void)ops; (void)param;
    a->free_fn(g_realloc_p);
    g_realloc_p = NULL;
}

/* ---- small: 8..64 byte alloc/free pairs --------------------------- */
static void run_small(const allocator_api *a, size_t ops, size_t param) {
    size_t i;
    (void)param;
    for (i = 0; i < ops; ++i) {
        size_t sz = 8u + (size_t)(rng_next() & 7u) * 8u; /* 8,16,...,64 */
        unsigned char *p = (unsigned char *)a->alloc_fn(sz);
        if (!p) { fprintf(stderr, "small alloc failed at %zu\n", i); exit(3); }
        do_queries(a, p);
        p[0] = (unsigned char)(g_rng & 0xffu);
        g_sink ^= (uint64_t)(uintptr_t)p;
        a->free_fn(p);
    }
}

/* ---- large: 64 KiB .. 512 KiB alloc/free pairs -------------------- */
static void run_large(const allocator_api *a, size_t ops, size_t param) {
    size_t i;
    (void)param;
    for (i = 0; i < ops; ++i) {
        size_t sz = 65536u + (size_t)(rng_next() & 7u) * 65536u; /* >=64 KiB */
        unsigned char *p = (unsigned char *)a->alloc_fn(sz);
        if (!p) { fprintf(stderr, "large alloc failed at %zu\n", i); exit(3); }
        do_queries(a, p);
        p[0] = (unsigned char)(g_rng & 0xffu);
        p[sz - 1] = p[0];
        g_sink ^= (uint64_t)(uintptr_t)p;
        a->free_fn(p);
    }
}

/* ---- grow: push `ops` ints into a realloc-doubled buffer ---------- */
/* Mirrors the growth strategy of the C+ vector (stdlib/vector.hp). */
static void run_grow(const allocator_api *a, size_t ops, size_t param) {
    size_t cap = 8u, n = 0, i;
    int *buf;
    (void)param;
    buf = (int *)a->alloc_fn(cap * sizeof(int));
    if (!buf) { fprintf(stderr, "grow alloc failed\n"); exit(3); }
    for (i = 0; i < ops; ++i) {
        if (n == cap) {
            int *nb;
            cap *= 2u;
            nb = (int *)a->realloc_fn(buf, cap * sizeof(int));
            if (!nb) { fprintf(stderr, "grow realloc failed at %zu\n", i); exit(3); }
            buf = nb;
            do_queries(a, buf);
        }
        buf[n++] = (int)rng_next();
        g_sink ^= (uint64_t)(uint32_t)buf[n - 1];
    }
    a->free_fn(buf);
}

typedef struct {
    const char *name;
    phase_fn prepare;
    phase_fn run;
    phase_fn finish;
    size_t def_ops;
} workload_desc;

static const workload_desc WORKLOADS[] = {
    {"alloc",   prep_alloc,   run_alloc,   fin_alloc,   200000},
    {"free",    prep_free,    run_free,    fin_free,    200000},
    {"realloc", prep_realloc, run_realloc, fin_realloc, 200000},
    {"small",   NULL,         run_small,   NULL,        200000},
    {"large",   NULL,         run_large,   NULL,        4000},
    {"grow",    NULL,         run_grow,    NULL,        200000},
};
#define NWORKLOADS ((int)(sizeof(WORKLOADS) / sizeof(WORKLOADS[0])))

/* ------------------------------------------------------------------ */
/* Statistics                                                          */
/* ------------------------------------------------------------------ */
static int cmp_double(const void *pa, const void *pb) {
    double a = *(const double *)pa, b = *(const double *)pb;
    return (a > b) - (a < b);
}

static void print_stats(const char *wl, const allocator_api *a,
                        double *t, int runs, size_t ops) {
    double min, med, max, p95, nsop, mops;
    qsort(t, (size_t)runs, sizeof(double), cmp_double);
    min = t[0];
    max = t[runs - 1];
    med = t[runs / 2];
    p95 = t[(int)(0.95 * (double)(runs - 1))]; /* nearest-rank */
    nsop = med * 1.0e9 / (double)ops;
    mops = (double)ops / med / 1.0e6;
    printf("%s,%s,%d,%zu,%.9f,%.9f,%.9f,%.9f,%.2f,%.2f\n",
           wl, a->name, runs, ops, min, med, max, p95, nsop, mops);
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */
static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [--workload NAME] [--variant NAME] [--runs N] "
        "[--warmup N] [--ops N] [--size N] [--seed N]\n"
        "  workloads: alloc free realloc small large grow (default: all)\n"
        "  variants:  raw tracked query (default: all, interleaved)\n"
        "  defaults:  runs=9 warmup=2 ops=200000(size auto for large) "
        "seed=314159265\n",
        prog);
}

int main(int argc, char **argv) {
    const char *workload = "all";
    const char *variant = "all";
    int runs = 9, warmup = 2;
    size_t ops = 200000, size_hint = 0;
    int ops_set = 0;
    uint32_t seed = 314159265u;
    int wi, r;

    for (int i = 1; i < argc; ++i) {
        const char *k = argv[i];
        if (!strcmp(k, "--workload") && i + 1 < argc) workload = argv[++i];
        else if (!strcmp(k, "--variant") && i + 1 < argc) variant = argv[++i];
        else if (!strcmp(k, "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else if (!strcmp(k, "--warmup") && i + 1 < argc) warmup = atoi(argv[++i]);
        else if (!strcmp(k, "--ops") && i + 1 < argc) { ops = (size_t)strtoul(argv[++i], NULL, 10); ops_set = 1; }
        else if (!strcmp(k, "--size") && i + 1 < argc) size_hint = (size_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(k, "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(k, "--help")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "unknown option: %s\n", k); usage(argv[0]); return 2; }
    }
    if (runs < 7) { fprintf(stderr, "--runs must be >= 7\n"); return 2; }
    if (warmup < 1) warmup = 1;
    if (ops == 0) { ops = 200000; ops_set = 0; }

    printf("# allocator-bench workload=%s variant=%s runs=%d warmup=%d "
           "ops=%zu ops_explicit=%d size=%zu seed=%u\n",
           workload, variant, runs, warmup, ops, ops_set, size_hint, seed);
    printf("# columns: workload,variant,runs,ops,min_s,median_s,max_s,p95_s,ns_per_op,Mops_per_s\n");

    for (wi = 0; wi < NWORKLOADS; ++wi) {
        const workload_desc *w = &WORKLOADS[wi];
        size_t wops;
        double *times;
        int selected[NVARIANTS], nsel = 0, vi_check;

        if (strcmp(workload, "all") && strcmp(workload, w->name))
            continue;
        wops = ops_set ? ops : w->def_ops;

        times = (double *)malloc((size_t)runs * NVARIANTS * sizeof(double));
        if (!times) { fprintf(stderr, "stats alloc failed\n"); return 3; }
        for (vi_check = 0; vi_check < NVARIANTS; ++vi_check)
            if (!strcmp(variant, "all") || !strcmp(variant, VARIANTS[vi_check].name))
                selected[nsel++] = vi_check;
        if (nsel == 0) { fprintf(stderr, "no variant matched\n"); return 2; }

        /* Warmup (untimed): full prepare/run/finish cycle for each variant. */
        for (vi_check = 0; vi_check < nsel; ++vi_check) {
            const allocator_api *a = &VARIANTS[selected[vi_check]];
            for (r = 0; r < warmup; ++r) {
                rng_seed(seed);
                if (w->prepare) w->prepare(a, wops, size_hint);
                w->run(a, wops, size_hint);
                if (w->finish) w->finish(a, wops, size_hint);
            }
        }

        /* Interleaved measured rounds: in each round every selected variant
         * runs once, round-robin, with the same fixed seed. */
        for (r = 0; r < runs; ++r) {
            for (vi_check = 0; vi_check < nsel; ++vi_check) {
                const allocator_api *a = &VARIANTS[selected[vi_check]];
                double t0, t1;
                rng_seed(seed);
                if (w->prepare) w->prepare(a, wops, size_hint);
                t0 = now_s();
                w->run(a, wops, size_hint);
                t1 = now_s();
                if (w->finish) w->finish(a, wops, size_hint);
                times[vi_check * runs + r] = t1 - t0;
            }
        }
        for (vi_check = 0; vi_check < nsel; ++vi_check)
            print_stats(w->name, &VARIANTS[selected[vi_check]],
                        times + vi_check * runs, runs, wops);
        free(times);
    }
    if (g_sink == UINT64_MAX || g_query_sink == (size_t)-1)
        fprintf(stderr, "sink==max (unreachable)\n");
    return 0;
}
