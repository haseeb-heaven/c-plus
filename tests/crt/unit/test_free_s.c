#include <assert.h>
#include <stdio.h>
#include "safe_alloc.h"
int main(void) {
    void *p = malloc_s(8);
    assert(p != NULL);
    assert(free_checked_s(p) == CPLUS_SAFE_OK);
    assert(free_checked_s(p) == CPLUS_SAFE_EINVAL);
    free_s(p);
    free_s(NULL);
    puts("unit free_s: ok");
    return 0;
}
