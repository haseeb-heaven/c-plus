#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "safe_alloc.h"
int main(void) {
    void *p = malloc_s(8);
    assert(p != NULL);
    memcpy(p, "abc", 4);
    errno = 0;
    assert(realloc_s(p, (size_t)-1) == NULL && errno == EINVAL);
    assert(cplus_safe_ptr_is_live(p));
    void *q = realloc_s(p, 32);
    assert(q != NULL);
    p = q;
    assert(memcmp(p, "abc", 4) == 0);
    free_s(p);
    /* realloc_s(NULL, n) allocates a fresh block */
    void *r = realloc_s(NULL, 8);
    assert(r != NULL);
    free_s(r);
    puts("unit realloc_s: ok");
    return 0;
}
