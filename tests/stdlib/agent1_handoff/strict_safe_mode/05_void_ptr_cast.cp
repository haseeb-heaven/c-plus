// OP: cast from void* to typed pointer.
// stdlib_alloc returns void*; casting it to int* and subscripting asserts both
// a type and an extent that the cast itself does not prove.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int main(void) {
    void *mem = stdlib_alloc(4);        /* void* from managed allocator */
    int *p = (int *)mem;                /* cast to typed pointer, unproven */
    if (p == NULL)
        return 1;
    p[0] = 7;                           /* typed access via cast */
    stdlib_release(mem);
    return p[0] == 7 ? 0 : 2;
}
