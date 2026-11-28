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

#define ALIGNED(p) ((((uintptr_t)(p)) & (uintptr_t)7u) == (uintptr_t)0u)

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    void *a = malloc_s(64);
    void *b = malloc_s(64);
    CHECK(a != NULL && b != NULL && ALIGNED(a) && ALIGNED(b));
    memset(b, 0xBB, 64);
    memset(a, 0xAA, 64);
    for (size_t i = 0; i < 64; ++i) CHECK(((unsigned char *)b)[i] == 0xBB);
    free_s(a);
    CHECK(((unsigned char *)b)[0] == 0xBB);
    free_s(b);

    {
        void *p = malloc_s(96);
        CHECK(p != NULL);
        memset((unsigned char *)p, 0xC0, 8);
        memset((unsigned char *)p + 88, 0xC1, 8);
        memset((unsigned char *)p + 8, 0x7E, 80);
        p = realloc_s(p, 512);
        CHECK(p != NULL && ALIGNED(p) && ((unsigned char *)p)[0] == 0xC0 && ((unsigned char *)p)[7] == 0xC0);
        free_s(p);
    }

    for (int i = 0; i < 100; ++i) {
        size_t n = (size_t)i + 1;
        void *p = malloc_s(n);
        CHECK(p != NULL && ALIGNED(p));
        memset(p, (unsigned char)i, n);
        CHECK(((unsigned char *)p)[0] == (unsigned char)i && ((unsigned char *)p)[n - 1] == (unsigned char)i);
        free_s(p);
    }

    if (failures == 0) { printf("test_alloc50_loops_canaries: ok\n"); return 0; }
    printf("test_alloc50_loops_canaries: FAILURES=%d\n", failures);
    return 1;
}
