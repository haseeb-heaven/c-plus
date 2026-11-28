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

static int all_zero(const unsigned char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) if (b[i] != 0) return 0;
    return 1;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    void *p = calloc_s(4, 8); CHECK(p != NULL && all_zero((const unsigned char *)p, 32)); free_s(p);
    p = calloc_s(1, 1); CHECK(p != NULL && ((const unsigned char *)p)[0] == 0); free_s(p);
    p = calloc_s(256, 1); CHECK(p != NULL && all_zero((const unsigned char *)p, 256)); free_s(p);
    p = calloc_s(1, 256); CHECK(p != NULL && all_zero((const unsigned char *)p, 256)); free_s(p);
    p = calloc_s(16, 16); CHECK(p != NULL && all_zero((const unsigned char *)p, 256)); free_s(p);

    p = malloc_s(256); CHECK(p != NULL);
    for (size_t i = 0; i < 256; ++i) ((unsigned char *)p)[i] = 0xFF;
    free_s(p);

    p = calloc_s(8, 8); CHECK(p != NULL && all_zero((const unsigned char *)p, 64)); free_s(p);
    p = calloc_s(1, 0); CHECK(p != NULL && ((const unsigned char *)p)[0] == 0); free_s(p);
    p = calloc_s(0, 1); CHECK(p != NULL && ((const unsigned char *)p)[0] == 0); free_s(p);
    p = calloc_s(64, 4); CHECK(p != NULL && all_zero((const unsigned char *)p, 256)); free_s(p);
    p = malloc_s(16); CHECK(p != NULL); free_s(p);

    if (failures == 0) { printf("test_alloc50_calloc_zeroing: ok\n"); return 0; }
    printf("test_alloc50_calloc_zeroing: FAILURES=%d\n", failures);
    return 1;
}
