/* Demo: modern C+ allocator-aware containers.
 * Shows a request-scoped Arena, a churn-friendly MemoryPool, and a
 * thread-local DataStack scratch scope, each backing string/vector values.
 * Build: ./build/c+ build demos/modern_allocators.cp --default --sanitize=none \
 *          -I. -o <bin> -- build/libc/libcplus_hardened.a
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/mempool.hp"
#include "stdlib/datastack.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

int main(void) {
    error e = error();
    stdlib_set_error_callback((stdlib_error_callback)0);

    /* 1. Arena: one region for the whole request; O(1) reset. */
    stdlib_arena arena;
    stdlib_arena_init(&arena, 2048, &e);
    stdlib_allocator A = stdlib_arena_as_allocator(&arena);
    {
        string name = string_in(&A, "arena-demo");
        vector nums = vector_in(&A);
        int i;
        for (i = 0; i < 10; i++) nums.push_back(i * i);
        print("ARENA", name.c_str(), name.size(), nums.size(), nums.at(9));
        name.drop(); nums.drop();
    }
    stdlib_arena_do_reset(&arena, &e); /* reuse for the next request */
    print("ARENA_RESET", stdlib_arena_used(&arena) == 0);

    /* 2. Pool: uniform slot size with individual frees under churn. */
    stdlib_mem_pool pool;
    stdlib_mem_pool_init(&pool, 128, 8, &e);
    stdlib_allocator P = stdlib_mem_pool_as_allocator(&pool);
    {
        vector v = vector_in(&P);
        int i;
        for (i = 0; i < 20; i++) v.push_back(i);
        print("POOL", v.size(), v.at(19), pool.live, pool.slab_count);
        v.drop();
    }
    stdlib_mem_pool_destroy(&pool, &e);

    /* 3. DataStack: nested scratch scopes with in-place top growth. */
    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    stdlib_scratch_mark outer = stdlib_scratch_enter(ds);
    {
        stdlib_allocator S = stdlib_scratch_allocator();
        string tmp = string_in(&S, "scratch-demo");
        vector vals = vector_in(&S);
        int i;
        for (i = 0; i < 12; i++) vals.push_back(i + 1);
        print("SCRATCH", tmp.c_str(), vals.size(), vals.at(11));
        tmp.drop(); vals.drop();
    }
    stdlib_scratch_leave(ds, outer);
    print("SCRATCH_REWOUND", ds->top == ds->base);
    stdlib_arena_destroy(&arena, &e);
    print("DEMO_OK");
    return 0;
}
