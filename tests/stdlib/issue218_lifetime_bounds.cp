/* Issue #218 regression: views into arena bytes must be invalidated by
 * destroy/reset, and realloc/release must reject forged sizes before doing
 * pointer subtraction or memcpy.
 *
 * Prints "LIFETIMEBOUNDS <checks> <failures>"; exits non-zero on failure. */
#include <stddef.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/arena.hp"
#include "stdlib/datastack.hp"

static int checks = 0;
static int failures = 0;

static void check(int cond) {
    checks++;
    if (!cond) failures++;
}

int main(void) {
    error e = error();
    stdlib_arena a;
    stdlib_datastack ds;
    unsigned long long t1 = 0u;
    unsigned long long t2 = 0u;
    void *p1;
    void *p2;
    void *q;
    unsigned char *top;

    stdlib_set_error_callback((stdlib_error_callback)0);

    /* 1. destroy retires every checked token before freeing the block. */
    check(stdlib_arena_init(&a, 128u, &e) == STDLIB_OK);
    p1 = stdlib_arena_take_checked(&a, 32u, 16u, &t1, &e);
    check(p1 != (void *)0 && t1 != 0u);
    check(stdlib_view_validate(p1, t1) == 1);
    stdlib_arena_destroy(&a, &e);
    check(stdlib_view_validate(p1, t1) == 0);
    check(stdlib_view_is_live(p1) == 0);
    check(a.checked_outstanding == 0u);

    /* 2. reset retires tokens in both the first block and a spilled block. */
    check(stdlib_arena_init(&a, 64u, &e) == STDLIB_OK);
    p1 = stdlib_arena_take_checked(&a, 32u, 16u, &t1, &e);
    p2 = stdlib_arena_take_checked(&a, 64u, 16u, &t2, &e);
    check(p1 != (void *)0 && p2 != (void *)0 && p2 != p1);
    check(t1 != 0u && t2 != 0u && t1 != t2);
    check(stdlib_view_validate(p1, t1) == 1);
    check(stdlib_view_validate(p2, t2) == 1);
    stdlib_arena_do_reset(&a, &e);
    check(stdlib_view_validate(p1, t1) == 0);
    check(stdlib_view_validate(p2, t2) == 0);
    check(stdlib_view_is_live(p1) == 0);
    check(stdlib_view_is_live(p2) == 0);
    check(a.checked_outstanding == 0u);
    stdlib_arena_destroy(&a, &e);

    /* 3. arena realloc rejects old_size larger than cursor - ptr. */
    check(stdlib_arena_init(&a, 256u, &e) == STDLIB_OK);
    p1 = stdlib_arena_alloc(&a, 32u, 16u, &e);
    check(p1 != (void *)0);
    q = stdlib_arena_realloc(&a, p1, 128u, 48u, 16u, &e);
    check(q == (void *)0);
    check(e.category() == STDLIB_CATEGORY_INVALID);
    check(a.cursor == (unsigned char *)p1 + 32u);
    q = stdlib_arena_realloc(&a, p1, 32u, 48u, 16u, &e);
    check(q == p1);
    check(a.cursor == (unsigned char *)p1 + 48u);
    stdlib_arena_destroy(&a, &e);

    /* 4. datastack realloc rejects old_size larger than top - ptr. */
    check(stdlib_datastack_init(&ds, 1024u, &e) == STDLIB_OK);
    p1 = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
    check(p1 != (void *)0);
    top = ds.top;
    q = stdlib_datastack_realloc(&ds, p1, 512u, 48u, 16u, &e);
    check(q == (void *)0);
    check(ds.top == top);
    q = stdlib_datastack_realloc(&ds, p1, 32u, 48u, 16u, &e);
    check(q == p1);
    check(ds.top == top + 16u);
    p2 = stdlib_datastack_alloc(&ds, 32u, 16u, &e);
    check(p2 != (void *)0);

    /* 5. datastack release refuses a forged size instead of underflowing. */
    top = ds.top;
    stdlib_datastack_release(&ds, p2, 1024u, &e);
    check(ds.top == top);
    stdlib_datastack_release(&ds, p2, 32u, &e);
    check(ds.top == (unsigned char *)p2);
    stdlib_datastack_destroy(&ds, &e);

    print("LIFETIMEBOUNDS", checks, failures);
    return failures == 0 ? 0 : 1;
}
