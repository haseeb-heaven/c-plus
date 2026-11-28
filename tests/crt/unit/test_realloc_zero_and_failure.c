/* Unit: realloc_s(ptr, 0) defined outcome; failed realloc preserves owner. */
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

    void *p = malloc_s(32);
    assert(p != NULL);
    p = realloc_s(p, 0);
    assert(p != NULL);
    p = realloc_s(p, 0);
    assert(p != NULL);
    free_s(p);

    void *q = realloc_s(NULL, 16);
    assert(q != NULL);
    free_s(q);

    void *r = malloc_s(32);
    assert(r != NULL);
    memset(r, 0xAB, 32);
    errno = 0;
    assert(realloc_s(r, (size_t)-1) == NULL && errno == EINVAL);
    assert(cplus_safe_ptr_is_live(r));
    assert(((unsigned char *)r)[0] == 0xAB && ((unsigned char *)r)[31] == 0xAB);
    free_s(r);

    void *g = malloc_s(8);
    assert(g != NULL);
    memcpy(g, "wxyz", 5);
    g = realloc_s(g, 128);
    assert(g != NULL && memcmp(g, "wxyz", 5) == 0);
    free_s(g);

    void *s = malloc_s(16);
    assert(s != NULL);
    s = realloc_s(s, 16);
    assert(s != NULL);
    free_s(s);

    void *d = malloc_s(8);
    assert(d != NULL);
    assert(free_checked_s(d) == CPLUS_SAFE_OK);
    assert(free_checked_s(d) == CPLUS_SAFE_EINVAL);
    free_s(NULL);

    puts("test_realloc_zero_and_failure: ok");
    return 0;
}
