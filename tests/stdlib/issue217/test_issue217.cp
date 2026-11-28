/* Issue #217 regression: the fixed 4096-slot lifetime registry must not be
 * permanently exhausted once every slot has held a distinct, now-retired
 * address.
 *
 * After 4096 short-lived views are registered and retired, no slot has a NULL
 * key left to reuse.  stdlib_view_register() must reclaim the first retired
 * tombstone (while still probing the whole chain so duplicate keys are updated
 * in place) instead of failing closed forever.  This drives the exact
 * precondition, then checks fresh registrations plus the end-to-end impact on
 * string_view, slice and arena::take_checked called out in the issue.
 *
 * Headless and deterministic: every check prints only on failure and the last
 * line is "ISSUE217 <checks> <failures>". */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/view_lifetime.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
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

#define CHURN ((size_t)5000) /* > STDLIB_VIEW_TABLE_SIZE (4096) */
#define FRESH ((size_t)64)

static char backing[CHURN][16];
static unsigned long long tokens[CHURN];
static char fresh_buf[FRESH][16];
static unsigned long long fresh_tokens[FRESH];

int main(void) {
    size_t i;
    size_t tombstones = 0;
    unsigned long long t;
    unsigned long long old_token;
    error e = error();
    stdlib_arena a;
    void *p;
    unsigned long long arena_token = 0;
    string s;
    string_view sv;
    vector v;
    slice sl;

    stdlib_set_error_callback((stdlib_error_callback)0);

    /* Fill the registry with more distinct addresses than it has slots. */
    for (i = 0; i < CHURN; ++i)
        tokens[i] = stdlib_view_register(&backing[i][0], 16u);
    for (i = 0; i < STDLIB_VIEW_TABLE_SIZE; ++i)
        ck(tokens[i] != 0 &&
               stdlib_view_validate(&backing[i][0], tokens[i]) == 1,
           "early_registration_live");

    /* Retire everything: every slot now holds a non-NULL tombstone key and no
     * never-used slot remains in the table (the issue #217 precondition). */
    for (i = 0; i < CHURN; ++i)
        stdlib_view_retire(&backing[i][0]);
    for (i = 0; i < STDLIB_VIEW_TABLE_SIZE; ++i) {
        if (stdlib_view_registry_state.slots[i].ptr != (const void *)0 &&
            stdlib_view_registry_state.slots[i].live == 0)
            ++tombstones;
    }
    ck(tombstones == STDLIB_VIEW_TABLE_SIZE, "registry_fully_tombstoned");
    ck(stdlib_view_validate(&backing[0][0], tokens[0]) == 0,
       "retired_token_invalid");

    /* Core regression: fresh addresses must still register after exhaustion. */
    for (i = 0; i < FRESH; ++i) {
        fresh_tokens[i] = stdlib_view_register(&fresh_buf[i][0], 16u);
        ck(fresh_tokens[i] != 0, "fresh_registration_after_churn");
        ck(stdlib_view_validate(&fresh_buf[i][0], fresh_tokens[i]) == 1,
           "fresh_token_valid");
        ck(stdlib_view_token_of(&fresh_buf[i][0]) == fresh_tokens[i],
           "fresh_token_of");
        ck(stdlib_view_size(&fresh_buf[i][0]) == 16u, "fresh_size");
    }
    ck(stdlib_view_validate(&backing[0][0], tokens[0]) == 0,
       "old_token_not_revived");

    /* Re-registering a retired address yields a new token, never the old one. */
    old_token = tokens[0];
    t = stdlib_view_register(&backing[0][0], 16u);
    ck(t != 0 && t != old_token, "reregister_fresh_token");
    ck(stdlib_view_validate(&backing[0][0], t) == 1 &&
           stdlib_view_validate(&backing[0][0], old_token) == 0,
       "reregister_supersedes_old");

    /* End-to-end impact called out in issue #217. */
    s = string("after_churn");
    sv = s.to_view();
    ck(sv.is_live() == 1 && sv.data() == s.c_str() &&
           sv.equals("after_churn") == 1,
       "string_view_after_churn");

    v = vector();
    ck(v.push_back(7) == 0, "vector_push");
    sl = v.view();
    ck(sl.stale() == 0 && sl.valid() == 1 && sl.data() == v.items,
       "slice_after_churn");

    ck(stdlib_arena_init(&a, 256u, &e) == 0, "arena_init");
    p = stdlib_arena_take_checked(&a, 16u, 8u, &arena_token, &e);
    ck(p != (void *)0 && arena_token != 0, "arena_take_after_churn");
    ck(p != (void *)0 && stdlib_view_validate(p, arena_token) == 1,
       "arena_token_valid");
    ck(p != (void *)0 &&
           stdlib_arena_release_checked(&a, p, arena_token, &e) == 0,
       "arena_release_after_churn");
    stdlib_arena_destroy(&a, &e);

    printf("ISSUE217 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
