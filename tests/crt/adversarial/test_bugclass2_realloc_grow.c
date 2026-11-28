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

static int aligned_for_any(const void *p) {
    return ((uintptr_t)p % sizeof(void *)) == 0;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    void *p = malloc_s(16);
    CHECK(p != NULL && aligned_for_any(p));
    memset(p, 0x5A, 16);

    p = realloc_s(p, 4096);
    CHECK(p != NULL && aligned_for_any(p));
    for (int i = 0; i < 16; ++i) CHECK(((unsigned char *)p)[i] == 0x5A);
    memset(p, 0xC3, 4096);
    CHECK(((unsigned char *)p)[0] == 0xC3 && ((unsigned char *)p)[4095] == 0xC3);
    free_s(p);

    void *q = malloc_s(32);
    CHECK(q != NULL);
    memset(q, 0x77, 32);
    CHECK(realloc_s(q, (size_t)-1) == NULL);
    CHECK(cplus_safe_ptr_is_live(q) && ((unsigned char *)q)[0] == 0x77 && ((unsigned char *)q)[31] == 0x77);
    q = realloc_s(q, 8);
    CHECK(q != NULL && aligned_for_any(q));
    q = realloc_s(q, 2048);
    CHECK(q != NULL && aligned_for_any(q));
    CHECK(free_checked_s(q) == CPLUS_SAFE_OK);
    CHECK(free_checked_s(q) == CPLUS_SAFE_EINVAL);
    free_s(NULL);

    void *r = realloc_s(NULL, 64);
    CHECK(r != NULL && aligned_for_any(r));
    free_s(r);
    CHECK(realloc_s(NULL, (size_t)-1) == NULL);

    if (failures == 0) { printf("bugclass2_realloc_grow: ok\n"); return 0; }
    printf("bugclass2_realloc_grow: FAILURES=%d\n", failures);
    return 1;
}
