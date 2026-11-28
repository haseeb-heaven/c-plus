#include <errno.h>
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

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    errno = 0; CHECK(malloc_s(SIZE_MAX) == NULL && errno == EINVAL);
    errno = 0; CHECK(calloc_s(SIZE_MAX, 1) == NULL && errno == EOVERFLOW);
    errno = 0; CHECK(calloc_s(RSIZE_MAX_MEM, 1) == NULL && errno == ENOMEM);
    errno = 0; CHECK(realloc_s(NULL, SIZE_MAX) == NULL && errno == EINVAL);

    void *p = malloc_s(0);
    CHECK(p != NULL);
    ((unsigned char *)p)[0] = 0x42;
    free_s(p);

    p = calloc_s(0, 0); CHECK(p != NULL); free_s(p);
    p = calloc_s(7, 0); CHECK(p != NULL); free_s(p);
    p = calloc_s(0, 7); CHECK(p != NULL); free_s(p);
    p = realloc_s(NULL, 0); CHECK(p != NULL); free_s(p);
    p = malloc_s(1); CHECK(p != NULL); free_s(p);

    if (failures == 0) { printf("test_alloc50_out_handle_null: ok\n"); return 0; }
    printf("test_alloc50_out_handle_null: FAILURES=%d\n", failures);
    return 1;
}
