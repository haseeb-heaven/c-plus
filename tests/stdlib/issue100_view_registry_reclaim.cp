/* Issue #100 regression: retired registry slots must be reclaimed.
 *
 * A fixed table of 4096 open-addressed slots fills up after that many distinct
 * addresses.  Retirement keeps the key as a tombstone (probe chains stay
 * intact) but the slot must be reusable, otherwise every later view token is
 * 0 and every view reports stale.  This test churns more distinct addresses
 * than the table has slots, then checks that fresh views are still usable. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/view_lifetime.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

static int checks = 0;
static int failed = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) {
        failed++;
        printf("FAIL %s\n", tag);
    }
}

#define CHURN ((size_t)4200) /* > STDLIB_VIEW_TABLE_SIZE (4096) */

static char backing[CHURN][16];
static unsigned long long tokens[CHURN];

int main(void) {
    size_t i;
    unsigned long long fresh_token;
    char scratch[16];
    string s;
    string_view sv;
    vector v;
    slice sl;

    stdlib_set_error_callback((stdlib_error_callback)0);

    for (i = 0; i < CHURN; ++i)
        tokens[i] = stdlib_view_register(&backing[i][0], 16u);
    /* All 4096 slots are live; the remaining registrations fail closed. */
    for (i = 0; i < STDLIB_VIEW_TABLE_SIZE; ++i)
        ck(tokens[i] != 0 && stdlib_view_validate(&backing[i][0],
                                                  tokens[i]) == 1,
           "early_registration_live");

    for (i = 0; i < CHURN; ++i)
        stdlib_view_retire(&backing[i][0]);
    for (i = 0; i < STDLIB_VIEW_TABLE_SIZE; ++i)
        ck(stdlib_view_validate(&backing[i][0], tokens[i]) == 0 &&
           stdlib_view_is_live(&backing[i][0]) == 0,
           "retired_slot_not_live");

    /* Fresh registration must reuse a tombstone (issue #100 root cause). */
    fresh_token = stdlib_view_register(&scratch[0], sizeof(scratch));
    ck(fresh_token != 0, "tombstone_reclaimed");
    ck(stdlib_view_validate(&scratch[0], fresh_token) == 1,
       "fresh_token_valid");
    ck(stdlib_view_token_of(&scratch[0]) == fresh_token, "fresh_token_of");
    ck(stdlib_view_size(&scratch[0]) == sizeof(scratch), "fresh_size");
    ck(stdlib_view_validate(&backing[0][0], tokens[0]) == 0,
       "old_token_not_revived");

    /* Re-registering an old address refreshes its token in place. */
    {
        unsigned long long again =
            stdlib_view_register(&backing[0][0], 16u);
        ck(again != 0 && again != tokens[0], "reregister_new_token");
        ck(stdlib_view_validate(&backing[0][0], again) == 1 &&
           stdlib_view_validate(&backing[0][0], tokens[0]) == 0,
           "old_token_superseded");
    }

    /* End-to-end: new string/vector views after the churn are usable. */
    s = string("after_churn");
    sv = s.to_view();
    ck(sv.is_live() == 1 && sv.data() == s.c_str() &&
       sv.equals("after_churn") == 1, "string_view_after_churn");
    v = vector();
    ck(v.push_back(7) == 0, "vector_push");
    sl = v.view();
    ck(sl.stale() == 0 && sl.valid() == 1 && sl.data() == v.items,
       "slice_after_churn");

    printf("ISSUE100 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
