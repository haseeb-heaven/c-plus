/* Unit: checked-allocation basics and out-handle discipline.
 *
 * Catalogue mapping:
 *   bug02 - unchecked malloc(): malloc_s/calloc_s return a status instead of a
 *           bare pointer; every failure path leaves a defined (NULL) handle.
 *   bug33 - uninitialised heap read: calloc_s hand-back is fully zeroed.
 *   bug51 - malloc(0): the checked API maps zero to a real 1-byte object.
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

    /* ---- out-handle is overwritten (never left stale) on success ---- */
    void *p = (void *)0x1;                    /* poison */
    assert(malloc_s(&p, 16) == CPLUS_SAFE_OK);
    assert(p != NULL);
    memset(p, 0x5A, 16);                      /* full declared range is usable */
    assert(((unsigned char *)p)[0] == 0x5A);
    assert(((unsigned char *)p)[15] == 0x5A);
    free_s(&p);
    assert(p == NULL);                        /* free_s clears the handle */
    free_s(&p);                               /* idempotent */
    assert(p == NULL);
    free_s(NULL);                             /* no crash */

    /* ---- NULL out-handle is rejected, never dereferenced (bug02) ---- */
    assert(malloc_s(NULL, 16) == CPLUS_SAFE_EINVAL);

    /* ---- malloc(0) is defined and non-NULL (bug51) ---- */
    void *z = NULL;
    assert(malloc_s(&z, 0) == CPLUS_SAFE_OK);
    assert(z != NULL);
    free_s(&z);
    assert(z == NULL);

    /* ---- failing malloc clears a poisoned handle to NULL (bug02) ---- */
    void *fail = (void *)0x1;
    assert(malloc_s(&fail, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(fail == NULL);

    /* ---- calloc zero-initialises: no uninitialised heap read (bug33) ---- */
    void *c = NULL;
    assert(calloc_s(&c, 8, 4) == CPLUS_SAFE_OK);
    assert(c != NULL);
    {
        int all_zero = 1;
        for (int i = 0; i < 32; ++i)
            if (((unsigned char *)c)[i] != 0) { all_zero = 0; break; }
        assert(all_zero);
    }
    free_s(&c);
    assert(c == NULL);

    /* ---- zero-dimension calloc normalises to a real 1-byte object (bug51) ---- */
    void *c0 = NULL;
    assert(calloc_s(&c0, 0, 0) == CPLUS_SAFE_OK);
    assert(c0 != NULL);
    free_s(&c0);
    assert(calloc_s(&c0, 4, 0) == CPLUS_SAFE_OK);
    assert(c0 != NULL);
    free_s(&c0);
    assert(calloc_s(&c0, 0, 4) == CPLUS_SAFE_OK);
    assert(c0 != NULL);
    free_s(&c0);
    assert(c0 == NULL);

    /* ---- NULL out-handle rejected by calloc too ---- */
    assert(calloc_s(NULL, 1, 1) == CPLUS_SAFE_EINVAL);

    puts("test_allocfmt_alloc_basics: ok");
    return 0;
}
