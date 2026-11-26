/* Issue #55 regression suite for stdlib/arena.hp.
 *
 * Self-checking and deterministic: fixed sizes, fixed order, no randomness.
 * Each check prints "<tag> <0|1>"; the final line is
 * "ARENA <checks> <failures>".  The process exits non-zero iff failures != 0.
 *
 * Coverage:
 *   A. reset releases every spilled heap block (one spill);
 *   B. reset releases a chain of two spilled blocks;
 *   C. from_buffer(buf, 0) initialises first_block (no wild reset target);
 *   D. alloc rejects n+align overflow before allocating a new block.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/arena.hp"

static int arena_checks = 0;
static int arena_failures = 0;

static void acheck(const char *tag, int cond) {
    arena_checks++;
    if (!cond) arena_failures++;
    print(tag, cond ? 1 : 0);
}

int main(void) {
    error e = error();
    unsigned char buf[8];
    stdlib_arena a;
    stdlib_arena_block *first;
    stdlib_arena_block *spill1;
    stdlib_arena_block *spill2;
    uint64_t g0;
    void *p;

    stdlib_set_error_callback((stdlib_error_callback)0);

    /* ---- A: one spilled block is released by reset --------------------- */
    a.block = (stdlib_arena_block *)0;
    a.first_base = (unsigned char *)0;
    a.first_block = (stdlib_arena_block *)0;
    acheck("a_reset_init_ok", stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
    first = a.block;
    g0 = a.generation;
    p = stdlib_arena_alloc(&a, 48u, 16u, &e);
    acheck("a_reset_first_alloc_ok", p != (void *)0);
    p = stdlib_arena_alloc(&a, 64u, 16u, &e); /* 16 left -> spill */
    acheck("a_reset_spill_alloc_ok", p != (void *)0);
    spill1 = a.block;
    acheck("a_reset_spill_is_new", spill1 != first);
    acheck("a_reset_spill_live_before", cplus_allocation_generation(spill1) != 0u);
    stdlib_arena_do_reset(&a, &e);
    acheck("a_reset_block_is_first", a.block == first);
    acheck("a_reset_remaining_restored", a.remaining == a.first_capacity);
    acheck("a_reset_used_zero", stdlib_arena_used(&a) == 0u);
    acheck("a_reset_generation_bumped", a.generation != g0);
    acheck("a_reset_spill_released", cplus_allocation_generation(spill1) == 0u);
    acheck("a_reset_first_still_live", cplus_allocation_generation(first) != 0u);
    p = stdlib_arena_alloc(&a, 32u, 16u, &e);
    acheck("a_reset_reuse_alloc_ok", p != (void *)0);
    stdlib_arena_destroy(&a, &e);
    acheck("a_reset_destroy_clean", a.block == (stdlib_arena_block *)0);
    acheck("a_reset_first_released", cplus_allocation_generation(first) == 0u);

    /* ---- B: a chain of two spilled blocks is fully released ------------ */
    a.block = (stdlib_arena_block *)0;
    a.first_base = (unsigned char *)0;
    a.first_block = (stdlib_arena_block *)0;
    acheck("a_chain_init_ok", stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
    first = a.block;
    p = stdlib_arena_alloc(&a, 64u, 16u, &e); /* exact fit in 64-byte block */
    acheck("a_chain_first_alloc_ok", p != (void *)0);
    p = stdlib_arena_alloc(&a, 64u, 16u, &e);
    acheck("a_chain_spill1_ok", p != (void *)0);
    spill1 = a.block;
    p = stdlib_arena_alloc(&a, 64u, 16u, &e);
    acheck("a_chain_spill2_ok", p != (void *)0);
    spill2 = a.block;
    acheck("a_chain_distinct_blocks",
           spill2 != spill1 && spill1 != first && spill2 != first);
    acheck("a_chain_both_live_before",
           cplus_allocation_generation(spill1) != 0u &&
           cplus_allocation_generation(spill2) != 0u);
    stdlib_arena_do_reset(&a, &e);
    acheck("a_chain_spill1_released", cplus_allocation_generation(spill1) == 0u);
    acheck("a_chain_spill2_released", cplus_allocation_generation(spill2) == 0u);
    acheck("a_chain_first_still_live", cplus_allocation_generation(first) != 0u);
    acheck("a_chain_reset_at_first", a.block == first);
    stdlib_arena_destroy(&a, &e);
    acheck("a_chain_destroy_clean", a.block == (stdlib_arena_block *)0);

    /* ---- C: from_buffer(len=0) sets a valid first_block ---------------- */
    a.block = (stdlib_arena_block *)0;
    a.first_base = (unsigned char *)0;
    /* Poison the field: the old code left it untouched when len == 0. */
    a.first_block = (stdlib_arena_block *)(void *)buf;
    acheck("a_fb0_init_ok",
           stdlib_arena_from_buffer(&a, (void *)buf, 0u, &e) == STDLIB_OK);
    acheck("a_fb0_first_block_set",
           a.first_block == a.block &&
           a.first_block != (stdlib_arena_block *)(void *)buf);
    acheck("a_fb0_first_live",
           cplus_allocation_generation(a.first_block) != 0u);
    first = a.first_block;
    p = stdlib_arena_alloc(&a, 5000u, 16u, &e); /* spills past 4096 */
    acheck("a_fb0_spill_ok", p != (void *)0);
    spill1 = a.block;
    acheck("a_fb0_spill_distinct", spill1 != first);
    stdlib_arena_do_reset(&a, &e);
    acheck("a_fb0_reset_at_first", a.block == first);
    acheck("a_fb0_reset_used_zero", stdlib_arena_used(&a) == 0u);
    acheck("a_fb0_spill_released", cplus_allocation_generation(spill1) == 0u);
    acheck("a_fb0_first_still_live", cplus_allocation_generation(first) != 0u);
    p = stdlib_arena_alloc(&a, 32u, 16u, &e);
    acheck("a_fb0_reuse_alloc_ok", p != (void *)0);
    stdlib_arena_destroy(&a, &e);
    acheck("a_fb0_destroy_clean", a.block == (stdlib_arena_block *)0);

    /* ---- D: n+align overflow is refused without allocating a block ----- */
    a.block = (stdlib_arena_block *)0;
    a.first_base = (unsigned char *)0;
    a.first_block = (stdlib_arena_block *)0;
    acheck("a_ov_init_ok", stdlib_arena_init(&a, 128u, &e) == STDLIB_OK);
    first = a.block;
    g0 = a.generation;
    p = stdlib_arena_alloc(&a, (size_t)-1, 16u, &e);
    acheck("a_ov_max_null", p == (void *)0);
    acheck("a_ov_max_range", e.category() == STDLIB_CATEGORY_RANGE);
    acheck("a_ov_max_no_new_block", a.block == first);
    acheck("a_ov_max_gen_unchanged", a.generation == g0);
    p = stdlib_arena_alloc(&a, (size_t)-1 - 8u, 16u, &e);
    acheck("a_ov_near_max_null", p == (void *)0);
    acheck("a_ov_near_max_range", e.category() == STDLIB_CATEGORY_RANGE);
    acheck("a_ov_near_max_no_new_block", a.block == first);
    p = stdlib_arena_alloc(&a, 32u, 16u, &e);
    acheck("a_ov_normal_alloc_ok", p != (void *)0);
    acheck("a_ov_normal_block_unchanged", a.block == first);
    stdlib_arena_destroy(&a, &e);
    acheck("a_ov_destroy_clean", a.block == (stdlib_arena_block *)0);

    print("ARENA", arena_checks, arena_failures);
    return arena_failures == 0 ? 0 : 1;
}
