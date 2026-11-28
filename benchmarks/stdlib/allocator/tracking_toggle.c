/* tracking_toggle.c - isolate C+ metadata tracking ON vs OFF from ONE source.
 *
 * The only difference between the two binaries is the compile-time macro
 * CPLUS_MALLOC_TRACKING.  cplus_libc.h redirects malloc/free to the tracked
 * C+ hardened allocator (cplus_malloc/cplus_free, with the metadata side
 * table) when the macro is defined, and leaves the host allocator untouched
 * when it is not.  No source changes, no queries: the delta is the cost of
 * enabling tracking itself.
 *
 * Identical fixed seed, warmup and interleaved-style repeated rounds in both
 * builds.  Timing/stats computed here (no Python).
 *
 * Build/run commands are in NOTES.md.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "cplus_libc.h"

static volatile uint64_t g_sink;
static uint32_t g_rng;

static uint32_t rng_next(void) {
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
}
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    size_t ops = (argc > 1) ? (size_t)strtoul(argv[1], NULL, 10) : 200000u;
    const int runs = 11, warmup = 2;
    double t[11];
    int r;

    for (r = 0; r < warmup; ++r) {
        size_t i;
        g_rng = 314159265u;
        for (i = 0; i < ops; ++i) {
            void *p = malloc(32u); /* redirected when tracking is ON */
            if (!p) return 3;
            g_sink ^= (uint64_t)(uintptr_t)p ^ rng_next();
            free(p);
        }
    }
    for (r = 0; r < runs; ++r) {
        size_t i;
        double t0, t1;
        g_rng = 314159265u;
        t0 = now_s();
        for (i = 0; i < ops; ++i) {
            void *p = malloc(32u);
            if (!p) return 3;
            g_sink ^= (uint64_t)(uintptr_t)p ^ rng_next();
            free(p);
        }
        t1 = now_s();
        t[r] = t1 - t0;
    }
    qsort(t, (size_t)runs, sizeof(double), cmp_double);
    printf("tracking=%s ops=%zu runs=%d min=%.9f median=%.9f max=%.9f ns_per_op=%.2f\n",
#ifdef CPLUS_MALLOC_TRACKING
           "ON",
#else
           "OFF",
#endif
           ops, runs, t[0], t[runs / 2], t[runs - 1],
           t[runs / 2] * 1.0e9 / (double)ops);
    return 0;
}
