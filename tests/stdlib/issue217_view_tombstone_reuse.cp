/* Issue #217 regression: the fixed lifetime registry must reuse retired
 * slots instead of permanently exhausting the 4096-slot table.
 *
 * Self-checking and deterministic.  Prints "VIEWREUSE <checks> <failures>";
 * exits non-zero iff failures != 0. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/view_lifetime.hp"

static int checks = 0;
static int failures = 0;

static void check(int cond) {
    checks++;
    if (!cond) failures++;
}

int main(void) {
    static unsigned char storage[STDLIB_VIEW_TABLE_SIZE + 16u];
    unsigned long long first_token;
    unsigned long long fresh_token;
    unsigned long long last_token;
    unsigned long long stale_token;
    const void *first = (const void *)&storage[0];
    const void *second = (const void *)&storage[1];
    size_t i;

    stdlib_set_error_callback((stdlib_error_callback)0);
    memset(&stdlib_view_registry_state, 0, sizeof(stdlib_view_registry_state));

    first_token = stdlib_view_register(first, 8u);
    check(first_token != 0u);
    check(stdlib_view_validate(first, first_token) == 1);
    stdlib_view_retire(first);
    check(stdlib_view_validate(first, first_token) == 0);
    check(stdlib_view_is_live(first) == 0);

    /* A different pointer reuses the tombstone, and the old token stays dead. */
    fresh_token = stdlib_view_register(second, 16u);
    check(fresh_token != 0u);
    check(fresh_token != first_token);
    check(stdlib_view_validate(second, fresh_token) == 1);
    check(stdlib_view_validate(first, first_token) == 0);
    check(stdlib_view_size(second) == 16u);

    /* Re-registering the same address also cannot revive an old token. */
    stale_token = fresh_token;
    fresh_token = stdlib_view_register(second, 24u);
    check(fresh_token != 0u);
    check(fresh_token != stale_token);
    check(stdlib_view_validate(second, stale_token) == 0);
    check(stdlib_view_validate(second, fresh_token) == 1);
    check(stdlib_view_size(second) == 24u);

    /* Churn more distinct addresses than the table has slots.  Before the
     * fix every registration after slot 4096 failed forever. */
    last_token = 0u;
    for (i = 0; i < (size_t)STDLIB_VIEW_TABLE_SIZE + 64u; ++i) {
        const void *p = (const void *)&storage[i % (sizeof(storage) - 8u)];
        unsigned long long t = stdlib_view_register(p, 4u);
        if (t == 0u || stdlib_view_validate(p, t) != 1)
            break;
        last_token = t;
        stdlib_view_retire(p);
    }
    check(i == (size_t)STDLIB_VIEW_TABLE_SIZE + 64u);
    check(last_token != 0u);

    /* One more live registration must still succeed after the churn. */
    last_token = stdlib_view_register(&storage[sizeof(storage) - 4u], 32u);
    check(last_token != 0u);
    check(stdlib_view_validate(&storage[sizeof(storage) - 4u], last_token) == 1);
    stdlib_view_retire(&storage[sizeof(storage) - 4u]);

    /* Token exhaustion fails closed: no wrap back to a stale token. */
    stdlib_view_registry_state.next_token = 0xFFFFFFFFFFFFFFFFULL;
    check(stdlib_view_register(&storage[2], 8u) == 0u);
    check(stdlib_view_registry_state.next_token == 0xFFFFFFFFFFFFFFFFULL);

    print("VIEWREUSE", checks, failures);
    return failures == 0 ? 0 : 1;
}
