/* scoped-memory-style facade proof: one-store scope, dynamic arena, stable refs. */
#include "cplus/cplus.h"
#include "stdlib/scoped_mem.hp"

struct item { int id; int hp; };

int main(void) {
    error e = error();
    stdlib_scoped_stack *ds = stdlib_scoped_stack_current(&e);
    stdlib_dyn_arena arena;
    stdlib_handle_pool pool;
    stdlib_pool_ref ref;
    struct item *it;
    unsigned char *p;
    int fails = 0;
    size_t before, after;

    stdlib_set_error_callback((stdlib_error_callback)0);
    if (ds == (stdlib_scoped_stack *)0) return 1;
    {
        stdlib_scoped_scope frame = stdlib_scoped_scope(ds);
        before = (size_t)(ds->top - ds->base);
        p = (unsigned char *)stdlib_scoped_alloc(ds, 64u, &e);
        if (p == (unsigned char *)0) fails++;
        if ((size_t)(ds->top - ds->base) <= before) fails++;
        frame.drop();
        after = (size_t)(ds->top - ds->base);
        if (after != before) fails++;
    }
    if (stdlib_dyn_arena_open(&arena, 4096u, &e) != 0) fails++;
    p = (unsigned char *)stdlib_dyn_arena_take(&arena, 128u, &e);
    if (p == (unsigned char *)0) fails++;
    stdlib_dyn_arena_rewind(&arena, &e);
    if (stdlib_arena_used(&arena) != 0u) fails++;
    stdlib_arena_destroy(&arena, &e);

    if (stdlib_handle_pool_open(&pool, sizeof(struct item), 4u, &e) != 0) fails++;
    if (stdlib_handle_pool_take(&pool, &ref, &e) != 0) fails++;
    it = (struct item *)stdlib_pool_ref_get(&ref, &e);
    if (it == (struct item *)0) fails++;
    else { it->id = 7; it->hp = 9; }
    stdlib_pool_ref_release(&ref, &e);
    {
        stdlib_slot_pool_scope guard = stdlib_slot_pool_scope(&pool);
        guard.drop();
    }
    print("SCOPED_MEM", fails);
    return fails ? 1 : 0;
}
