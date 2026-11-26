/* Issue #42 regression: a string_view that escapes its owning string must be
 * rejected before any byte is read.  The owner publishes its buffer in the
 * shared lifetime registry; drop()/reallocation retires that token. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"

static int checks = 0;
static int failed = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) {
        failed++;
        printf("FAIL %s\n", tag);
    }
}

static string_view escaped_view(void) {
    string s = string("temporary_confidential_token");
    return s.to_view(); /* s is dropped at function exit */
}

int main(void) {
    string_view stale = escaped_view();
    string live = string("visible");
    string_view fresh = live.to_view();
    const char *stale_raw = stale.raw_data(); /* identity only, never deref */

    stdlib_set_error_callback((stdlib_error_callback)0);
    ck(stale_raw != (const char *)0, "stale_raw_recorded");
    ck(stale.is_live() == 0, "stale_not_live");
    ck(stale.data() == (const char *)0, "stale_data_null");
    ck(stale.equals("temporary_confidential_token") == 0, "stale_equals_refused");
    ck(stale.is_valid_utf8() == 0, "stale_utf8_refused");

    ck(fresh.is_live() == 1 && fresh.data() == live.c_str(), "fresh_live");
    ck(fresh.size() == 7 && fresh.equals("visible") == 1 &&
       fresh.is_valid_utf8() == 1, "fresh_content");
    live.reserve(128); /* moves the buffer -> old view goes stale */
    ck(fresh.is_live() == 0 && fresh.data() == (const char *)0,
       "realloc_stale");
    live.drop();
    ck(fresh.data() == (const char *)0, "drop_stale");

    /* Direct, caller-managed views keep their documented legacy behaviour. */
    {
        string_view direct = string_view("literal", 7);
        ck(direct.data() != (const char *)0 && direct.equals("literal") == 1,
           "direct_view_unchanged");
    }

    printf("ISSUE42 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
