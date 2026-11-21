/* Unit: checked-allocation overflow + uninitialised-out-param safety.
 *
 * Second adversarial catalogue (bugs 105/107): a caller-side size computation
 * can overflow before it ever reaches the allocator, and callers frequently
 * reuse an output handle forever.  The checked C+ allocators must (a) reject a
 * count*size product that overflows size_t, and (b) leave the caller's output
 * handle in a *defined* state (NULL) on every failing path - never a stale or
 * uninitialised pointer.
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

    /* ---- overflow rejection: count * size wraps size_t (bug105) ---- */
    void *p = NULL;
    assert(calloc_s(&p, (size_t)-1, 4) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    assert(calloc_s(&p, SIZE_MAX / 4 + 1, 4) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    assert(calloc_s(&p, 2, SIZE_MAX / 2 + 1) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);

    /* A product that is exactly representable is NOT overflow: it fails as OOM
     * with a defined error value instead. */
    assert(calloc_s(&p, SIZE_MAX / 2, 2) == CPLUS_SAFE_ENOMEM);
    assert(p == NULL);
    assert(calloc_s(&p, SIZE_MAX, 1) == CPLUS_SAFE_ENOMEM);
    assert(p == NULL);
    assert(calloc_s(&p, 1, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(p == NULL);

    /* ---- uninitialised-out-param safety (bug107) ---- */
    /* a garbage caller handle must be overwritten with a defined value on failure */
    void *garbage = (void *)0xdeadbeef;
    assert(calloc_s(&garbage, (size_t)-1, 8) == CPLUS_SAFE_EOVERFLOW);
    assert(garbage == NULL);
    garbage = (void *)0xdeadbeef;
    assert(calloc_s(&garbage, SIZE_MAX, 1) == CPLUS_SAFE_ENOMEM);
    assert(garbage == NULL);

    /* NULL out-param is rejected, never dereferenced */
    assert(calloc_s(NULL, 4, 4) == CPLUS_SAFE_EINVAL);

    /* ---- zero-argument contract is defined, not UB ---- */
    void *z = NULL;
    assert(calloc_s(&z, 0, 0) == CPLUS_SAFE_OK);
    assert(z != NULL);
    free_s(&z);
    assert(calloc_s(&z, 4, 0) == CPLUS_SAFE_OK);   /* size == 0 -> normalised */
    assert(z != NULL);
    free_s(&z);

    /* ---- a successful calloc returns zero-initialised storage ---- */
    void *q = NULL;
    assert(calloc_s(&q, 4, 8) == CPLUS_SAFE_OK);
    assert(q != NULL);
    for (int i = 0; i < 32; ++i) assert(((unsigned char *)q)[i] == 0);
    free_s(&q);
    assert(q == NULL);

    /* ---- malloc_s shares the same overflow/failure discipline ---- */
    void *m = (void *)0xdeadbeef;
    assert(malloc_s(NULL, 16) == CPLUS_SAFE_EINVAL);
    assert(malloc_s(&m, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(m == NULL);
    assert(malloc_s(&m, 0) == CPLUS_SAFE_OK);      /* zero -> 1 byte, defined */
    assert(m != NULL);
    free_s(&m);
    assert(m == NULL);
    free_s(&m);                                    /* idempotent */
    free_s(NULL);                                  /* no crash */

    puts("test_calloc_s_overflow_uninit: ok");
    return 0;
}
