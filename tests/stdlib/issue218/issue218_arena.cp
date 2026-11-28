/* Issue #218 regression (arena half).
 *
 * 1. stdlib_arena_destroy() / stdlib_arena_do_reset() must retire every view
 *    token registered by stdlib_arena_take_checked() before the backing blocks
 *    are released or rewound.  A surviving token is a use-after-free bypass:
 *    ctl_span / stdlib_view_validate() would keep reporting freed storage live.
 * 2. stdlib_arena_realloc() must reject an old_size larger than the bytes that
 *    actually back `ptr` BEFORE subtracting it from `cursor` or memcpy-ing it.
 *    A forged size otherwise underflows `cursor - old_size` and reads past the
 *    owning block.
 *
 * Every check is deterministic: no timing, allocation-address equality, or
 * uninitialised reads are asserted.  Printed output is fixed; see `expected`. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/arena.hp"

static int checks = 0;
static int failed = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) {
        failed++;
        printf("FAIL %s\n", tag);
    }
}

#define CAP ((size_t)64)

/* A token registered outside the arena must survive arena teardown. */
static unsigned char outside[16];

static void test_destroy_retires(void) {
    error e = error();
    stdlib_arena a;
    unsigned long long t = 0;
    unsigned long long t2 = 0;
    unsigned long long ot;
    unsigned char *p;
    unsigned char *q;

    ot = stdlib_view_register(&outside[0], sizeof(outside));
    ck(ot != 0, "outside_register");
    ck(stdlib_arena_init(&a, CAP, &e) == 0, "destroy_init");
    p = (unsigned char *)stdlib_arena_take_checked(&a, 8u, 4u, &t, &e);
    ck(p != (unsigned char *)0 && t != 0 && stdlib_view_validate(p, t) == 1,
       "destroy_live_before");
    ck(a.checked_outstanding == 1u, "destroy_count_before");

    stdlib_arena_destroy(&a, &e);
    /* Registry lookups never dereference p, so using the stale value is safe. */
    ck(stdlib_view_validate(p, t) == 0, "destroy_token_retired");
    ck(stdlib_view_is_live(p) == 0, "destroy_not_live");
    ck(a.checked_outstanding == 0u, "destroy_count_cleared");
    ck(stdlib_view_validate(&outside[0], ot) == 1, "outside_survives_destroy");

    ck(stdlib_arena_init(&a, CAP, &e) == 0, "reinit_after_destroy");
    q = (unsigned char *)stdlib_arena_take_checked(&a, 8u, 4u, &t2, &e);
    ck(q != (unsigned char *)0 && t2 != 0 && t2 != t, "reinit_fresh_token");
    ck(stdlib_view_validate(p, t) == 0 && stdlib_view_validate(q, t2) == 1,
       "reinit_stale_not_revived");
    stdlib_view_retire(&outside[0]);
    stdlib_arena_destroy(&a, &e);
}

static void test_reset_retires(void) {
    error e = error();
    stdlib_arena a;
    unsigned long long t = 0;
    unsigned long long ot;
    unsigned char *p;
    void *r;

    ot = stdlib_view_register(&outside[0], sizeof(outside));
    ck(ot != 0, "outside_register_reset");
    ck(stdlib_arena_init(&a, CAP, &e) == 0, "reset_init");
    p = (unsigned char *)stdlib_arena_take_checked(&a, 8u, 4u, &t, &e);
    ck(p != (unsigned char *)0 && stdlib_view_validate(p, t) == 1,
       "reset_live_before");

    stdlib_arena_do_reset(&a, &e);
    ck(stdlib_view_validate(p, t) == 0, "reset_token_retired");
    ck(stdlib_view_is_live(p) == 0, "reset_not_live");
    ck(a.checked_outstanding == 0u, "reset_count_cleared");
    ck(stdlib_arena_used(&a) == 0u, "reset_cursor");
    ck(stdlib_view_validate(&outside[0], ot) == 1, "outside_survives_reset");

    r = stdlib_arena_take_checked(&a, 8u, 4u, &t, &e);
    ck(r != (void *)0 && stdlib_view_validate(r, t) == 1, "reset_take_again");
    ck(stdlib_arena_rewind_checked(&a, &e) != 0, "reset_rewind_refuses_live");
    ck(stdlib_arena_release_checked(&a, r, t, &e) == 0, "reset_release_checked");
    ck(stdlib_arena_rewind_checked(&a, &e) == 0, "reset_rewind_after_release");
    stdlib_view_retire(&outside[0]);
    stdlib_arena_destroy(&a, &e);
}

