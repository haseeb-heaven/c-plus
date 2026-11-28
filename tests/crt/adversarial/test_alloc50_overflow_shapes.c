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
    CHECK(calloc_s(2, SIZE_MAX) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(SIZE_MAX, SIZE_MAX) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(SIZE_MAX / 2 + 1, 2) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(SIZE_MAX / 4 + 1, 4) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s(SIZE_MAX / 16 + 1, 16) == NULL && errno == EOVERFLOW);
    CHECK(calloc_s((size_t)1 << 40, (size_t)1 << 40) == NULL && errno == EOVERFLOW);

    void *p = calloc_s(1000, 4);
    CHECK(p != NULL);
    free_s(p);

    if (failures == 0) { printf("test_alloc50_overflow_shapes: ok\n"); return 0; }
    printf("test_alloc50_overflow_shapes: FAILURES=%d\n", failures);
    return 1;
}
