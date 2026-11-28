/* Issue #45 regression: ctl_span_init must refuse stack backing storage and
 * must reject access after the tracked allocation is released.  The unsafe
 * opt-in remains available for callers that intentionally keep raw pointers. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/ctl_span.hp"

static int checks = 0;
static int failed = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) {
        failed++;
        printf("FAIL %s\n", tag);
    }
}

static ctl_span escaping_span(void) {
    int local_buf[4] = {10, 20, 30, 40};
    ctl_span s;
    error e = error();
    /* Seed a well-defined empty view; the checked init below must refuse the
     * stack buffer and leave these fields untouched. */
    ctl_span_init_unsafe(&s, (void *)0, 0u, sizeof(int), &e);
    ck(ctl_span_init(&s, local_buf, 4u, sizeof(int), &e) != 0,
       "escaped_stack_refused");
    return s;
}

int main(void) {
    ctl_span s;
    ctl_span sub;
    error e = error();
    int local_buf[4] = {10, 20, 30, 40};
    int *heap;
    int i;

    stdlib_set_error_callback((stdlib_error_callback)0);
    ck(ctl_span_init(&s, local_buf, 4u, sizeof(int), &e) != 0,
       "stack_init_refused");
    ck(e.category() == STDLIB_CATEGORY_LIFECYCLE, "stack_refusal_category");
    ck(ctl_span_at(&s, 0u, &e) == (void *)0, "refused_span_no_access");

    {
        ctl_span escaped = escaping_span();
        ck(ctl_span_size(&escaped) == 0u || ctl_span_at(&escaped, 0u, &e) == (void *)0,
           "escaped_stack_span_unusable");
    }

    heap = (int *)stdlib_alloc(4u * sizeof(int));
    ck(heap != (int *)0, "heap_alloc");
    for (i = 0; i < 4; ++i) heap[i] = i + 1;
    ck(ctl_span_init(&s, heap, 4u, sizeof(int), &e) == 0, "heap_init_ok");
    ck(*(int *)ctl_span_at(&s, 3u, &e) == 4, "heap_at");
    ck(ctl_span_slice(&s, 1u, 2u, &sub, &e) == 0, "heap_slice");
    ck(*(int *)ctl_span_at(&sub, 0u, &e) == 2, "slice_at_tracked_base");
    stdlib_release(heap);
    ck(ctl_span_at(&s, 0u, &e) == (void *)0, "stale_span_refused");
    ck(e.category() == STDLIB_CATEGORY_LIFECYCLE, "stale_refusal_category");

    ck(ctl_span_init_unsafe(&s, local_buf, 4u, sizeof(int), &e) == 0 &&
       *(int *)ctl_span_at(&s, 0u, &e) == 10, "unsafe_opt_in");

    printf("ISSUE45 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
