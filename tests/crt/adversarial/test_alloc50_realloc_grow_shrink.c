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

static int prefix_is(const unsigned char *b, size_t n, unsigned char v) {
    for (size_t i = 0; i < n; ++i)
        if (b[i] != v) return 0;
    return 1;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    void *p = malloc_s(16);
    CHECK(p != NULL && ALIGNED(p));
    memset(p, 0x5A, 16);

    p = realloc_s(p, 4096);
    CHECK(p != NULL && ALIGNED(p) && prefix_is((const unsigned char *)p, 16, 0x5A));

    memset(p, 0xC3, 4096);
    CHECK(((unsigned char *)p)[0] == 0xC3 && ((unsigned char *)p)[4095] == 0xC3);

    p = realloc_s(p, 8);
    CHECK(p != NULL && ALIGNED(p) && prefix_is((const unsigned char *)p, 8, 0xC3));

    p = realloc_s(p, 1024);
    CHECK(p != NULL && ALIGNED(p) && prefix_is((const unsigned char *)p, 8, 0xC3));

    p = realloc_s(p, 1);
    CHECK(p != NULL && ALIGNED(p) && ((unsigned char *)p)[0] == 0xC3);
    free_s(p);

    {
        int ok = 1;
        size_t n = 1;
        p = malloc_s(1);
        ((unsigned char *)p)[0] = 0x9E;
        for (int i = 0; i < 100; ++i) {
            n += 7;
            void *np = realloc_s(p, n);
            if (!np || !ALIGNED(np) || ((unsigned char *)np)[0] != 0x9E) {
                if (np) free_s(np);
                else free_s(p);
                ok = 0;
                break;
            }
            p = np;
        }
        CHECK(ok);
        if (ok) free_s(p);
    }

    if (failures == 0) { printf("test_alloc50_realloc_grow_shrink: ok\n"); return 0; }
    printf("test_alloc50_realloc_grow_shrink: FAILURES=%d\n", failures);
    return 1;
}
