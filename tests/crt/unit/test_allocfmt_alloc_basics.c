/* Unit: checked-allocation basics with standard C signatures. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    void *p = malloc_s(16);
    assert(p != NULL && cplus_safe_ptr_is_live(p));
    memset(p, 0x5A, 16);
    assert(((unsigned char *)p)[0] == 0x5A);
    assert(((unsigned char *)p)[15] == 0x5A);
    assert(free_checked_s(p) == CPLUS_SAFE_OK);
    assert(!cplus_safe_ptr_is_live(p));
    assert(free_checked_s(p) == CPLUS_SAFE_EINVAL); /* double free refused */
    free_s(NULL);

    /* malloc(0) -> defined 1-byte live object */
    void *z = malloc_s(0);
    assert(z != NULL && cplus_safe_ptr_size(z) == 1);
    free_s(z);

    /* huge malloc fails cleanly with NULL + errno */
    errno = 0;
    assert(malloc_s((size_t)-1) == NULL && errno == EINVAL);

    /* calloc zero-initialises */
    void *c = calloc_s(8, 4);
    assert(c != NULL && cplus_safe_ptr_size(c) == 32);
    for (int i = 0; i < 32; ++i) assert(((unsigned char *)c)[i] == 0);
    free_s(c);

    /* zero-dimension calloc normalises to 1 zeroed byte */
    void *c0 = calloc_s(0, 0);
    assert(c0 != NULL);
    free_s(c0);
    c0 = calloc_s(4, 0);
    assert(c0 != NULL);
    free_s(c0);
    c0 = calloc_s(0, 4);
    assert(c0 != NULL);
    free_s(c0);

    puts("test_allocfmt_alloc_basics: ok");
    return 0;
}
