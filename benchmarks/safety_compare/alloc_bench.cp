/* C+ allocation-throughput comparison workload (safety_compare).
 *
 * Subject: the C+ managed allocator reached through the supported stdlib
 * path, `stdlib_alloc` / `stdlib_release` (stdlib/error.hp). Those wrappers
 * call the runtime's `cplus_alloc` / `cplus_release`, so the C+ driver links
 * them automatically -- unlike the hardened-libc `cplus_malloc` family, which
 * needs libcplus_hardened.a and collides with libcplus_runtime.a.
 *
 * Workload (identical operation count in alloc_bench.rs):
 *   N = 200,000 iterations; each iteration performs exactly one allocation
 *   and one free:
 *       sz   = 8 + (i mod 8) * 8        (8, 16, ..., 64 bytes)
 *       byte = (i * 31 + 7) & 0xff
 *       p    = stdlib_alloc(sz)
 *       sink = p                        (volatile; stops alloc/free elision)
 *       checksum = checksum * 33 + sz + byte     (mod 2^64)
 *       stdlib_release(p)
 *
 * The checksum is value-based (no addresses), so it is identical in Rust.
 * `sink` is never read: it only makes the allocation observable to the
 * compiler. The timed region covers the loop only; process startup, clock
 * calls and the final printf are outside it.
 *
 * Build (from the repo root):
 *   PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
 *     build/c+ build --strict --sanitize=none \
 *     benchmarks/safety_compare/alloc_bench.cp \
 *     -o build/bench_safety/cplus_alloc_bench
 */
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"

#define N 200000u

/* Written but never read: exists only to pin each allocation in the IR. */
static volatile void *g_sink;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
}

int main(void) {
    unsigned long long checksum = 0ULL;
    unsigned int i;
    double t0, t1;

    t0 = now_s();
    for (i = 0; i < N; ++i) {
        size_t sz = 8u + (size_t)(i & 7u) * 8u; /* 8,16,...,64 */
        unsigned long long byte =
            ((unsigned long long)i * 31ULL + 7ULL) & 0xffULL;
        void *p = stdlib_alloc(sz);
        if (p == NULL) {
            fprintf(stderr, "stdlib_alloc failed at i=%u\n", i);
            return 2;
        }
        g_sink = p;
        checksum = checksum * 33ULL + (unsigned long long)sz + byte;
        stdlib_release(p);
    }
    t1 = now_s();

    printf("checksum=%llu elapsed_s=%.9f ops=%u\n", checksum, t1 - t0, N);
    return 0;
}
