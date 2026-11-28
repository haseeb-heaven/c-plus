/* Core-memory audit C2 regression: scratch-backed owners and their views.
 *
 * A scratch scope rewind reuses the exact same addresses.  The stale owner's
 * view must die at scope exit, otherwise a later allocation at that address
 * merely replaces its token and the old owner's retire-by-address can erase
 * the fresh owner's token.  Two independent string owners share one address
 * across two scopes; every observed value is deterministic.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/datastack.hp"
#include "stdlib/string.hp"

static int checks = 0;
static int failures = 0;

static void ck(int ok, const char *tag) {
    ++checks;
    if (!ok) { ++failures; printf("FAIL %s\n", tag); }
}

int main(void) {
    error e = error();
    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    stdlib_allocator scratch = stdlib_scratch_allocator();
    string old_owner;
    string fresh_owner;
    string_view stale;
    string_view fresh;
    const char *old_addr;
    unsigned long long old_token;
    unsigned long long fresh_token;

    stdlib_set_error_callback((stdlib_error_callback)0);
    if (ds == (stdlib_datastack *)0) { printf("SCRATCH_VIEW 1\n"); return 1; }

    /* Owner A: the view is live inside the scope. */
    {
        stdlib_scratch_scope scope = stdlib_scratch_scope(ds);
        old_owner = string_in(&scratch, "OLD-VALUE");
        ck(old_owner.valid() == 1, "owner_a_live");
        stale = old_owner.to_view();
        old_addr = stale.raw_data();
        old_token = stdlib_view_token_of(old_addr);
        ck(old_token != 0, "token_a_published");
        ck(stale.is_live() == 1, "view_a_live_in_scope");
        scope.drop();
    }

    /* Scope exit rewound A's bytes: its token must be retired right there,
     * not left live until some later allocation happens to reuse the address. */
    ck(stale.is_live() == 0, "view_a_dead_after_scope");
    ck(stale.data() == (const char *)0, "view_a_data_null");
    ck(stale.equals("OLD-VALUE") == 0, "view_a_read_refused");
    ck(stdlib_view_validate(old_addr, old_token) == 0, "token_a_retired");
    old_owner.drop(); /* dead owner retires an address with no new owner yet */
    ck(stdlib_view_token_of(old_addr) == 0, "address_unpublished");

    /* Owner B: same reclaimed address, different owner and different token.
     * A's stale view stays dead and cannot invalidate B's live token. */
    {
        stdlib_scratch_scope scope = stdlib_scratch_scope(ds);
        fresh_owner = string_in(&scratch, "NEW-VALUE");
        fresh = fresh_owner.to_view();
        ck(fresh.raw_data() == old_addr, "address_reused");
        fresh_token = stdlib_view_token_of(old_addr);
        ck(fresh_token != 0 && fresh_token != old_token, "token_b_fresh");
        ck(fresh.is_live() == 1, "view_b_live");
        ck(fresh.equals("NEW-VALUE") == 1, "view_b_content");
        ck(stale.is_live() == 0, "view_a_stays_dead_after_reuse");
        ck(stdlib_view_validate(old_addr, old_token) == 0, "token_a_not_revived");
        ck(stdlib_view_validate(old_addr, fresh_token) == 1, "token_b_survives");
        scope.drop();
        ck(fresh.is_live() == 0, "view_b_dead_after_scope");
    }

    /* The vtable reset path rewinds the same way. */
    {
        stdlib_scratch_scope scope = stdlib_scratch_scope(ds);
        string reset_owner = string_in(&scratch, "RESET-VALUE");
        string_view reset_view = reset_owner.to_view();
        ck(reset_view.is_live() == 1, "view_c_live");
        scratch.reset(scratch.ctx);
        ck(reset_view.is_live() == 0, "view_c_dead_after_reset");
        reset_owner.drop();
        scope.drop(); /* already rewound: no-op */
    }

    printf("SCRATCH_VIEW %d\n", failures);
    return failures == 0 ? 0 : 1;
}
