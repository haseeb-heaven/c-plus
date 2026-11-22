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

    void *p = malloc_s(0);
    CHECK(p != NULL);
    ((unsigned char *)p)[0] = 0xA1;
    p = realloc_s(p, 0);
    CHECK(p != NULL && ((unsigned char *)p)[0] == 0xA1);
    p = realloc_s(p, 0);
    CHECK(p != NULL);
    free_s(p);

    p = calloc_s(0, 0); CHECK(p != NULL && ((unsigned char *)p)[0] == 0); free_s(p);
    p = calloc_s(0, 64); CHECK(p != NULL && ((unsigned char *)p)[0] == 0); free_s(p);
    p = calloc_s(64, 0); CHECK(p != NULL && ((unsigned char *)p)[0] == 0); free_s(p);
    p = calloc_s(0, SIZE_MAX); CHECK(p != NULL); free_s(p);
    p = calloc_s(SIZE_MAX, 0); CHECK(p != NULL); free_s(p);
    p = realloc_s(NULL, 0); CHECK(p != NULL); ((unsigned char *)p)[0] = 0xB2; free_s(p);
    p = malloc_s(8); CHECK(p != NULL); free_s(p);

    if (failures == 0) { printf("test_alloc50_size0_defined: ok\n"); return 0; }
    printf("test_alloc50_size0_defined: FAILURES=%d\n", failures);
    return 1;
}
