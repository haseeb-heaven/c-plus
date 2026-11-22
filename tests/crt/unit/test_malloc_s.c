#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include "safe_alloc.h"
int main(void) {
    void *p = malloc_s(32);
    assert(p != NULL && cplus_safe_ptr_is_live(p));
    free_s(p);
    assert(!cplus_safe_ptr_is_live(p));
    errno = 0;
    assert(malloc_s(SIZE_MAX) == NULL && errno == EINVAL);
    p = malloc_s(0);
    assert(p != NULL);
    free_s(p);
    puts("unit malloc_s: ok");
    return 0;
}
