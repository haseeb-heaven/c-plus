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

    void *p = malloc_s(64);
    CHECK(p != NULL && cplus_safe_ptr_is_live(p));
    CHECK(free_checked_s(p) == CPLUS_SAFE_OK);
    CHECK(!cplus_safe_ptr_is_live(p));
    CHECK(free_checked_s(p) == CPLUS_SAFE_EINVAL); /* double free refused */
    free_s(p);                                     /* no crash */
    free_s(NULL);

    for (int i = 0; i < 100; ++i) {
        void *q = malloc_s((size_t)(i % 32) + 1);
        CHECK(q != NULL);
        ((unsigned char *)q)[0] = (unsigned char)i;
        CHECK(free_checked_s(q) == CPLUS_SAFE_OK);
        CHECK(free_checked_s(q) == CPLUS_SAFE_EINVAL);
    }

    p = calloc_s(32, 4);
    CHECK(p != NULL);
    CHECK(free_checked_s(p) == CPLUS_SAFE_OK);
    CHECK(free_checked_s(p) == CPLUS_SAFE_EINVAL);

    if (failures == 0) { printf("test_alloc50_free_idempotent: ok\n"); return 0; }
    printf("test_alloc50_free_idempotent: FAILURES=%d\n", failures);
    return 1;
}
