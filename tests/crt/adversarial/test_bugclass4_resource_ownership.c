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

    {
        void *p = malloc_s(16);
        CHECK(p != NULL);
        memset(p, 0x5A, 16);
        p = realloc_s(p, 4096);
        CHECK(p != NULL);
        for (int i = 0; i < 16; ++i) CHECK(((unsigned char *)p)[i] == 0x5A);
        memset(p, 0xC3, 4096);
        p = realloc_s(p, 8);
        CHECK(p != NULL && ((unsigned char *)p)[0] == 0xC3 && ((unsigned char *)p)[7] == 0xC3);
        p = realloc_s(p, 1024);
        CHECK(p != NULL && ((unsigned char *)p)[0] == 0xC3);
        free_s(p);
        CHECK(!cplus_safe_ptr_is_live(p));
    }

    {
        void *p = malloc_s(32);
        CHECK(p != NULL);
        memset(p, 0x77, 32);
        CHECK(realloc_s(p, (size_t)-1) == NULL);
        CHECK(cplus_safe_ptr_is_live(p) && ((unsigned char *)p)[0] == 0x77 && ((unsigned char *)p)[31] == 0x77);
        free_s(p);
    }

    {
        void *p = malloc_s(64);
        CHECK(p != NULL);
        CHECK(free_checked_s(p) == CPLUS_SAFE_OK);
        CHECK(free_checked_s(p) == CPLUS_SAFE_EINVAL);
        free_s(NULL);
    }

    {
        void *p = realloc_s(NULL, 48);
        CHECK(p != NULL);
        free_s(p);
        p = malloc_s(0);
        CHECK(p != NULL);
        free_s(p);
    }

    if (failures == 0) { printf("bugclass4_resource_ownership: ok\n"); return 0; }
    printf("bugclass4_resource_ownership: FAILURES=%d\n", failures);
    return 1;
}
