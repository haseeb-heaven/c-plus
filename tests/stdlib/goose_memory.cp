/* Goose-inspired memory model tests: VM-backed bump stacks with on-demand
 * commit, and stable-slot pools with intrusive free lists and generation-based
 * stale refusal.  Architecture inspired by aardappel/goose (Apache-2.0);
 * implementation written from scratch in C+ with RAII/vtable/liveness hooks. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/datastack.hp"
#include "stdlib/mempool.hp"

static int fails = 0;
static void ck(int cond, const char *label) {
    if (!cond) { fails++; print("FAIL", label); }
}

int main(void) {
    error e = error();
    stdlib_set_error_callback((stdlib_error_callback)0);

    /* ---- VM-backed DataStack: reserve, on-demand commit, RAII scope ----- */
    {
        stdlib_datastack ds;
        ck(stdlib_datastack_init_vm(&ds, 32u * 1024u * 1024u, &e) == STDLIB_OK,
           "vm_init");
        ck(ds.backing == 1 && ds.capacity == 32u * 1024u * 1024u &&
           ds.committed == 0 && ds.top == ds.base, "vm_reservation");
        {
            stdlib_scratch_scope scope = stdlib_scratch_scope(&ds);
            unsigned char *a = (unsigned char *)stdlib_datastack_alloc(&ds, 4096, 16, &e);
            ck(a != 0 && (((unsigned long)a) & 15ul) == 0, "vm_align");
            ck(ds.committed >= 4096 && ds.committed <= ds.capacity, "vm_commit_on_demand");
            unsigned char *b = (unsigned char *)stdlib_datastack_alloc(&ds, 2u*1024u*1024u, 8, &e);
            ck(b != 0 && ds.committed >= 2u*1024u*1024u + 4096u, "vm_grow_commit");
            void *grown = stdlib_datastack_realloc(&ds, b, 2u*1024u*1024u,
                                                   2u*1024u*1024u + 64u, 8, &e);
            ck(grown == b, "vm_top_realloc_in_place");
        }
        ck(ds.top == ds.base, "vm_raii_scope_free_one_store");
        stdlib_datastack_destroy(&ds, &e);
        ck(ds.base == 0 && ds.capacity == 0, "vm_destroy");
    }

    /* ---- scoped-memory-style slot_pool: stable slots, free list, stale refusal -- */
    {
        stdlib_slot_pool sp;
        ck(stdlib_slot_pool_init(&sp, 32, 4, &e) == STDLIB_OK, "slot_init");
        {
            stdlib_slot_pool_scope guard = stdlib_slot_pool_scope(&sp);
            stdlib_slot_ref a, b, c;
            ck(stdlib_slot_pool_alloc_ref(&sp, &a, &e) == STDLIB_OK &&
               stdlib_slot_pool_alloc_ref(&sp, &b, &e) == STDLIB_OK, "slot_alloc_ref");
            ck(a.index == 0 && b.index == 1 && a.generation == b.generation,
               "slot_indices");
            unsigned char *pa = (unsigned char *)stdlib_slot_ref_get(&a, &e);
            ck(pa != 0 && pa == sp.items + a.index * sp.slot_size, "slot_get");
            stdlib_slot_ref_free(&a, &e);
            ck(sp.free_count == 1, "slot_intrusive_free_list");
            /* stale handle (generation 1) must refuse, never corrupt */
            {
                stdlib_slot_ref stale;
                stale.pool = &sp; stale.index = 0; stale.generation = 1;
                ck(stdlib_slot_ref_get(&stale, &e) == 0 && e.code() == 103,
                   "slot_stale_refused");
                stdlib_slot_ref_free(&stale, &e);
                ck(e.code() == 104, "slot_double_free_refused");
            }
            ck(stdlib_slot_pool_alloc_ref(&sp, &c, &e) == STDLIB_OK &&
               c.index == 0 && c.generation != 1, "slot_reuse_generation");
            /* grow beyond initial capacity: handles remain stable */
            {
                stdlib_slot_ref many[16];
                int i;
                for (i = 0; i < 16; i++)
                    ck(stdlib_slot_pool_alloc_ref(&sp, &many[i], &e) == STDLIB_OK,
                       "slot_grow_alloc");
                ck(sp.capacity >= 16 && stdlib_slot_ref_get(&c, &e) != 0,
                   "slot_handle_stable_across_growth");
                for (i = 0; i < 16; i++) stdlib_slot_ref_free(&many[i], &e);
            }
            stdlib_slot_ref_free(&c, &e);
            stdlib_slot_ref_free(&b, &e);
        }
        ck(sp.items == 0 && sp.count == 0, "slot_raii_destroy");
    }

    print("SCOPED_MEMORY", fails);
    return fails == 0 ? 0 : 1;
}
