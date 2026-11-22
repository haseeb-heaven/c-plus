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

    CHECK(calloc_s(SIZE_MAX, 1) == NULL);
    CHECK(calloc_s(1, SIZE_MAX) == NULL);
    CHECK(calloc_s(RSIZE_MAX_MEM, 1) == NULL);
    CHECK(calloc_s(SIZE_MAX / 4, 4) == NULL);
    CHECK(calloc_s(SIZE_MAX / 16, 16) == NULL);

    errno = 0; CHECK(malloc_s(SIZE_MAX) == NULL && errno == EINVAL);
    errno = 0; CHECK(malloc_s(SIZE_MAX - 1) == NULL && errno == EINVAL);
    errno = 0; CHECK(malloc_s(RSIZE_MAX_MEM) == NULL && errno == ENOMEM);
    CHECK(malloc_s((size_t)1 << 60) == NULL);

    CHECK(realloc_s(NULL, SIZE_MAX) == NULL);
    CHECK(realloc_s(NULL, RSIZE_MAX_MEM) == NULL);

    void *p = malloc_s(64);
    CHECK(p != NULL);
    CHECK(realloc_s(p, SIZE_MAX) == NULL);
    CHECK(cplus_safe_ptr_is_live(p));
    free_s(p);

    p = malloc_s(1u << 20);
    CHECK(p != NULL);
    free_s(p);

    if (failures == 0) { printf("test_alloc50_huge_enomem: ok\n"); return 0; }
    printf("test_alloc50_huge_enomem: FAILURES=%d\n", failures);
    return 1;
}
