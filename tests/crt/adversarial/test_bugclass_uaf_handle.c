/*
 * Bug CLASS: use-after-free / double-free / dangling handle.
 *
 * Raw C: free(p) leaves a dangling p; free(p) again is UB; realloc(p, big) on
 * failure leaves the only reference dangling (leak/UAF). The checked C+ handle
 * API is address-of-pointer based:
 *   - free_s(&p) nulls the handle, so a later *read* sees NULL, not a dangling p.
 *   - a second free_s(&p) is a safe no-op (handle already NULL).
 *   - realloc_s(&p, huge) that fails PRESERVES the original pointer/contents.
 */
#include <stdio.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* --- free_s nulls the handle, so no dangling pointer remains --- */
    void *p = NULL;
    CHECK(malloc_s(&p, 32) == CPLUS_SAFE_OK);
    CHECK(p != NULL);
    memset(p, 0xAB, 32);
    free_s(&p);
    CHECK(p == NULL); /* handle cleared: cannot be dereferenced / reused */

    /* double free / free of a cleared handle is a safe no-op */
    free_s(&p);
    CHECK(p == NULL);
    free_s(&p);
    CHECK(p == NULL);
    free_s(NULL); /* NULL out-param is tolerated */

    /* --- realloc failure must preserve the original owner --- */
    void *q = NULL;
    CHECK(malloc_s(&q, 64) == CPLUS_SAFE_OK);
    CHECK(q != NULL);
    memset(q, 0x3C, 64);
    void *const old = q;
    CHECK(realloc_s(&q, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    CHECK(q == old);                               /* still owned, not lost */
    CHECK(((unsigned char *)q)[0] == 0x3C);        /* contents intact */
    CHECK(((unsigned char *)q)[63] == 0x3C);

    /* growing realloc succeeds and keeps contents in the prefix */
    CHECK(realloc_s(&q, 256) == CPLUS_SAFE_OK);
    CHECK(q != NULL);
    CHECK(((unsigned char *)q)[0] == 0x3C);
    CHECK(((unsigned char *)q)[63] == 0x3C);
    free_s(&q);
    CHECK(q == NULL);

    /* realloc_s from a NULL handle behaves like a fresh allocation */
    void *r = NULL;
    CHECK(realloc_s(&r, 16) == CPLUS_SAFE_OK);
    CHECK(r != NULL);
    free_s(&r);
    CHECK(r == NULL);

    /* each error path leaves the handle defined (NULL), never stale */
    void *s = (void *)0xdeadbeef;
    CHECK(malloc_s(&s, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    CHECK(s == NULL);

    if (failures == 0) {
        printf("bugclass_uaf_handle: ok\n");
        return 0;
    }
    printf("bugclass_uaf_handle: FAILURES=%d\n", failures);
    return 1;
}
