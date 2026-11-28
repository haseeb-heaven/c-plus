/* Unit: allocation overflow, failure-preservation and zero-size realloc. */
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

    errno = 0; assert(calloc_s((size_t)-1, 2) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(SIZE_MAX / 2 + 1, 2) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(2, SIZE_MAX / 2 + 1) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(SIZE_MAX, 3) == NULL && errno == EOVERFLOW);
    errno = 0; assert(calloc_s(RSIZE_MAX_MEM, 1) == NULL && errno == ENOMEM);

    /* failed realloc preserves caller pointer and contents */
    void *r = malloc_s(64);
    assert(r != NULL);
    memset(r, 0xC3, 64);
    errno = 0;
    assert(realloc_s(r, (size_t)-1) == NULL && errno == EINVAL);
    assert(cplus_safe_ptr_is_live(r));
    assert(((unsigned char *)r)[0] == 0xC3);
    assert(((unsigned char *)r)[63] == 0xC3);
    free_s(r);

    /* realloc(ptr, 0) stays defined and non-NULL */
    void *q = malloc_s(32);
    assert(q != NULL);
    q = realloc_s(q, 0);
    assert(q != NULL && cplus_safe_ptr_size(q) == 1);
    q = realloc_s(q, 0);
    assert(q != NULL);
    free_s(q);

    /* realloc(NULL, n) allocates; realloc(NULL, 0) stays defined */
    void *a = realloc_s(NULL, 16);
    assert(a != NULL);
    free_s(a);
    a = realloc_s(NULL, 0);
    assert(a != NULL);
    free_s(a);

    /* growth from zero-capacity object preserves payload */
    void *zk = malloc_s(0);
    assert(zk != NULL);
    ((unsigned char *)zk)[0] = 0x7E;
    zk = realloc_s(zk, 256);
    assert(zk != NULL && ((unsigned char *)zk)[0] == 0x7E);
    free_s(zk);

    /* growing realloc keeps bytes */
    void *g = malloc_s(8);
    assert(g != NULL);
    memcpy(g, "wxyz", 5);
    g = realloc_s(g, 128);
    assert(g != NULL && memcmp(g, "wxyz", 5) == 0);
    g = realloc_s(g, 128);
    assert(g != NULL);
    free_s(g);

    puts("test_allocfmt_alloc_overflow: ok");
    return 0;
}
