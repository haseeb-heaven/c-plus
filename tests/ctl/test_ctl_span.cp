/* Checked CTL span wrapper tests (issue #24 ADOPT row). */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/ctl_span.hp"

static int checks = 0;
static int passed = 0;

static void check(int cond, const char *tag) {
    checks++;
    if (cond) passed++;
    else printf("CTL_SPAN CHECK_FAIL %s\n", tag);
}

int main(void) {
    int a[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    ctl_span s, sub, empty;
    error e = error();
    int *p;

    stdlib_set_error_callback((stdlib_error_callback)0);
    check(ctl_span_init(&s, a, 8u, sizeof(int), &e) == 0, "init");
    p = (int *)ctl_span_at(&s, 7u, &e);
    check(p == &a[7] && *p == 7, "at_last");
    check(ctl_span_at(&s, 8u, &e) == (void *)0, "at_refuse");
    check(e.code() == CPLUS_EXIT_BOUNDS, "at_refuse_code");
    check(ctl_span_slice(&s, 2u, 3u, &sub, &e) == 0, "slice");
    check(ctl_span_size(&sub) == 3u, "slice_size");
    check(*(int *)ctl_span_at(&sub, 0u, &e) == 2, "slice_offset");
    check(ctl_span_slice(&s, 7u, 3u, &sub, &e) != 0, "slice_refuse");
    check(ctl_span_init(&empty, (void *)0, 0u, sizeof(int), &e) == 0 &&
          ctl_span_size(&empty) == 0u, "empty_ok");
    check(ctl_span_at(&empty, 0u, &e) == (void *)0, "empty_at_refuse");
    check(ctl_span_init(&s, a, 8u, 0u, &e) != 0, "zero_elem_refuse");
    check(ctl_span_init(&s, (void *)0, 1u, sizeof(int), &e) != 0,
          "null_data_refuse");
    check(ctl_span_from_ctl_vector(a, 8u, sizeof(int), &sub, &e) == 0 &&
          ctl_span_size(&sub) == 8u, "from_ctl_storage");
    printf("CTL_SPAN %d %d %d\n", checks, passed, checks - passed);
    return checks == passed ? 0 : 1;
}
