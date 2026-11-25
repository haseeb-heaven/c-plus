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

    size_t base_live = cplus_safe_alloc_live_count();
    void *p = NULL;

    {
        int ok = 1;
        for (int i = 0; i < 100; ++i) {
            p = malloc_s((size_t)i + 1);
            if (!p) { ok = 0; break; }
            free_s(p);
            if (cplus_safe_ptr_is_live(p)) { ok = 0; break; }
        }
        CHECK(ok);
    }

    {
        int ok = 1;
        for (int i = 0; i < 100; ++i) {
            p = calloc_s((size_t)(i % 16) + 1, 4);
            if (!p || ((unsigned char *)p)[0] != 0) { ok = 0; break; }
            free_s(p);
            if (cplus_safe_ptr_is_live(p)) { ok = 0; break; }
        }
        CHECK(ok);
    }

    {
        enum { N = 100 };
        void *blk[N];
        int ok = 1;
        for (int i = 0; i < N; ++i) {
            blk[i] = malloc_s((size_t)(i + 1) * 8);
            if (!blk[i] || !ALIGNED(blk[i])) { ok = 0; break; }
            memset(blk[i], 0xD0 + (unsigned char)(i & 0x0F), (size_t)(i + 1) * 8);
        }
        CHECK(ok);
        for (int i = 0; i < N; ++i) {
            size_t n = (size_t)(i + 1) * 8;
            unsigned char v = (unsigned char)(0xD0 + (i & 0x0F));
            if (((unsigned char *)blk[i])[0] != v || ((unsigned char *)blk[i])[n - 1] != v) ok = 0;
            free_s(blk[i]);
        }
        CHECK(ok);
    }

    {
        void *a = malloc_s(32);
        void *b = malloc_s(32);
        CHECK(a != NULL && b != NULL && a != b);
        free_s(a);
        CHECK(!cplus_safe_ptr_is_live(a) && cplus_safe_ptr_is_live(b));
        free_s(b);
    }

    {
        enum { M = 64 };
        void *c[M];
        int ok = 1;
        for (int i = 0; i < M; ++i) {
            c[i] = calloc_s((size_t)i + 1, 2);
            if (!c[i] || ((unsigned char *)c[i])[0] != 0) { ok = 0; break; }
        }
        CHECK(ok);
        for (int i = 0; i < M; ++i) free_s(c[i]);
    }

    {
        int ok = 1;
        for (int i = 0; i < 50; ++i) {
            void *q = malloc_s(16);
            if (!q) { ok = 0; break; }
            ((unsigned char *)q)[0] = 0x5E;
            void *nq = realloc_s(q, 4096);
            if (!nq || ((unsigned char *)nq)[0] != 0x5E) { free_s(nq ? nq : q); ok = 0; break; }
            q = nq;
            free_s(q);
        }
        CHECK(ok);
    }

    CHECK(cplus_safe_alloc_live_count() == base_live);

    if (failures == 0) { printf("test_alloc50_alternating_leakfree: ok\n"); return 0; }
    printf("test_alloc50_alternating_leakfree: FAILURES=%d\n", failures);
    return 1;
}
