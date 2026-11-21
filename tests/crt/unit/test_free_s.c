#include <assert.h>
#include <stdio.h>
#include "safe_alloc.h"
int main(void) {
    void *p = NULL;
    assert(malloc_s(&p, 8) == CPLUS_SAFE_OK);
    free_s(&p); assert(p == NULL);
    free_s(&p); assert(p == NULL);
    free_s(NULL);
    puts("unit free_s: ok");
    return 0;
}
