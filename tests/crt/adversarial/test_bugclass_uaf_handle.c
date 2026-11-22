#include <errno.h>
#include <stdio.h>
#include <string.h>
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
    CHECK(p != NULL && cplus_safe_ptr_is_live(p));
    memset(p, 0xAB, 32);
    CHECK(free_checked_s(p) == CPLUS_SAFE_OK);
    CHECK(!cplus_safe_ptr_is_live(p));
    CHECK(free_checked_s(p) == CPLUS_SAFE_EINVAL);
    free_s(p);
    free_s(NULL);

    void *q = malloc_s(64);
    CHECK(q != NULL);
    memset(q, 0x3C, 64);
    CHECK(realloc_s(q, (size_t)-1) == NULL);
    CHECK(cplus_safe_ptr_is_live(q) && ((unsigned char *)q)[0] == 0x3C && ((unsigned char *)q)[63] == 0x3C);

    q = realloc_s(q, 256);
    CHECK(q != NULL && ((unsigned char *)q)[0] == 0x3C && ((unsigned char *)q)[63] == 0x3C);
    free_s(q);

    void *r = realloc_s(NULL, 16);
    CHECK(r != NULL);
    free_s(r);

    CHECK(malloc_s((size_t)-1) == NULL);

    if (failures == 0) { printf("bugclass_uaf_handle: ok\n"); return 0; }
    printf("bugclass_uaf_handle: FAILURES=%d\n", failures);
    return 1;
}
