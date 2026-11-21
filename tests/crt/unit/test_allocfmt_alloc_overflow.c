/* Unit: allocation overflow, failure-preservation and zero-size realloc.
 *
 * Catalogue mapping:
 *   bug41/bug105 - count*size overflow must be detected before the allocator.
 *   bug32        - failed realloc must preserve the caller's pointer AND bytes.
 *   bug179       - growth from a zero-capacity object must be defined.
 *   bug199       - realloc(ptr, 0) must have a defined, non-NULL outcome.
 *   bug02        - every failing path leaves a defined (NULL) handle.
 */
#include <assert.h>
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

    /* ---- product overflow is EOVERFLOW, handle cleared (bug41/105) ---- */
    void *p = (void *)0xdeadbeef;
    assert(calloc_s(&p, (size_t)-1, 2) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    assert(calloc_s(&p, SIZE_MAX / 2 + 1, 2) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    assert(calloc_s(&p, 2, SIZE_MAX / 2 + 1) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    assert(calloc_s(&p, SIZE_MAX, 3) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);

    /* ---- representable-but-huge is OOM, not overflow ---- */
    assert(calloc_s(&p, SIZE_MAX, 1) == CPLUS_SAFE_ENOMEM);
    assert(p == NULL);

    /* ---- failed realloc preserves pointer and contents (bug32) ---- */
    void *r = NULL;
    assert(malloc_s(&r, 64) == CPLUS_SAFE_OK);
    memset(r, 0xC3, 64);
    void *old = r;
    assert(realloc_s(&r, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(r == old);                          /* no leak, no dangling */
    assert(((unsigned char *)r)[0] == 0xC3);
    assert(((unsigned char *)r)[63] == 0xC3);
    free_s(&r);
    assert(r == NULL);

    /* ---- realloc(ptr, 0) is defined and keeps a non-NULL owner (bug199) ---- */
    void *q = NULL;
    assert(malloc_s(&q, 32) == CPLUS_SAFE_OK);
    assert(realloc_s(&q, 0) == CPLUS_SAFE_OK);
    assert(q != NULL);
    assert(realloc_s(&q, 0) == CPLUS_SAFE_OK);  /* idempotent */
    assert(q != NULL);
    free_s(&q);
    assert(q == NULL);

    /* ---- realloc(NULL, n) allocates; realloc(NULL, 0) stays defined ---- */
    void *a = NULL;
    assert(realloc_s(&a, 16) == CPLUS_SAFE_OK);
    assert(a != NULL);
    free_s(&a);
    a = NULL;
    assert(realloc_s(&a, 0) == CPLUS_SAFE_OK);  /* non-NULL 1-byte object */
    assert(a != NULL);
    free_s(&a);
    assert(a == NULL);

    /* ---- growth from a zero-capacity object preserves payload (bug179) ---- */
    void *zk = NULL;
    assert(malloc_s(&zk, 0) == CPLUS_SAFE_OK);  /* 1-byte, defined (bug51) */
    assert(zk != NULL);
    ((unsigned char *)zk)[0] = 0x7E;
    assert(realloc_s(&zk, 256) == CPLUS_SAFE_OK);
    assert(zk != NULL);
    assert(((unsigned char *)zk)[0] == 0x7E);
    free_s(&zk);
    assert(zk == NULL);

    /* ---- growing realloc keeps bytes; NULL handle rejected ---- */
    void *g = NULL;
    assert(malloc_s(&g, 8) == CPLUS_SAFE_OK);
    memcpy(g, "wxyz", 5);
    assert(realloc_s(&g, 128) == CPLUS_SAFE_OK);
    assert(g != NULL);
    assert(memcmp(g, "wxyz", 5) == 0);
    assert(realloc_s(&g, 128) == CPLUS_SAFE_OK); /* no-op size still defined */
    assert(g != NULL);
    free_s(&g);
    assert(g == NULL);
    assert(realloc_s(NULL, 8) == CPLUS_SAFE_EINVAL);

    puts("test_allocfmt_alloc_overflow: ok");
    return 0;
}
