#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/dynamic_len.c"
#include <stdio.h>
#include "cplus/cplus.h"

typedef struct Buffer { int id; } Buffer;

int main(void) {
    int arr[] = {10, 40};
    int *heap = (int *)cplus_alloc(sizeof(int) * 3);
    int *dyn = cplus_new_array(int, 2);
    Buffer *b = cplus_new(Buffer);

    /* Fixed-size allocation lengths are compile-time constant expressions. */
    _Static_assert((2) == 2, "fixed array length");
    _Static_assert(((sizeof ( int ) * 3) / sizeof(*heap)) == 3, "allocation length must fold at compile time");
    _Static_assert(((2)) == 2, "new_array length must fold");
    _Static_assert((1) == 1, "new length must fold");

    printf("static=%zu heap=%zu dyn=%zu buf=%zu\n",
           (size_t)(2), (size_t)((sizeof ( int ) * 3) / sizeof(*heap)), (size_t)((2)), (size_t)(1));
    cplus_release(b);
    cplus_release(dyn);
    cplus_release(heap);
    return 0;
}