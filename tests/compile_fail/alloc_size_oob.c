/* The allocation is for `size` elements, but the loop writes 10. */
#include "cplus/cplus.h"

int *get_data(size_t size) {
    int *arr = (int *)cplus_alloc(sizeof(int) * size);
    int *ptr = arr;
    for (int idx = 0; idx < 10; idx++) {
        arr[idx] = idx * 10;
    }
    (void)ptr;
    return arr;
}

int main(void) {
    int *arr = get_data(5);
    (void)arr;
    return 0;
}
