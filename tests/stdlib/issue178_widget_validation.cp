/* Issue #178 regression: widget kinds are validated and ID lookup outcomes
 * are unambiguous.  Headless, deterministic, one summary line. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/widgets.hp"

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
    stdlib_window w;
    size_t index = 99u;
    int found = -1;
    char serial[256];

    stdlib_set_error_callback((stdlib_error_callback)0);
    ck(stdlib_window_init(&w, (const stdlib_allocator *)0, 0, 0, 120, 80, 2,
                          &e) == STDLIB_OK,
       "init");
    if (!e.ok()) {
        printf("ISSUE178 %d %d\n", checks, failed);
        return 1;
    }

    /* All four public tags are accepted and stored verbatim. */
    ck(stdlib_widget_add(&w, 1, STDLIB_WIDGET_BUTTON, "b", 0, 0, 2, 2, &e) == 0,
       "kind_button");
    ck(stdlib_widget_add(&w, 2, STDLIB_WIDGET_LABEL, "l", 0, 0, 2, 2, &e) == 1,
       "kind_label");
    ck(stdlib_widget_add(&w, 3, STDLIB_WIDGET_INPUT, "i", 0, 0, 2, 2, &e) == 2,
       "kind_input");
    ck(stdlib_widget_add(&w, 4, STDLIB_WIDGET_PANEL, "p", 0, 0, 2, 2, &e) == 3,
       "kind_panel");
    ck(w.count == 4u, "count_four");

    /* Out-of-range kinds are refused atomically with CATEGORY_INVALID. */
    ck(stdlib_widget_add(&w, 5, 0, "z", 0, 0, 2, 2, &e) == STDLIB_FAIL,
       "kind_zero_refused");
    ck(e.failed_now() == 1 && e.category() == STDLIB_CATEGORY_INVALID,
       "kind_zero_category");
    ck(stdlib_widget_add(&w, 5, -999, "z", 0, 0, 2, 2, &e) == STDLIB_FAIL,
       "kind_negative_refused");
    ck(stdlib_widget_add(&w, 5, 99999, "z", 0, 0, 2, 2, &e) == STDLIB_FAIL,
       "kind_large_refused");
    ck(stdlib_widget_add(&w, 5, STDLIB_WIDGET_KIND_MAX + 1, "z", 0, 0, 2, 2,
                         &e) == STDLIB_FAIL,
       "kind_max_plus_one_refused");
    ck(w.count == 4u && e.failed_now() == 1, "bad_kind_no_change");

    /* Serialization still prints the stored kind integers verbatim. */
    ck(stdlib_window_serialize(&w, serial, sizeof(serial), &e) == STDLIB_OK &&
           strncmp(serial, "WIDGETS 4\nW 1 1 ", 16u) == 0,
       "serialize_kinds");

    /* Three distinct lookup outcomes: found, absent, invalid window. */
    index = 99u;
    found = -1;
    ck(stdlib_widget_index_by_id(&w, 3, &index, &found, &e) == STDLIB_OK &&
           found == 1 && index == 2u && e.ok(),
       "lookup_found");
    index = 99u;
    found = -1;
    ck(stdlib_widget_index_by_id(&w, 987, &index, &found, &e) == STDLIB_OK &&
           found == 0 && index == 0u && e.ok(),
       "lookup_missing_no_error");
    index = 99u;
    found = -1;
    ck(stdlib_widget_index_by_id((const stdlib_window *)0, 1, &index, &found,
                                 &e) == STDLIB_FAIL &&
           found == -1 && index == 99u && e.failed_now() == 1,
       "lookup_invalid_window");
    ck(e.category() == STDLIB_CATEGORY_INVALID &&
           e.code() == CPLUS_EXIT_NULL_POINTER,
       "lookup_invalid_category");

    /* Optional outputs do not change the status distinction. */
    ck(stdlib_widget_index_by_id(&w, 1, (size_t *)0, (int *)0, &e) == STDLIB_OK &&
           e.ok(),
       "lookup_null_outputs_found");
    ck(stdlib_widget_index_by_id(&w, 987, (size_t *)0, (int *)0, &e) == STDLIB_OK &&
           e.ok(),
       "lookup_null_outputs_missing");

    /* find_by_id returns NULL for both miss and error, but only the invalid
     * window populates err. */
    ck(stdlib_widget_find_by_id(&w, 3, &e) == &w.items[2] && e.ok(),
       "find_found");
    ck(stdlib_widget_find_by_id(&w, 987, &e) == (stdlib_widget *)0 && e.ok(),
       "find_missing_no_error");
    ck(stdlib_widget_find_by_id((stdlib_window *)0, 1, &e) ==
           (stdlib_widget *)0 && e.failed_now() == 1,
       "find_invalid_window");

    stdlib_window_drop(&w, &e);
    printf("ISSUE178 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
