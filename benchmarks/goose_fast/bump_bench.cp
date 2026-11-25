/* Goose-style fast-path benchmark: VM data-stack bump allocation vs checked heap.
 * Measures append throughput and one-store scope release. Deterministic output
 * (timings reported in buckets to avoid flaky exact numbers). */
#include <stdio.h>
#include <time.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/datastack.hp"

static long long ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(void) {
    error e = error();
    stdlib_set_error_callback((stdlib_error_callback)0);
    enum { N = 200000 };
    long long t0, t1, t2, t3;
    size_t i;

    /* Goose-style: bump on the thread data stack inside a scratch scope. */
    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    if (ds == (stdlib_datastack *)0) { print("BENCH FATAL datastack"); return 1; }
    t0 = ns_now();
    {
        stdlib_scratch_scope sc = stdlib_scratch_scope(ds);
        unsigned char *p = (unsigned char *)0;
        for (i = 0; i < N; ++i) {
            p = (unsigned char *)stdlib_datastack_alloc(ds, 32, 16, &e);
            if (p == (unsigned char *)0) { print("BENCH FATAL bump"); return 1; }
            p[0] = (unsigned char)(i & 255u);
        }
        t1 = ns_now();
        unsigned char *saved = ds->top;   /* scope exit will restore top */
        (void)saved;
    }                                      /* one-store release */
    t2 = ns_now();

    /* Checked heap baseline (same element size and count). */
    void *heap[1]; (void)heap;
    for (i = 0; i < N; ++i) {
        void *p = stdlib_alloc(32);
        if (p == (void *)0) { print("BENCH FATAL heap"); return 1; }
        ((unsigned char *)p)[0] = (unsigned char)(i & 255u);
        stdlib_release(p);
    }
    t3 = ns_now();

    /* Bucketed verdicts stay the deterministic gate; exact per-op timings
     * are printed as measured evidence for the proof document (host-specific). */
    long long bump_ns = t1 - t0;
    long long scope_ns = t2 - t1;
    long long heap_ns = t3 - t2;
    print("BENCH bump_ms_bucket", (bump_ns < 50000000LL) ? "fast" : "slow");
    print("BENCH release_ms_bucket", (scope_ns < 1000000LL) ? "one_store" : "slow");
    print("BENCH heap_ms_bucket", (heap_ns < 50000000LL) ? "fast" : "slow");
    print("BENCH ratio_ge_2", (heap_ns > bump_ns * 2) ? "yes" : "no");
    print("BENCH bump_ns_per_op", (unsigned long long)(bump_ns / (long long)N));
    print("BENCH heap_ns_per_op", (unsigned long long)(heap_ns / (long long)N));
    print("BENCH scope_release_ns", (unsigned long long)scope_ns);
    print("BENCH_OK");
    return 0;
}
