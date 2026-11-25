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

    void *p = malloc_s(32);
    CHECK(p != NULL);
    memset(p, 0x77, 32);
    CHECK(realloc_s(p, SIZE_MAX) == NULL);
    CHECK(cplus_safe_ptr_is_live(p) && ((unsigned char *)p)[0] == 0x77 && ((unsigned char *)p)[31] == 0x77);
    free_s(p);

    p = malloc_s(128);
    CHECK(p != NULL);
    memset(p, 0xAB, 128);
    for (int i = 0; i < 20; ++i) {
        CHECK(realloc_s(p, SIZE_MAX - (size_t)i) == NULL);
        CHECK(cplus_safe_ptr_is_live(p) && ((unsigned char *)p)[0] == 0xAB && ((unsigned char *)p)[127] == 0xAB);
    }
    free_s(p);

    p = realloc_s(NULL, 48);
    CHECK(p != NULL);
    ((unsigned char *)p)[0] = 0x11;
    ((unsigned char *)p)[47] = 0x11;
    p = realloc_s(p, 64);
    CHECK(p != NULL && ((unsigned char *)p)[0] == 0x11 && ((unsigned char *)p)[47] == 0x11);
    free_s(p);

    p = malloc_s(16);
    CHECK(p != NULL);
    ((unsigned char *)p)[0] = 0x22;
    p = realloc_s(p, 0);
    CHECK(p != NULL && ((unsigned char *)p)[0] == 0x22);
    free_s(p);

    p = realloc_s(NULL, 0);
    CHECK(p != NULL);
    free_s(p);

    if (failures == 0) { printf("test_alloc50_realloc_failure_preserve: ok\n"); return 0; }
    printf("test_alloc50_realloc_failure_preserve: FAILURES=%d\n", failures);
    return 1;
}
