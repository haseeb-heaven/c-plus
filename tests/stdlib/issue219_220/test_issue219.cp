/* Issue #219 regression: pool slot alignment and grown slot-pool generations.
 *
 * Part A -- stdlib_mem_pool_add_slab() must align every slot header and user
 * pointer to STDLIB_ALIGN_DEFAULT even when slot_size is not a multiple of 16.
 * Part B -- stdlib_slot_pool_grow() must zero the newly grown generations[]
 * tail so the next alloc_ref() reads initialized memory.
 *
 * Headless and deterministic: only FAIL lines are printed and the last line is
 * "ISSUE219 <checks> <failures>". */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/mempool.hp"

static int checks = 0;
static int failed = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) {
        failed++;
        printf("FAIL %s\n", tag);
    }
}

/* ---- Part B: slot_pool grown generations ------------------------------- */

static void test_slot_pool_generations(void) {
    error e = error();
    stdlib_slot_pool sp;
    stdlib_slot_ref a;
    stdlib_slot_ref b;
    stdlib_slot_ref c;
    stdlib_slot_ref d;
    stdlib_slot_ref stale;
    size_t i;

    ck(stdlib_slot_pool_init(&sp, 8, 2, &e) == STDLIB_OK, "sp_init");
    ck(sp.capacity == 2 && sp.count == 0, "sp_init_state");
    for (i = 0; i < sp.capacity; i++)
        ck(sp.generations[i] == 0, "sp_init_generations_zero");

    ck(stdlib_slot_pool_alloc_ref(&sp, &a, &e) == STDLIB_OK, "sp_alloc_a");
    ck(a.index == 0 && a.generation == 1, "sp_gen_a");
    ck(stdlib_slot_pool_alloc_ref(&sp, &b, &e) == STDLIB_OK, "sp_alloc_b");
    ck(b.index == 1 && b.generation == 1, "sp_gen_b");

    /* Third allocation grows capacity 2 -> 4. */
    ck(stdlib_slot_pool_alloc_ref(&sp, &c, &e) == STDLIB_OK, "sp_alloc_grow");
    ck(sp.capacity == 4 && sp.count == 3, "sp_grow_capacity");
    ck(c.index == 2 && c.generation == 1, "sp_fresh_generation_is_one");

    /* Invariant: slots never handed out must still carry generation 0.  The
     * pre-fix code left gen[3] uninitialized and incrementing it read UB. */
    for (i = 0; i < sp.capacity; i++)
        ck(sp.generations[i] == (i < 3 ? 1u : 0u), "sp_grown_tail_zeroed");

    /* The fourth ref takes the grown slot and gets a deterministic tag. */
    ck(stdlib_slot_pool_alloc_ref(&sp, &d, &e) == STDLIB_OK, "sp_alloc_d");
    ck(d.index == 3 && d.generation == 1, "sp_grown_slot_generation");

    /* Generation tags keep stale handles refused after reuse. */
    stale = d;
    stdlib_slot_ref_free(&d, &e);
    ck(d.pool == (stdlib_slot_pool *)0, "sp_free_clears_handle");
    ck(stdlib_slot_ref_get(&stale, &e) == (void *)0 && e.code() == 103,
       "sp_stale_get_refused");
    stdlib_slot_ref_free(&stale, &e);
    ck(e.code() == 104, "sp_stale_free_refused");
    ck(stdlib_slot_pool_alloc_ref(&sp, &d, &e) == STDLIB_OK, "sp_reuse");
    ck(d.index == 3 && d.generation == 3, "sp_reuse_generation");

    stdlib_slot_ref_free(&a, &e);
    stdlib_slot_ref_free(&b, &e);
    stdlib_slot_ref_free(&c, &e);
    stdlib_slot_ref_free(&d, &e);
    stdlib_slot_pool_destroy(&sp, &e);
    ck(sp.capacity == 0 && sp.generations == (uint64_t *)0, "sp_destroy");
}

/* ---- Part A: mem_pool slot/header alignment ---------------------------- */

static void test_pool_alignment(size_t slot_size) {
    error e = error();
    stdlib_mem_pool p;
    void *slots[8];
    unsigned char *bytes[8];
    size_t i;
    size_t j;

    ck(stdlib_mem_pool_init(&p, slot_size, 8, &e) == STDLIB_OK, "mp_init");

    for (i = 0; i < 8; i++) {
        slots[i] = stdlib_mem_pool_alloc(&p, &e);
        ck(slots[i] != (void *)0, "mp_alloc");
        if (slots[i] == (void *)0)
            break;
        ck((((uintptr_t)slots[i]) & (STDLIB_ALIGN_DEFAULT - 1u)) == 0,
           "mp_user_slot_aligned");
        ck((((uintptr_t)((unsigned char *)slots[i] -
                         STDLIB_POOL_HEADER_SIZE)) &
             (STDLIB_ALIGN_DEFAULT - 1u)) == 0,
           "mp_slot_header_aligned");
        bytes[i] = (unsigned char *)slots[i];
        for (j = 0; j < slot_size; j++)
            bytes[i][j] = (unsigned char)(i + 1u);
    }

    /* Distinct slots must never overlap each other's storage. */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < slot_size; j++)
            ck(bytes[i][j] == (unsigned char)(i + 1u), "mp_no_slot_overlap");
    }

    for (i = 0; i < 8; i++)
        stdlib_mem_pool_release(&p, slots[i], &e);
    ck(p.live == 0, "mp_live_zero");

    /* Released slot is zeroed and a second release is refused. */
    ck(slots[0] != (void *)0, "mp_release_target");
    if (slots[0] != (void *)0) {
        ck(((unsigned char *)slots[0])[0] == 0, "mp_release_zeroes_slot");
        stdlib_mem_pool_release(&p, slots[0], &e);
        ck(e.failed_now(), "mp_double_free_refused");
    }

    stdlib_mem_pool_destroy(&p, &e);
    ck(p.slab_count == 0 && p.slabs == (stdlib_pool_slab *)0, "mp_destroy");
}

int main(void) {
    test_slot_pool_generations();
    test_pool_alignment(1u);
    test_pool_alignment(5u);
    test_pool_alignment(12u);
    test_pool_alignment(13u);
    test_pool_alignment(16u);
    printf("ISSUE219 %d %d\n", checks, failed);
    return failed != 0;
}
