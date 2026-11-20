#include <stdio.h>
#include "cplus/cplus.h"

typedef struct Buffer { int id; } Buffer;

int main(void) {
    int arr[] = {10, 40};
    int *heap = (int *)cplus_alloc(sizeof(int) * 3);
    int *dyn = cplus_new_array(int, 2);
    Buffer *b = cplus_new(Buffer);

    printf("static=%zu heap=%zu dyn=%zu buf=%zu\n",
           (size_t)arr.len, (size_t)heap.len, (size_t)dyn.len, (size_t)b.len);
    cplus_release(b);
    cplus_release(dyn);
    cplus_release(heap);
    return 0;
}
