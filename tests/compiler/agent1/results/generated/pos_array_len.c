#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/array_len.c"
#include <stdio.h>

int main(void) {
    int arr[] = {10, 20, 30};
    int fixed[5];
    int i;

    printf("arr=%d fixed=%d\n", (3), (5));
    for (i = 0; i < (3); i++)
        printf("%d ", arr[__cplus_bounds_check((size_t)(i), 3, "arr")]);
    printf("\n");
    return 0;
}
