#include <stdio.h>
#include "cplus/cplus.h"

typedef struct Buffer { int id; } Buffer;

int main(void) {
    int arr[] = {10, 40};
    int *heap = (int *)cplus_alloc(sizeof(int) * 3);
    int *dyn = cplus_new_array(int, 2);
    Buffer *b = cplus_new(Buffer);

    /* Fixed-size allocation lengths are compile-time constant expressions. */
    _Static_assert(arr.len == 2, "fixed array length");
    _Static_assert(heap.len == 3, "allocation length must fold at compile time");
    _Static_assert(dyn.len == 2, "new_array length must fold");
    _Static_assert(b.len == 1, "new length must fold");

    printf("static=%zu heap=%zu dyn=%zu buf=%zu\n",
           (size_t)arr.len, (size_t)heap.len, (size_t)dyn.len, (size_t)b.len);
    cplus_release(b);
    cplus_release(dyn);
    cplus_release(heap);
    return 0;
}
