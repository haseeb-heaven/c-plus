/* PR31 source-level allocator pairing probe: allocations and releases made
 * through the same arena / slot-pool allocator API, with no exported symbols.
 * C+ additive headers only; prints ALLOC_PAIRING <checks> <fail>. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/arena.hp"
#include "stdlib/mempool.hp"

static int checks = 0;
static int failed = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) { failed++; printf("ALLOC_PAIRING FAIL %s\n", tag); }
}

int main(void) {
    error e = error();
    stdlib_arena arena;
    stdlib_slot_pool pool;
    stdlib_slot_ref ref;
    void *p;
    int *slot;

    stdlib_set_error_callback((stdlib_error_callback)0);
    ck(stdlib_arena_init(&arena, 4096u, &e) == 0, "arena_init");
    p = stdlib_arena_alloc(&arena, 64u, 16u, &e);
    ck(p != (void *)0, "arena_alloc");
    stdlib_arena_release(&arena, p, 64u, &e);
    /* Bump arena: release is a no-op; scope reset reclaims the block. */
    ck(stdlib_arena_used(&arena) >= 64u, "arena_release_noop");
    stdlib_arena_do_reset(&arena, &e);
    ck(stdlib_arena_used(&arena) == 0u, "arena_reset");
    stdlib_arena_destroy(&arena, &e);

    ck(stdlib_slot_pool_init(&pool, sizeof(int), 4u, &e) == 0, "pool_init");
    ck(stdlib_slot_pool_alloc_ref(&pool, &ref, &e) == 0, "slot_alloc");
    slot = (int *)stdlib_slot_ref_get(&ref, &e);
    ck(slot != (int *)0, "slot_ptr");
    if (slot != (int *)0)
        *slot = 1234;
    stdlib_slot_ref_free(&ref, &e);
    ck(pool.free_count == 1u, "free_count");
    ck(stdlib_slot_pool_alloc_ref(&pool, &ref, &e) == 0, "slot_reuse");
    slot = (int *)stdlib_slot_ref_get(&ref, &e);
    ck(slot != (int *)0 && *slot == 0, "slot_reuse_value");
    stdlib_slot_ref_free(&ref, &e);
    {
        stdlib_slot_pool_scope guard = stdlib_slot_pool_scope(&pool);
        guard.drop();
    }
    printf("ALLOC_PAIRING %d %d\n", checks, failed);
    return failed ? 1 : 0;
}
