#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_alloc.h"
int main(void) {
    void *p = NULL;
    assert(malloc_s(&p, 8) == CPLUS_SAFE_OK);
    memcpy(p, "abc", 4);
    void *old = p;
    assert(realloc_s(&p, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(p == old); /* preserved */
    assert(realloc_s(&p, 32) == CPLUS_SAFE_OK);
    assert(memcmp(p, "abc", 4) == 0);
    free(p);
    assert(realloc_s(NULL, 8) == CPLUS_SAFE_EINVAL);
    puts("unit realloc_s: ok");
    return 0;
}
