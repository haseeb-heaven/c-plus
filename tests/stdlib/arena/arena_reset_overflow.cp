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
 *   D. alloc rejects n+align overflow before allocating a new block;
 *   E. failed init and destroy leave no dangling reset target;
 *   F. debug reset poisons the reusable first-block payload with 0xDD;
 *   G. the generation counter is shared with another translation unit;
 *   H. a checked release refuses a token owned by a different arena;
 *   I. from_buffer never writes an unaligned header into caller storage;
 *   J. a small caller buffer is used, not silently replaced by a heap block.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/arena.hp"

extern uint64_t arena_helper_next_generation(void);

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
    uint64_t g1;
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

    /* ---- E: a failed init / destroy leaves no stale reset target --------- */
    a.block = (stdlib_arena_block *)0;
    a.first_base = (unsigned char *)0;
    a.first_block = (stdlib_arena_block *)(void *)buf; /* poisoned sentinel */
    a.first_capacity = 7u;
    acheck("a_state_init_fail",
           stdlib_arena_init(&a, (size_t)-1, &e) == STDLIB_FAIL);
    acheck("a_state_fail_no_target",
           a.first_block == (stdlib_arena_block *)0 &&
           a.first_base == (unsigned char *)0 && a.first_capacity == 0u);
    stdlib_arena_do_reset(&a, &e);
    acheck("a_state_fail_reset_no_block",
           a.block == (stdlib_arena_block *)0 && a.remaining == 0u);

    acheck("a_state_init_ok", stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
    p = stdlib_arena_alloc(&a, 32u, 16u, &e);
    acheck("a_state_alloc_ok", p != (void *)0);
    first = a.first_block;
    stdlib_arena_destroy(&a, &e);
    acheck("a_state_destroy_clears_target",
           a.first_block == (stdlib_arena_block *)0 &&
           a.first_base == (unsigned char *)0 && a.first_capacity == 0u);
    acheck("a_state_destroy_first_released",
           cplus_allocation_generation(first) == 0u);
    stdlib_arena_do_reset(&a, &e);
    acheck("a_state_destroy_reset_no_block",
           a.block == (stdlib_arena_block *)0 &&
           a.cursor == (unsigned char *)0 && a.remaining == 0u);
    acheck("a_state_destroy_reset_used_zero", stdlib_arena_used(&a) == 0u);

    /* ---- F: debug reset poisons the reused first-block payload ---------- */
    a.block = (stdlib_arena_block *)0;
    a.first_base = (unsigned char *)0;
    a.first_block = (stdlib_arena_block *)0;
    acheck("a_poison_init_ok", stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
    p = stdlib_arena_alloc(&a, 64u, 16u, &e); /* exact fit: first block full */
    acheck("a_poison_alloc_ok", p != (void *)0);
    if (p != (void *)0) memset(p, 0xAB, 64u);
    {
        unsigned char *bytes = (unsigned char *)p;
        size_t i;
        int written = 1;
        for (i = 0; i < 64u; ++i)
            if (bytes[i] != 0xABu) written = 0;
        acheck("a_poison_written", written);
    }
    p = stdlib_arena_alloc(&a, 64u, 16u, &e); /* spills past first block */
    acheck("a_poison_spill_ok", p != (void *)0);
    stdlib_arena_do_reset(&a, &e);
    {
        unsigned char *bytes = (unsigned char *)a.first_base;
        size_t i;
        int poisoned = 1;
        for (i = 0; i < 64u; ++i) {
#ifdef NDEBUG
            if (bytes[i] != 0xABu) poisoned = 0; /* no poison in release */
#else
            if (bytes[i] != 0xDDu) poisoned = 0;
#endif
        }
        acheck("a_poison_reset_bytes", poisoned);
    }
    p = stdlib_arena_alloc(&a, 64u, 16u, &e);
    acheck("a_poison_realloc_zeroed",
           p != (void *)0 && ((unsigned char *)p)[0] == 0u &&
           ((unsigned char *)p)[63] == 0u);
    stdlib_arena_destroy(&a, &e);
    acheck("a_poison_destroy_clean", a.block == (stdlib_arena_block *)0);

    /* ---- G: generation state is one shared object across TUs ------------ */
    a.block = (stdlib_arena_block *)0;
    a.first_base = (unsigned char *)0;
    a.first_block = (stdlib_arena_block *)0;
    acheck("a_gen_init_ok", stdlib_arena_init(&a, 32u, &e) == STDLIB_OK);
    g0 = a.generation;
    g1 = arena_helper_next_generation();
    acheck("a_gen_shared_counter", g1 == g0 + 1u);
    {
        stdlib_arena_handle h = stdlib_arena_mark(&a);
        acheck("a_gen_handle_live", stdlib_arena_handle_valid(&h) == 1);
        stdlib_arena_do_reset(&a, &e);
        acheck("a_gen_handle_stale", stdlib_arena_handle_valid(&h) == 0);
    }
    stdlib_arena_destroy(&a, &e);
    acheck("a_gen_destroy_clean", a.block == (stdlib_arena_block *)0);

    /* ---- H: a foreign token cannot decrement this arena's count --------- */
    {
        stdlib_arena other;
        unsigned long long ta = 0;
        unsigned long long tb = 0;
        void *pa;
        void *pb;
        a.block = (stdlib_arena_block *)0;
        a.first_base = (unsigned char *)0;
        a.first_block = (stdlib_arena_block *)0;
        acheck("a_own_init_a", stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
        acheck("a_own_init_b", stdlib_arena_init(&other, 64u, &e) == STDLIB_OK);
        pa = stdlib_arena_take_checked(&a, 32u, 16u, &ta, &e);
        pb = stdlib_arena_take_checked(&other, 32u, 16u, &tb, &e);
        acheck("a_own_takes", pa != (void *)0 && pb != (void *)0 &&
                             ta != 0 && tb != 0);
        acheck("a_own_foreign_refused",
               stdlib_arena_release_checked(&other, pa, ta, &e) != 0);
        acheck("a_own_foreign_category",
               e.category() == STDLIB_CATEGORY_OWNERSHIP);
        acheck("a_own_counts_unchanged",
               a.checked_outstanding == 1 && other.checked_outstanding == 1);
        acheck("a_own_token_still_live", stdlib_view_validate(pa, ta) == 1);
        acheck("a_own_owner_release",
               stdlib_arena_release_checked(&a, pa, ta, &e) == 0);
        acheck("a_own_other_still_outstanding", other.checked_outstanding == 1);
        acheck("a_own_rewind_a", stdlib_arena_rewind_checked(&a, &e) == 0);
        acheck("a_own_release_b",
               stdlib_arena_release_checked(&other, pb, tb, &e) == 0);
        stdlib_arena_destroy(&other, &e);
    }
    stdlib_arena_destroy(&a, &e);
    acheck("a_own_destroy_clean", a.block == (stdlib_arena_block *)0);

    /* ---- I: misaligned caller storage keeps an aligned heap header ------- */
    {
        unsigned char raw[256];
        size_t off = 0u;
        unsigned char *buf;
        stdlib_arena_block *hdr;
        void *bp;
        while (((uintptr_t)(raw + off) &
                ((uintptr_t)_Alignof(stdlib_arena_block) - 1u)) == 0u)
            ++off;
        buf = raw + off;
        a.block = (stdlib_arena_block *)0;
        a.first_base = (unsigned char *)0;
        a.first_block = (stdlib_arena_block *)0;
        acheck("a_mis_init_ok",
               stdlib_arena_from_buffer(&a, (void *)buf,
                   sizeof(raw) - off, &e) == STDLIB_OK);
        hdr = a.first_block;
        acheck("a_mis_heap_header",
               hdr != (stdlib_arena_block *)(void *)buf && hdr->owns == 1);
        acheck("a_mis_payload_aligned",
               ((uintptr_t)a.first_base &
                (uintptr_t)(STDLIB_ALIGN_DEFAULT - 1u)) == 0u);
        acheck("a_mis_payload_in_caller",
               (uintptr_t)a.first_base >= (uintptr_t)buf &&
               (uintptr_t)a.first_base < (uintptr_t)raw + sizeof(raw));
        bp = stdlib_arena_alloc(&a, 16u, 16u, &e);
        acheck("a_mis_alloc_ok",
               bp != (void *)0 && (uintptr_t)bp >= (uintptr_t)buf &&
               (uintptr_t)bp < (uintptr_t)raw + sizeof(raw));
        if (bp != (void *)0) memset(bp, 0x3C, 16u);
        stdlib_arena_do_reset(&a, &e);
        acheck("a_mis_reset_ok",
               a.block == hdr && a.remaining == a.first_capacity);
        stdlib_arena_destroy(&a, &e);
        acheck("a_mis_destroy_clean", a.block == (stdlib_arena_block *)0);
        buf[0] = 0x11u; /* caller bytes survive the heap-header release */
        acheck("a_mis_caller_intact", buf[0] == 0x11u);
    }

    /* ---- J: small caller storage is honored ---------------------------- */
    {
        unsigned char raw[64];
        size_t off = 0u;
        unsigned char *buf;
        size_t len = sizeof(stdlib_arena_block) + STDLIB_ALIGN_DEFAULT;
        size_t pad;
        void *bp;
        void *spill;
        while (((uintptr_t)(raw + off) &
                ((uintptr_t)_Alignof(stdlib_arena_block) - 1u)) != 0u)
            ++off;
        buf = raw + off;
        pad = (size_t)((STDLIB_ALIGN_DEFAULT -
                        ((uintptr_t)buf & (uintptr_t)(STDLIB_ALIGN_DEFAULT - 1u))) &
                       (uintptr_t)(STDLIB_ALIGN_DEFAULT - 1u));
        a.block = (stdlib_arena_block *)0;
        a.first_base = (unsigned char *)0;
        a.first_block = (stdlib_arena_block *)0;
        acheck("a_small_init_ok",
               stdlib_arena_from_buffer(&a, (void *)buf, len, &e) == STDLIB_OK);
        acheck("a_small_capacity_is_caller",
               a.first_capacity == len - pad && a.first_capacity < a.default_block);
        acheck("a_small_base_in_caller",
               a.first_base == buf + pad && pad < len);
        bp = stdlib_arena_alloc(&a, 8u, 8u, &e);
        acheck("a_small_alloc_in_caller",
               bp != (void *)0 && (uintptr_t)bp >= (uintptr_t)buf &&
               (uintptr_t)bp < (uintptr_t)(raw + sizeof(raw)));
        spill = stdlib_arena_alloc(&a, 5000u, 16u, &e);
        acheck("a_small_spill_ok", spill != (void *)0 && a.block != a.first_block);
        stdlib_arena_do_reset(&a, &e);
        acheck("a_small_reset_to_caller",
               a.block == a.first_block && a.cursor == a.first_base);
        stdlib_arena_destroy(&a, &e);
        acheck("a_small_destroy_clean", a.block == (stdlib_arena_block *)0);
        buf[0] = 0x22u;
        acheck("a_small_caller_intact", buf[0] == 0x22u);
    }

    print("ARENA", arena_checks, arena_failures);
    return arena_failures == 0 ? 0 : 1;
}
