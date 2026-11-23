#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/auto_infer.c"
#include <stdio.h>

int main(void) {
    int arr[] = {10, 20, 30};
    __typeof__( (3)) n = (3);
    __typeof__( 0) total = 0;
    for (__typeof__( 0) i = 0; i < n; i++)
        total += arr[__cplus_bounds_check((size_t)(i), 3, "arr")];
    __typeof__( &total) p = &total;
    printf("n=%d total=%d p=%d\n", n, total, *p);
    return 0;
}