static void test_realloc_forged(void) {
    error e = error();
    stdlib_arena a;
    unsigned char *p;
    unsigned char *q;
    void *r;
    size_t i;

    ck(stdlib_arena_init(&a, CAP, &e) == 0, "realloc_init");

    /* old_size (40) <= block capacity (64) but > the 8 bytes from p to the
     * cursor.  Pre-fix: top = cursor - 40 underflows below base and memcpy
     * copies 40 bytes over a fresh allocation overlapping p. */
    p = (unsigned char *)stdlib_arena_alloc(&a, 8u, 4u, &e);
    ck(p != (unsigned char *)0, "forged_top_alloc");
    for (i = 0; i < 8u; ++i) p[i] = (unsigned char)(i + 1u);
    r = stdlib_arena_realloc(&a, p, 40u, 16u, 4u, &e);
    ck(r == (void *)0, "forged_top_refused");
    ck(e.category() == STDLIB_CATEGORY_INVALID, "forged_top_category");
    ck(stdlib_arena_used(&a) == 8u, "forged_top_cursor_intact");
    ck(p[0] == 1 && p[7] == 8, "forged_top_data_intact");

    /* q sits 4 bytes before the end of the block.  old_size = capacity passes
     * the old guard but reads 60 bytes past the block's payload. */
    q = (unsigned char *)stdlib_arena_alloc(&a, 52u, 4u, &e);
    ck(q != (unsigned char *)0 && stdlib_arena_used(&a) == 60u,
       "forged_tail_alloc");
    for (i = 0; i < 16u; ++i) q[i] = (unsigned char)(0xA0u + i);
    r = stdlib_arena_realloc(&a, q, CAP, 8u, 4u, &e);
    ck(r == (void *)0, "forged_tail_refused");
    ck(e.category() == STDLIB_CATEGORY_INVALID, "forged_tail_category");
    ck(stdlib_arena_used(&a) == 60u, "forged_tail_cursor_intact");
    ck(q[0] == 0xA0 && q[15] == 0xAF, "forged_tail_data_intact");

    r = stdlib_arena_realloc(&a, p, (size_t)1 << 50, 16u, 4u, &e);
    ck(r == (void *)0, "forged_huge_refused");
    stdlib_arena_destroy(&a, &e);
}

static void test_realloc_valid(void) {
    error e = error();
    stdlib_arena a;
    unsigned char *p;
    unsigned char *q;
    unsigned char *r;
    size_t i;
    int ok;

    ck(stdlib_arena_init(&a, CAP, &e) == 0, "valid_init");
    p = (unsigned char *)stdlib_arena_alloc(&a, 16u, 4u, &e);
    ck(p != (unsigned char *)0, "valid_alloc");
    for (i = 0; i < 16u; ++i) p[i] = (unsigned char)(i + 1u);

    r = (unsigned char *)stdlib_arena_realloc(&a, p, 16u, 32u, 4u, &e);
    ck(r == p && stdlib_arena_used(&a) == 32u, "valid_grow_in_place");
    ok = 1;
    for (i = 0; i < 16u; ++i)
        if (r[i] != (unsigned char)(i + 1u)) ok = 0;
    ck(ok == 1, "valid_grow_data");

    r = (unsigned char *)stdlib_arena_realloc(&a, p, 32u, 8u, 4u, &e);
    ck(r == p && stdlib_arena_used(&a) == 8u, "valid_shrink_in_place");
    ok = 1;
    for (i = 0; i < 8u; ++i)
        if (r[i] != (unsigned char)(i + 1u)) ok = 0;
    ck(ok == 1, "valid_shrink_data");

    /* A non-top allocation is copied; the newer neighbour must be untouched. */
    q = (unsigned char *)stdlib_arena_alloc(&a, 16u, 4u, &e);
    ck(q != (unsigned char *)0 && q != p, "valid_neighbor_alloc");
    for (i = 0; i < 16u; ++i) q[i] = (unsigned char)(0x40u + i);
    r = (unsigned char *)stdlib_arena_realloc(&a, p, 8u, 24u, 4u, &e);
    ck(r != (unsigned char *)0 && r != p, "valid_copy_realloc");
    ok = 1;
    for (i = 0; i < 8u; ++i)
        if (r[i] != (unsigned char)(i + 1u)) ok = 0;
    ck(ok == 1, "valid_copy_data");
    ok = 1;
    for (i = 0; i < 16u; ++i)
        if (q[i] != (unsigned char)(0x40u + i)) ok = 0;
    ck(ok == 1, "valid_neighbor_intact");
    stdlib_arena_destroy(&a, &e);
}

static void test_realloc_spilled(void) {
    error e = error();
    stdlib_arena a;
    unsigned char *p;
    unsigned char *q;
    unsigned char *s;
    void *r;

    ck(stdlib_arena_init(&a, CAP, &e) == 0, "spill_init");
    p = (unsigned char *)stdlib_arena_alloc(&a, CAP, 4u, &e);
    ck(p != (unsigned char *)0 && a.block != (stdlib_arena_block *)0 &&
       a.block->base == p, "spill_first_full");
    q = (unsigned char *)stdlib_arena_alloc(&a, 16u, 4u, &e);
    s = (unsigned char *)stdlib_arena_alloc(&a, 48u, 4u, &e);
    ck(q != (unsigned char *)0 && s != (unsigned char *)0 &&
       s == q + 16u, "spill_second_block");
    /* s has only 16 bytes to the end of its block; old_size = capacity would
     * make the old code memcpy 64 bytes from s (48 bytes over-read). */
    r = stdlib_arena_realloc(&a, s, CAP, 8u, 4u, &e);
    ck(r == (void *)0, "spill_forged_refused");
    ck(e.category() == STDLIB_CATEGORY_INVALID, "spill_forged_category");
    /* A legitimate realloc of a pointer in the first block still copies. */
    r = stdlib_arena_realloc(&a, p, CAP, 8u, 4u, &e);
    ck(r != (void *)0 && r != p, "spill_first_copy");
    stdlib_arena_destroy(&a, &e);
}

int main(void) {
    stdlib_set_error_callback((stdlib_error_callback)0);
    test_destroy_retires();
    test_reset_retires();
    test_realloc_forged();
    test_realloc_valid();
    test_realloc_spilled();
    printf("ISSUE218 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
