/*
 * Bug CLASS: integer overflow in allocation size arithmetic.
 *
 * Raw C: calloc(count, size) / malloc(count * size) where the product wraps
 * (a classic heap-overflow root cause). The checked C+ allocators detect the
 * count*size overflow and return CPLUS_SAFE_EOVERFLOW; an enormous but
 * representable request fails cleanly with CPLUS_SAFE_ENOMEM. Neither leaves a
 * stale/undersized handle behind.
 */
#include <stdio.h>
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

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    void *p = NULL;

    /* --- count*size wraps -> EOVERFLOW, handle stays NULL --- */
    CHECK(calloc_s(&p, SIZE_MAX, 2) == CPLUS_SAFE_EOVERFLOW);
    CHECK(p == NULL);
    CHECK(calloc_s(&p, SIZE_MAX / 4 + 1, 4) == CPLUS_SAFE_EOVERFLOW);
    CHECK(p == NULL);
    CHECK(calloc_s(&p, 2, SIZE_MAX / 2 + 1) == CPLUS_SAFE_EOVERFLOW);
    CHECK(p == NULL);
    CHECK(calloc_s(&p, SIZE_MAX / 8 + 1, 8) == CPLUS_SAFE_EOVERFLOW);
    CHECK(p == NULL);
    CHECK(calloc_s(&p, SIZE_MAX, SIZE_MAX) == CPLUS_SAFE_EOVERFLOW);
    CHECK(p == NULL);
    CHECK(calloc_s(&p, 3, SIZE_MAX / 3 + 1) == CPLUS_SAFE_EOVERFLOW);
    CHECK(p == NULL);

    /* --- representable but impossible requests -> ENOMEM, handle NULL --- */
    CHECK(calloc_s(&p, SIZE_MAX, 1) == CPLUS_SAFE_ENOMEM);
    CHECK(p == NULL);
    CHECK(calloc_s(&p, 1, SIZE_MAX) == CPLUS_SAFE_ENOMEM);
    CHECK(p == NULL);
    CHECK(malloc_s(&p, SIZE_MAX) == CPLUS_SAFE_ENOMEM);
    CHECK(p == NULL);
    CHECK(malloc_s(&p, SIZE_MAX / 2) == CPLUS_SAFE_ENOMEM);
    CHECK(p == NULL);
    CHECK(malloc_s(&p, SIZE_MAX - 4096) == CPLUS_SAFE_ENOMEM);
    CHECK(p == NULL);

    /* --- NULL out-param is a defined argument error --- */
    CHECK(calloc_s(NULL, 1, 1) == CPLUS_SAFE_EINVAL);
    CHECK(malloc_s(NULL, 8) == CPLUS_SAFE_EINVAL);

    /* --- sanity: a representable, satisfiable request still succeeds --- */
    CHECK(calloc_s(&p, 1000, 4) == CPLUS_SAFE_OK);
    CHECK(p != NULL);
    free_s(&p);
    CHECK(p == NULL);

    if (failures == 0) {
        printf("bugclass_int_overflow_alloc: ok\n");
        return 0;
    }
    printf("bugclass_int_overflow_alloc: FAILURES=%d\n", failures);
    return 1;
}
