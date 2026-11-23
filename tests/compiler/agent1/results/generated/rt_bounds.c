#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/runtime_fail/bounds.c"
#include <stdio.h>

/* A constant bad index is rejected at compile time.  This test exercises
 * the other half of the safety model: an index that is only known at run
 * time must abort safely. */
static volatile int bad_index = 7;

int main(void) {
    int values[4];
    int i;
    for (i = 0; i < 4; i++)
        values[__cplus_bounds_check((size_t)(i), 4, "values")] = i;
    printf("in range %d\n", values[3]);
    printf("out of range %d\n", values[__cplus_bounds_check((size_t)(bad_index), 4, "values")]);
    return 0;
}