/* Positive: C+ code using only data stacks/pools compiles in no-heap mode. */
#include "cplus/cplus.h"
#include "stdlib/scoped_mem.hp"
#include "stdlib/sqldb.hp"
#include "stdlib/gameeng.hp"
#include "stdlib/no_heap.hp"  /* policy macros AFTER all headers */
int main(void) {
    error e = error(); stdlib_scoped_stack *ds = stdlib_scoped_stack_current(&e);
    stdlib_dyn_arena a; stdlib_handle_pool p; stdlib_pool_ref r; sqldb db; game_world w;
    int fails = 0;
    stdlib_set_error_callback((stdlib_error_callback)0);
    { stdlib_scoped_scope s = stdlib_scoped_scope(ds);
      if (stdlib_scoped_alloc(ds, 32u, &e) == (void *)0) fails++;
      s.drop(); }
    if (stdlib_dyn_arena_open(&a, 1024u, &e) != 0) fails++;
    if (stdlib_handle_pool_open(&p, sizeof(int), 4u, &e) != 0) fails++;
    if (stdlib_handle_pool_take(&p, &r, &e) != 0) fails++;
    stdlib_pool_ref_release(&r, &e); stdlib_dyn_arena_rewind(&a, &e);
    sqldb_init(&db); sqldb_insert(&db, 1, 2, "x");
    game_world_init(&w); game_add_entity(&w, 1, 1, 1); game_step(&w, 0.5f);
    print("NO_HEAP_OK", fails); return fails ? 1 : 0;
}
#include "stdlib/no_heap.hp"  /* policy macros must come last */
