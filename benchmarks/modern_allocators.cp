/* Benchmark: HeapAllocator vs Arena vs DataStack for high-frequency
 * string/vector construction and teardown.  Reports wall-clock medians
 * (call run_bench.sh for 7 runs and medians). */
#include <stdio.h>
#include <time.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/datastack.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

#ifndef ROUNDS
#define ROUNDS 2000
#endif

static unsigned long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

static unsigned long long run_case(const stdlib_allocator *a, int reset_each) {
    unsigned long long t0 = now_ns();
    int r;
    for (r = 0; r < ROUNDS; r++) {
        string s = string_in(a, "benchmark-text-value");
        vector v = vector_in(a);
        int i;
        for (i = 0; i < 8; i++) v.push_back(i + r);
        if (v.at(7) != 7 + r || s.size() != 20) return 0;
        s.drop(); v.drop();
        if (reset_each) stdlib_allocator_reset(a);
    }
    return now_ns() - t0;
}

int main(void) {
    error e = error();
    unsigned long long heap_ns, arena_ns, scratch_ns;
    stdlib_allocator H = stdlib_heap_allocator();
    stdlib_arena ar;
    stdlib_datastack *ds;
    stdlib_allocator A, S;

    stdlib_set_error_callback((stdlib_error_callback)0);
    (void)e;
    stdlib_arena_init(&ar, 1u << 16u, &e);
    A = stdlib_arena_as_allocator(&ar);
    ds = stdlib_thread_datastack(&e);
    S = stdlib_scratch_allocator();
    if (ds == (stdlib_datastack *)0) return 2;

    heap_ns = run_case(&H, 0);
    arena_ns = run_case(&A, 1);
    {
        stdlib_scratch_mark m = stdlib_scratch_enter(ds);
        scratch_ns = run_case(&S, 0);
        stdlib_scratch_leave(ds, m);
    }
    print("BENCH", ROUNDS, heap_ns, arena_ns, scratch_ns);
    stdlib_arena_destroy(&ar, &e);
    return 0;
}
