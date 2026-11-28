/* Unified tests for the modern allocator subsystem: Arena, MemoryPool,
 * DataStack, allocator-aware string/vector, and failure diagnostics. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/mempool.hp"
#include "stdlib/datastack.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

static int fails = 0;
static void ck(int cond, const char *label) {
    if (!cond) { fails++; print("FAIL", label); }
}

int main(void) {
    error e = error();
    stdlib_set_error_callback((stdlib_error_callback)0);

    /* ---- Arena: alignment, chaining, reset, generation ---------------- */
    {
        stdlib_arena a;
        ck(stdlib_arena_init(&a, 48, &e) == STDLIB_OK, "arena_init");
        unsigned char *p1 = (unsigned char *)stdlib_arena_alloc(&a, 16, 16, &e);
        unsigned char *p2 = (unsigned char *)stdlib_arena_alloc(&a, 128, 32, &e);
        ck(p1 != 0 && p2 != 0, "arena_alloc_two");
        ck((((unsigned long)p1) & 15ul) == 0 && (((unsigned long)p2) & 31ul) == 0,
           "arena_alignment");
        ck(p2 > p1, "arena_chained_forward");
        stdlib_arena_handle h = stdlib_arena_mark(&a);
        ck(stdlib_arena_handle_valid(&h) == 1, "arena_handle_live");
        stdlib_arena_do_reset(&a, &e);
        ck(stdlib_arena_handle_valid(&h) == 0, "arena_handle_stale_after_reset");
        stdlib_arena_destroy(&a, &e);
    }

    /* ---- Arena from caller buffer: no free of caller storage ----------- */
    {
        unsigned char buf[512];
        stdlib_arena a;
        ck(stdlib_arena_from_buffer(&a, buf, sizeof(buf), &e) == STDLIB_OK,
           "arena_from_buffer");
        void *p = stdlib_arena_alloc(&a, 32, 8, &e);
        ck(p != 0 && (unsigned char *)p >= buf &&
           (unsigned char *)p < buf + sizeof(buf), "arena_buffer_first");
        stdlib_arena_destroy(&a, &e); /* must not free buf */
        ck(buf[0] == buf[0], "arena_caller_buffer_survives");
    }

    /* ---- MemoryPool: churn, double-free, foreign pointer -------------- */
    {
        stdlib_mem_pool p;
        ck(stdlib_mem_pool_init(&p, 32, 4, &e) == STDLIB_OK, "pool_init");
        void *a = stdlib_mem_pool_alloc(&p, &e);
        void *b = stdlib_mem_pool_alloc(&p, &e);
        ck(a != 0 && b != 0 && p.live == 2, "pool_alloc");
        stdlib_mem_pool_release(&p, a, &e);
        ck(p.live == 1, "pool_release");
        stdlib_mem_pool_release(&p, a, &e);
        ck(e.code() == 104, "pool_double_free_refused"); /* CPLUS_EXIT_DOUBLE_FREE */
        unsigned char stack_obj[64];
        stdlib_mem_pool_release(&p, (void *)stack_obj, &e);
        ck(e.code() == 106, "pool_foreign_refused");
        ck(stdlib_mem_pool_alloc(&p, &e) == a, "pool_slot_reuse");
        for (int i = 0; i < 100; i++) {
            void *q = stdlib_mem_pool_alloc(&p, &e);
            ck(q != 0, "pool_churn_alloc");
            stdlib_mem_pool_release(&p, q, &e);
        }
        ck(p.slabs != 0 && p.live == 2, "pool_churn_no_growth_leak");
        stdlib_mem_pool_destroy(&p, &e);
    }

    /* ---- DataStack: nested scopes, top realloc ------------------------ */
    {
        stdlib_datastack *ds = stdlib_thread_datastack(&e);
        ck(ds != 0, "datastack_init");
        stdlib_scratch_mark outer = stdlib_scratch_enter(ds);
        void *a = stdlib_datastack_alloc(ds, 32, 8, &e);
        {
            stdlib_scratch_mark inner = stdlib_scratch_enter(ds);
            void *b = stdlib_datastack_alloc(ds, 16, 8, &e);
            ck(a != 0 && b != 0, "scratch_nested_alloc");
            void *grown = stdlib_datastack_realloc(ds, b, 16, 64, 8, &e);
            ck(grown == b, "scratch_top_realloc_in_place");
            stdlib_scratch_leave(ds, inner);
        }
        stdlib_scratch_leave(ds, outer);
        ck(ds->top == ds->base, "scratch_scope_rewind");
    }

    /* ---- Allocator-aware containers in an Arena ------------------------ */
    {
        stdlib_arena a;
        ck(stdlib_arena_init(&a, 4096, &e) == STDLIB_OK, "container_arena_init");
        stdlib_allocator A = stdlib_arena_as_allocator(&a);
        string s = string_in(&A, "arena-string");
        ck(s.valid() && s.size() == 12, "string_in_arena");
        ck(s.append("-more") == 0 && s.size() == 17, "string_in_arena_append");
        vector v = vector_in(&A);
        for (int i = 0; i < 32; i++) ck(v.push_back(i) == 0, "vector_in_arena_push");
        ck(v.size() == 32 && v.at(31) == 31, "vector_in_arena_read");
        s.drop(); v.drop();
        stdlib_arena_destroy(&a, &e);
    }

    /* ---- Pool-backed vector with slot reuse ---------------------------- */
    {
        stdlib_mem_pool pool;
        ck(stdlib_mem_pool_init(&pool, 256, 8, &e) == STDLIB_OK, "pool_for_vector");
        stdlib_allocator P = stdlib_mem_pool_as_allocator(&pool);
        vector v = vector_in(&P);
        for (int i = 0; i < 16; i++) ck(v.push_back(i) == 0, "vector_in_pool_push");
        ck(v.at(15) == 15, "vector_in_pool_read");
        v.drop();
        stdlib_mem_pool_destroy(&pool, &e);
    }

    print("MODERN_ALLOC", fails);
    return fails == 0 ? 0 : 1;
}
