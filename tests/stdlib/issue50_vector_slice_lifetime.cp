/* Issue #50 regression: slice must not hold a pointer into the owner object
 * and must not expose a raw writable array.  The generation is kept in the
 * stable lifetime registry; escaped slices are stale and all accessors refuse. */
#include <stdio.h>
#include "cplus/cplus.h"
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

static slice escaped_slice(void) {
    vector v = vector();
    v.push_back(777);
    v.push_back(888);
    return v.view(); /* v is dropped here */
}

int main(void) {
    slice stale = escaped_slice();
    int out = -1;
    int stack_items[4] = {1, 2, 3, 4};

    stdlib_set_error_callback((stdlib_error_callback)0);
    ck(stale.raw_data() != (int *)0, "stale_address_recorded");
    ck(stale.stale() == 1 && stale.valid() == 0, "escaped_stale");
    ck(stale.data() == (int *)0, "escaped_data_null");
    ck(stale.at_checked(0, &out) == -1 && out == -1, "escaped_read_refused");
    ck(stale.set_checked(0, 99999) == -1, "escaped_write_refused");

    {
        vector v = vector();
        slice live;
        slice part;
        ck(v.push_back(10) == 0 && v.push_back(20) == 0, "owner_setup");
        live = v.view();
        ck(live.stale() == 0 && live.valid() == 1 && live.size() == 2,
           "live_slice");
        ck(live.set_checked(1, 42) == 0 && live.at_checked(1, &out) == 0 &&
           out == 42, "checked_write_read");
        part = live.sub(1, 1);
        ck(part.stale() == 0 && part.at_checked(0, &out) == 0 && out == 42,
           "sub_same_generation");
        v.push_back(30); /* no reallocation: existing slices stay live */
        ck(live.stale() == 0 && part.stale() == 0, "no_realloc_push");
        v.erase(0, &out);
        ck(live.stale() == 1 && part.stale() == 1 &&
           live.at_checked(0, &out) == -1, "shape_change_stale");
    }

    {
        slice direct = slice(stack_items, 4);
        ck(direct.stale() == 0 && direct.at_checked(0, &out) == 0 &&
           out == 1, "direct_slice_unchanged");
        ck(direct.set_checked(3, 9) == 0 && stack_items[3] == 9,
           "direct_checked_write");
    }

    printf("ISSUE50 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
