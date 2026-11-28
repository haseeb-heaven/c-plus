#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "safe_alloc.h"
int main(void) {
    void *p = malloc_s(32);
    assert(p != NULL);
    memset(p, 7, 32);
    p = realloc_s(p, 64);
    assert(p != NULL);
    assert(((unsigned char *)p)[0] == 7);
    assert(free_checked_s(p) == CPLUS_SAFE_OK);
    assert(free_checked_s(p) == CPLUS_SAFE_EINVAL); /* double free refused */
    free_s(NULL); /* no-op */
    errno = 0;
    assert(malloc_s(SIZE_MAX) == NULL && errno == EINVAL);

    void *q = calloc_s(4, 8);
    assert(q != NULL);
    for (int i = 0; i < 32; i++) assert(((unsigned char *)q)[i] == 0);
    /* realloc failure preserves owner */
    errno = 0;
    void *np = realloc_s(q, (size_t)-1);
    assert(np == NULL && errno == EINVAL);
    assert(cplus_safe_ptr_is_live(q));
    free_s(q);

    /* calloc overflow */
    errno = 0;
    assert(calloc_s((size_t)-1, 2) == NULL && errno == EOVERFLOW);
    puts("test_safe_alloc: ok");
    return 0;
}
