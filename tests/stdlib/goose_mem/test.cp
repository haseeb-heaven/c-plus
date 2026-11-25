/* Goose-style facade proof: one-store scope, dynamic arena, stable refs. */
#include "cplus/cplus.h"
#include "stdlib/goose_mem.hp"

struct item { int id; int hp; };

int main(void) {
    error e = error();
    goose_stack *ds = goose_stack_current(&e);
    goose_arena arena;
    goose_pool pool;
    goose_ref ref;
    struct item *it;
    unsigned char *p;
    int fails = 0;
    size_t before, after;

    stdlib_set_error_callback((stdlib_error_callback)0);
    if (ds == (goose_stack *)0) return 1;
    {
        goose_scope frame = goose_scope(ds);
        before = (size_t)(ds->top - ds->base);
        p = (unsigned char *)goose_alloc(ds, 64u, &e);
        if (p == (unsigned char *)0) fails++;
        if ((size_t)(ds->top - ds->base) <= before) fails++;
        frame.drop();
        after = (size_t)(ds->top - ds->base);
        if (after != before) fails++;
    }
    if (goose_arena_open(&arena, 4096u, &e) != 0) fails++;
    p = (unsigned char *)goose_arena_take(&arena, 128u, &e);
    if (p == (unsigned char *)0) fails++;
    goose_arena_rewind(&arena, &e);
    if (stdlib_arena_used(&arena) != 0u) fails++;
    stdlib_arena_destroy(&arena, &e);

    if (goose_pool_open(&pool, sizeof(struct item), 4u, &e) != 0) fails++;
    if (goose_pool_take(&pool, &ref, &e) != 0) fails++;
    it = (struct item *)goose_ref_get(&ref, &e);
    if (it == (struct item *)0) fails++;
    else { it->id = 7; it->hp = 9; }
    goose_ref_release(&ref, &e);
    {
        stdlib_slot_pool_scope guard = stdlib_slot_pool_scope(&pool);
        guard.drop();
    }
    print("GOOSE_MEM", fails);
    return fails ? 1 : 0;
}
