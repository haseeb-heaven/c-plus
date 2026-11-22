/*
 * Bug CLASS: realloc/grow that loses the owner, its contents, or its alignment.
 *
 * Raw C: `p = realloc(p, big)` overwrites the only reference; on failure the
 * original block leaks and p dangles. The checked C+ handle API is
 * address-of-pointer based:
 *   - realloc_s(&p, bigger) grows and PRESERVES the original contents.
 *   - a FAILED realloc_s(&p, huge) returns CPLUS_SAFE_ENOMEM and leaves *p (and
 *     its contents) untouched.
 *   - free_s(&p) nulls the handle, so no dangling pointer survives.
 *   - the returned block stays suitably aligned for any object type.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
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

/* A block from the checked allocator must be aligned for any object type;
 * pointer alignment is a conservative, portable lower bound for that. */
static int aligned_for_any(const void *p) {
    return ((uintptr_t)p % sizeof(void *)) == 0;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* --- grow preserves contents and alignment --- */
    void *p = NULL;
    CHECK(malloc_s(&p, 16) == CPLUS_SAFE_OK);
    CHECK(p != NULL);
    CHECK(aligned_for_any(p));
    memset(p, 0x5A, 16);

    CHECK(realloc_s(&p, 4096) == CPLUS_SAFE_OK);
    CHECK(p != NULL);
    CHECK(aligned_for_any(p));
    {
        int intact = 1;
        int i;
        for (i = 0; i < 16; ++i)
            if (((unsigned char *)p)[i] != 0x5A) intact = 0;
        CHECK(intact);                          /* prefix preserved on grow   */
    }
    /* the whole grown region is usable now */
    memset(p, 0xC3, 4096);
    CHECK(((unsigned char *)p)[0] == 0xC3);
    CHECK(((unsigned char *)p)[4095] == 0xC3);

    free_s(&p);
    CHECK(p == NULL);                           /* free_s nulls the handle    */

    /* --- failing grow must PRESERVE the owner, its pointer and contents --- */
    void *q = NULL;
    CHECK(malloc_s(&q, 32) == CPLUS_SAFE_OK);
    CHECK(q != NULL);
    memset(q, 0x77, 32);
    {
        void *const qold = q;
        CHECK(realloc_s(&q, (size_t)-1) == CPLUS_SAFE_ENOMEM);
        CHECK(q == qold);                       /* not lost, not dangling     */
        CHECK(((unsigned char *)q)[0] == 0x77); /* contents intact            */
        CHECK(((unsigned char *)q)[31] == 0x77);
    }

    /* --- alignment survives a shrink then a grow --- */
    CHECK(realloc_s(&q, 8) == CPLUS_SAFE_OK);
    CHECK(aligned_for_any(q));
    CHECK(realloc_s(&q, 2048) == CPLUS_SAFE_OK);
    CHECK(aligned_for_any(q));

    free_s(&q);
    CHECK(q == NULL);
    free_s(&q);                                 /* double free is a safe no-op */
    CHECK(q == NULL);
    free_s(NULL);                               /* NULL out-param tolerated    */

    /* --- NULL out-param is a defined argument error --- */
    CHECK(realloc_s(NULL, 16) == CPLUS_SAFE_EINVAL);

    /* --- realloc_s from a NULL handle behaves like a fresh, aligned alloc --- */
    void *r = NULL;
    CHECK(realloc_s(&r, 64) == CPLUS_SAFE_OK);
    CHECK(r != NULL);
    CHECK(aligned_for_any(r));
    free_s(&r);
    CHECK(r == NULL);

    /* --- a failed grow on a NULL handle still leaves NULL, not a stale value - */
    void *s = NULL;
    CHECK(realloc_s(&s, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    CHECK(s == NULL);

    if (failures == 0) {
        printf("bugclass2_realloc_grow: ok\n");
        return 0;
    }
    printf("bugclass2_realloc_grow: FAILURES=%d\n", failures);
    return 1;
}
