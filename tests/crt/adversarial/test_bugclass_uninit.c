/*
 * Bug CLASS: use of uninitialised memory (raw C: malloc + read before write).
 *
 * The checked C+ API gives deterministic, defined initialisation: calloc_s
 * returns zero-initialised memory, and cplus_memzero_s / cplus_memset_s(...,0)
 * produce a fully zeroed range, so a subsequent read is defined (no indeterminate
 * values, no UB). Indeterminate reads of malloc_s memory are never performed here.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
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

static int all_zero(const unsigned char *b, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (b[i] != 0) return 0;
    return 1;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    void *p = NULL;

    /* calloc_s: deterministic zero-initialised bytes -> defined reads */
    CHECK(calloc_s(&p, 32, 1) == CPLUS_SAFE_OK);
    CHECK(p != NULL);
    CHECK(all_zero((unsigned char *)p, 32));
    free_s(&p);
    CHECK(p == NULL);

    CHECK(calloc_s(&p, 4, sizeof(int)) == CPLUS_SAFE_OK);
    CHECK(all_zero((unsigned char *)p, 4 * sizeof(int)));
    free_s(&p);

    /* calloc_s(0,0) is defined (1 zero byte), not an uninitialised pointer */
    CHECK(calloc_s(&p, 0, 0) == CPLUS_SAFE_OK);
    CHECK(p != NULL);
    CHECK(((unsigned char *)p)[0] == 0);
    free_s(&p);

    /* cplus_memzero_s turns fresh malloc memory into a defined zero buffer */
    CHECK(malloc_s(&p, 64) == CPLUS_SAFE_OK);
    CHECK(p != NULL);
    memset(p, 0xEE, 64); /* dirty it first */
    CHECK(cplus_memzero_s(p, 64) == 0);
    CHECK(all_zero((unsigned char *)p, 64));

    /* memset_s(...,0) is likewise deterministic */
    memset(p, 0xEE, 64);
    CHECK(cplus_memset_s(p, 64, 0, 64) == 0);
    CHECK(all_zero((unsigned char *)p, 64));

    /* a nonzero deterministic fill is also well defined and observable */
    CHECK(cplus_memset_s(p, 64, 0xAB, 64) == 0);
    for (int i = 0; i < 64; ++i) CHECK(((unsigned char *)p)[i] == 0xAB);
    free_s(&p);

    /* degenerate zero-length/zero-pointer calls are defined errors, not UB */
    unsigned char z[4];
    CHECK(cplus_memzero_s(z, 0) != 0);   /* ESZEROL */
    CHECK(cplus_memzero_s(NULL, 8) != 0); /* ESNULLP */

    if (failures == 0) {
        printf("bugclass_uninit: ok\n");
        return 0;
    }
    printf("bugclass_uninit: FAILURES=%d\n", failures);
    return 1;
}
