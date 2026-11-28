#include <errno.h>
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

    CHECK(calloc_s(SIZE_MAX, 2) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(SIZE_MAX / 4 + 1, 4) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(2, SIZE_MAX / 2 + 1) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(SIZE_MAX / 8 + 1, 8) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(SIZE_MAX, SIZE_MAX) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(3, SIZE_MAX / 3 + 1) == NULL && errno == EOVERFLOW);

    CHECK(calloc_s(RSIZE_MAX_MEM, 1) == NULL && errno == ENOMEM);
    CHECK(malloc_s(SIZE_MAX) == NULL && errno == EINVAL);
    CHECK(malloc_s(RSIZE_MAX_MEM) == NULL && errno == ENOMEM);
    CHECK(malloc_s(SIZE_MAX - 4096) == NULL && errno == EINVAL);

    void *p = calloc_s(1000, 4);
    CHECK(p != NULL);
    free_s(p);

    if (failures == 0) { printf("bugclass_int_overflow_alloc: ok\n"); return 0; }
    printf("bugclass_int_overflow_alloc: FAILURES=%d\n", failures);
    return 1;
}
