/* Regression for issue #78: a failed metadata-table calloc() must not leave
 * meta_lock held or spin forever in the linear-probe insertion loop.  A
 * deterministic hook replaces calloc() with a function that returns NULL; the
 * test also checks that bounded probing still records into a tombstone and
 * that normal growth works once the hook is removed. */
#include <stdio.h>
#include <stdlib.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int g_hook_calls;

static void *oom_calloc(size_t count, size_t size) {
    (void)count;
    (void)size;
    ++g_hook_calls;
    return (void *)0;
}

int main(void) {
    stdlib_meta_slot *saved_tab;
    size_t saved_cap, saved_used, saved_live;
    stdlib_telemetry_stats before;
    stdlib_telemetry_stats after;
    stdlib_meta_slot *full;
    int keys[8];
    int failures = 0;
    int i;
    int found;
    unsigned long long fail_before;

    saved_tab = stdlib_state.meta_tab;
    saved_cap = stdlib_state.meta_cap;
    saved_used = stdlib_state.meta_used;
    saved_live = stdlib_state.meta_live;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_state.meta_tab = (stdlib_meta_slot *)0;
    stdlib_state.meta_cap = 0u;
    stdlib_state.meta_used = 0u;
    stdlib_state.meta_live = 0u;

    /* 1. First-ever growth fails: no table, no lock held, clean refusal. */
    stdlib_meta_set_calloc_hook(oom_calloc);
    g_hook_calls = 0;
    stdlib_telemetry_snapshot(&before);
    stdlib_meta_record((const void *)&keys[0], 16u, 1u);
    stdlib_telemetry_snapshot(&after);
    if (g_hook_calls != 1 || stdlib_state.meta_cap != 0u ||
        stdlib_state.meta_lock != 0 ||
        after.api_failures - before.api_failures == 0ULL)
        ++failures;
    /* The lock must be acquirable immediately after the OOM path. */
    stdlib_meta_acquire();
    stdlib_meta_release_lock();

    /* 2. Completely full table and a failing calloc: the old unbounded probe
     * spun here while holding meta_lock. */
    full = (stdlib_meta_slot *)calloc(4u, sizeof(stdlib_meta_slot));
    if (full == (stdlib_meta_slot *)0) {
        ++failures;
    } else {
        stdlib_state.meta_tab = full;
        stdlib_state.meta_cap = 4u;
        stdlib_state.meta_used = 4u;
        stdlib_state.meta_live = 4u;
        for (i = 0; i < 4; ++i) {
            full[i].ptr = (const void *)&keys[i];
            full[i].byte_size = 16u;
            full[i].elem_size = 1u;
            full[i].generation = (unsigned long long)(i + 1);
        }
        fail_before = stdlib_state.counters[STDLIB_TM_API_FAIL];
        g_hook_calls = 0;
        stdlib_meta_record((const void *)&keys[4], 32u, 1u);
        if (g_hook_calls != 1 || stdlib_state.meta_lock != 0 ||
            stdlib_state.meta_cap != 4u || stdlib_state.meta_used != 4u ||
            stdlib_state.meta_live != 4u ||
            stdlib_state.counters[STDLIB_TM_API_FAIL] <= fail_before)
            ++failures;
        found = 0;
        for (i = 0; i < 4; ++i)
            if (full[i].ptr == (const void *)&keys[4])
                ++found;
        if (found != 0)
            ++failures;
        stdlib_meta_acquire();
        stdlib_meta_release_lock();

        /* 3. OOM with a tombstone available: bounded probe must still insert. */
        stdlib_meta_forget((const void *)&keys[0]);
        stdlib_meta_record((const void *)&keys[5], 24u, 2u);
        found = 0;
        for (i = 0; i < 4; ++i)
            if (full[i].ptr == (const void *)&keys[5])
                ++found;
        if (found != 1 || stdlib_state.meta_live != 4u ||
            stdlib_state.meta_lock != 0)
            ++failures;

        /* 4. With the hook removed, normal growth into a larger table works. */
        stdlib_meta_set_calloc_hook((stdlib_meta_calloc_fn)0);
        stdlib_meta_record((const void *)&keys[6], 40u, 4u);
        if (stdlib_state.meta_cap != 8u || stdlib_state.meta_lock != 0 ||
            stdlib_meta_find((const void *)&keys[6]) == (stdlib_meta_slot *)0)
            ++failures;
        free(stdlib_state.meta_tab);
    }

    /* Restore the process-wide metadata table used by the checked allocator. */
    stdlib_meta_set_calloc_hook((stdlib_meta_calloc_fn)0);
    stdlib_state.meta_tab = saved_tab;
    stdlib_state.meta_cap = saved_cap;
    stdlib_state.meta_used = saved_used;
    stdlib_state.meta_live = saved_live;
    stdlib_telemetry_set_enabled(0);

    print("meta_record-oom-regression", failures == 0 ? 1 : 0, g_hook_calls);
    return failures == 0 ? 0 : 1;
}
