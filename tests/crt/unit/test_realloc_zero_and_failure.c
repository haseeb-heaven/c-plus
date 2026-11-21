/* Unit: realloc_s(ptr, 0) defined outcome; failed realloc preserves the owner.
 *
 * Second adversarial catalogue (bug199): realloc(ptr, 0) is implementation-
 * defined/UB-adjacent in raw C.  The checked wrapper must give it a defined
 * outcome while keeping the handle owned until free_s.  (bug032): a failed
 * realloc must preserve the caller's original pointer - no leak, no dangling.
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

    /* ---- realloc_s(ptr, 0): defined, handle stays owned (bug199) ---- */
    void *p = NULL;
    assert(malloc_s(&p, 32) == CPLUS_SAFE_OK);
    assert(p != NULL);
    assert(realloc_s(&p, 0) == CPLUS_SAFE_OK);
    assert(p != NULL);                       /* still owned; freed only by free_s */
    assert(realloc_s(&p, 0) == CPLUS_SAFE_OK);   /* idempotent, still defined */
    assert(p != NULL);
    free_s(&p);
    assert(p == NULL);

    /* ---- realloc_s(NULL, n) behaves as an allocation ---- */
    void *q = NULL;
    assert(realloc_s(&q, 16) == CPLUS_SAFE_OK);
    assert(q != NULL);
    free_s(&q);
    assert(q == NULL);

    /* ---- failed realloc preserves pointer + contents (bug032) ---- */
    void *r = NULL;
    assert(malloc_s(&r, 32) == CPLUS_SAFE_OK);
    memset(r, 0xAB, 32);
    void *const old = r;
    assert(realloc_s(&r, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(r == old);                        /* handle preserved, not leaked */
    assert(((unsigned char *)r)[0] == 0xAB);
    assert(((unsigned char *)r)[31] == 0xAB);
    free_s(&r);
    assert(r == NULL);

    /* ---- growing preserves the existing bytes ---- */
    void *g = NULL;
    assert(malloc_s(&g, 8) == CPLUS_SAFE_OK);
    memcpy(g, "wxyz", 5);
    assert(realloc_s(&g, 128) == CPLUS_SAFE_OK);
    assert(g != NULL);
    assert(memcmp(g, "wxyz", 5) == 0);
    free_s(&g);

    /* ---- same-size realloc is a defined no-op outcome ---- */
    void *s = NULL;
    assert(malloc_s(&s, 16) == CPLUS_SAFE_OK);
    assert(realloc_s(&s, 16) == CPLUS_SAFE_OK);
    assert(s != NULL);
    free_s(&s);

    /* ---- NULL handle input rejected; free_s nulls the handle ---- */
    assert(realloc_s(NULL, 8) == CPLUS_SAFE_EINVAL);
    void *d = NULL;
    assert(malloc_s(&d, 8) == CPLUS_SAFE_OK);
    free_s(&d);
    assert(d == NULL);
    free_s(&d);                              /* idempotent double free */
    free_s(NULL);                            /* no crash */

    puts("test_realloc_zero_and_failure: ok");
    return 0;
}
