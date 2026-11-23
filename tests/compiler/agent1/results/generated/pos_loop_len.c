#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/loop_len.c"
#include <stdio.h>

int main(void) {
    int arr[] = {10, 30, 50};
    __typeof__( 0) total = 0;
    for (__typeof__( 0) idx = 0; idx < (3); idx++)
        total += arr[__cplus_bounds_check((size_t)(idx), 3, "arr")];
    printf("total=%d\n", total);
    return 0;
}
