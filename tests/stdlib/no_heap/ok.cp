/* Positive: C+ code using only data stacks/pools compiles in no-heap mode. */
#include "cplus/cplus.h"
#include "stdlib/goose_mem.hp"
#include "stdlib/sqldb.hp"
#include "stdlib/gameeng.hp"
#include "stdlib/no_heap.hp"  /* policy macros AFTER all headers */
int main(void) {
    error e = error(); goose_stack *ds = goose_stack_current(&e);
    goose_arena a; goose_pool p; goose_ref r; sqldb db; game_world w;
    int fails = 0;
    stdlib_set_error_callback((stdlib_error_callback)0);
    { goose_scope s = goose_scope(ds);
      if (goose_alloc(ds, 32u, &e) == (void *)0) fails++;
      s.drop(); }
    if (goose_arena_open(&a, 1024u, &e) != 0) fails++;
    if (goose_pool_open(&p, sizeof(int), 4u, &e) != 0) fails++;
    if (goose_pool_take(&p, &r, &e) != 0) fails++;
    goose_ref_release(&r, &e); goose_arena_rewind(&a, &e);
    sqldb_init(&db); sqldb_insert(&db, 1, 2, "x");
    game_world_init(&w); game_add_entity(&w, 1, 1, 1); game_step(&w, 0.5f);
    print("NO_HEAP_OK", fails); return fails ? 1 : 0;
}