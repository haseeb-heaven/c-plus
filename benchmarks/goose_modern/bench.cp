/* Modern libs + Goose-style scratch arena: measured no-heap hot path.
 * stdlib/sqldb.hp and stdlib/gameeng.hp use fixed slots (no per-object heap);
 * transient buffers come from the VM data stack.  The heap baseline mallocs
 * and frees the same 32-byte record for every operation. */
#include <stdio.h>
#include <time.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/datastack.hp"
#include "stdlib/sqldb.hp"
#include "stdlib/gameeng.hp"

static long long ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(void) {
    error e = error();
    const int N = 20000;
    long long t0, t1, t2, t3, t4;
    int i, ops = 0;
    stdlib_datastack *ds;
    sqldb db;
    game_world w;

    stdlib_set_error_callback((stdlib_error_callback)0);
    ds = stdlib_thread_datastack(&e);
    if (ds == (stdlib_datastack *)0) { print("GOOSE_MODERN FATAL datastack"); return 1; }
    sqldb_init(&db);
    game_world_init(&w);

    /* Warm up the VM data-stack pages (first-touch commit) before timing. */
    {
        stdlib_scratch_scope warm = stdlib_scratch_scope(ds);
        for (i = 0; i < 2000; ++i) {
            unsigned char *buf = (unsigned char *)stdlib_datastack_alloc(ds, 32, 16, &e);
            if (buf == (unsigned char *)0) { print("GOOSE_MODERN FATAL warm"); return 1; }
            buf[0] = 1;
        }
        warm.drop();
    }

    /* Phase 1: allocation-only, identical 32-byte record in both arms. */    t0 = ns_now();
    {
        stdlib_scratch_scope frame = stdlib_scratch_scope(ds);
        for (i = 0; i < N; ++i) {
            unsigned char *buf = (unsigned char *)stdlib_datastack_alloc(ds, 32, 16, &e);
            if (buf == (unsigned char *)0) { print("GOOSE_MODERN FATAL bump"); return 1; }
            buf[0] = (unsigned char)(i & 255);
        }
        frame.drop();
    }
    t1 = ns_now();
    for (i = 0; i < N; ++i) {
        void *p = stdlib_alloc(32);
        if (p == (void *)0) { print("GOOSE_MODERN FATAL heap"); return 1; }
        ((unsigned char *)p)[0] = (unsigned char)(i & 255);
        stdlib_release(p);
    }
    t2 = ns_now();

    /* Phase 2: modern-lib workload inside one scratch scope. sqldb/gameeng
     * use fixed slots (no per-object heap); payloads are bumped. */
    t3 = ns_now();
    {
        stdlib_scratch_scope frame = stdlib_scratch_scope(ds);
        for (i = 0; i < N; ++i) {
            unsigned char *buf = (unsigned char *)stdlib_datastack_alloc(ds, 32, 16, &e);
            if (buf == (unsigned char *)0) { print("GOOSE_MODERN FATAL bump2"); return 1; }
            buf[0] = (unsigned char)(i & 255);
            if (sqldb_insert(&db, i % 64, i % 100, "x") == 0) ops++;
            if (i % 1000 == 0) game_step(&w, 0.25f);
        }
        frame.drop();
    }
    t4 = ns_now();

    print("GOOSE_MODERN ops", ops);
    print("GOOSE_MODERN bump_ns_per_op", (unsigned long long)((t1 - t0) / (long long)N));
    print("GOOSE_MODERN heap_ns_per_op", (unsigned long long)((t2 - t1) / (long long)N));
    print("GOOSE_MODERN modern_ns_per_op", (unsigned long long)((t4 - t3) / (long long)N));
    print("GOOSE_MODERN ratio_ge_2", (t2 - t1) > (t1 - t0) * 2 ? "yes" : "no");
    print("GOOSE_MODERN_OK");
    return 0;
}
