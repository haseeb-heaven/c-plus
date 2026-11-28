/* Issue #44 regression: an unscoped arena rewind must not silently alias a
 * tracked live pointer.  Rewind is refused until every tracked pointer is
 * released; after a successful rewind old tokens are dead even if the new
 * allocation reuses the same address. */
#include <stdio.h>
#include "cplus/cplus.h"
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

int main(void) {
    error e = error();
    stdlib_arena a;
    unsigned long long t1 = 0;
    unsigned long long t2 = 0;
    int *p1;
    int *p2;
    void *reused;
    unsigned long long t3 = 0;

    stdlib_set_error_callback((stdlib_error_callback)0);
    ck(stdlib_arena_init(&a, 256u, &e) == 0, "arena_init");

    p1 = (int *)stdlib_arena_take_checked(&a, sizeof(int), 4u, &t1, &e);
    ck(p1 != (int *)0 && t1 != 0 && *p1 == 0, "tracked_take");
    *p1 = 1234;

    ck(a.checked_outstanding == 1, "outstanding_count");
    ck(stdlib_arena_rewind_checked(&a, &e) != 0, "rewind_refused");
    ck(e.category() == STDLIB_CATEGORY_LIFECYCLE, "refusal_category");
    ck(stdlib_view_validate(p1, t1) == 1 && *p1 == 1234, "old_pointer_intact");

    p2 = (int *)stdlib_arena_take_checked(&a, sizeof(int), 4u, &t2, &e);
    ck(p2 != (int *)0 && p2 != p1, "second_take_no_alias");
    *p2 = 9999;
    ck(*p1 == 1234, "old_value_not_overwritten");

    ck(stdlib_arena_release_checked(&a, p1, t1, &e) == 0, "release_1");
    ck(stdlib_arena_release_checked(&a, p1, t1, &e) != 0, "double_release_refused");
    ck(stdlib_arena_release_checked(&a, p2, t2, &e) == 0, "release_2");
    ck(stdlib_view_validate(p1, t1) == 0, "old_token_retired");
    ck(stdlib_arena_rewind_checked(&a, &e) == 0, "rewind_after_release");
    ck(stdlib_arena_used(&a) == 0u, "rewind_reset_cursor");

    reused = stdlib_arena_take_checked(&a, sizeof(int), 4u, &t3, &e);
    ck(reused == (void *)p1, "address_reused");
    ck(t3 != t1 && stdlib_view_validate(reused, t1) == 0 &&
       stdlib_view_validate(reused, t3) == 1, "recycled_address_new_token");
    stdlib_arena_release_checked(&a, reused, t3, &e);

    /* Scoped allocations: drop() retires tokens and rewinds in one step. */
    {
        stdlib_arena_scope scope = stdlib_arena_scope(&a);
        int *scoped = (int *)scope.take(sizeof(int), 4u, &e);
        unsigned long long scoped_token = stdlib_view_token_of(scoped);
        ck(scoped != (int *)0 && scoped_token != 0 &&
           stdlib_view_validate(scoped, scoped_token) == 1, "scope_take");
        scope.drop();
        ck(stdlib_view_validate(scoped, scoped_token) == 0, "scope_stale");
        ck(stdlib_arena_used(&a) == 0u && a.checked_outstanding == 0,
           "scope_drop_rewound");
    }

    stdlib_arena_destroy(&a, &e);
    printf("ISSUE44 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
