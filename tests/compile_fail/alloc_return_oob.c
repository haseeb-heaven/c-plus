/* The extent of the allocation returned by get_data must be known at the
 * call site, so the loop in main is also rejected. */
#include <stdio.h>
#include "cplus/cplus.h"

int *get_data(size_t size) {
    int *arr = (int *)cplus_alloc(sizeof(int) * size);
    return arr;
}

int main(void) {
    int *arr = get_data(5);
    for (int idx = 0; idx < 10; idx++)
        printf("%d\t", arr[idx]);
    return 0;
}
