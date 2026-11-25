#include <stdio.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"

static int failures = 0;
static unsigned long checks = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

static unsigned long g_lcg;
static void lcg_seed(unsigned long s) { g_lcg = s ? s : 1UL; }
static unsigned long lcg_next(void) {
    g_lcg = g_lcg * 6364136223846793005UL + 1442695040888963407UL;
    return g_lcg;
}
static unsigned lcg_below(unsigned n) {
    return (unsigned)((lcg_next() >> 33) % (unsigned long)n);
}

static long g_live = 0;
static int all_eq(const unsigned char *p, size_t n, unsigned char v) {
    for (size_t i = 0; i < n; ++i) if (p[i] != v) return 0;
    return 1;
}
static void *acquire_realloc(void *p, size_t n) {
    void *np = realloc_s(p, n);
    if (np != NULL && p == NULL) g_live++;
    return np;
}
static void release(void **p) {
    if (*p) { free_s(*p); *p = NULL; g_live--; }
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    lcg_seed(0x5A1C0DEUL);

    {
        void *p = malloc_s(16);
        CHECK(p != NULL);
        g_live++;
        memset(p, 0x5A, 16);
        p = realloc_s(p, 64); CHECK(p != NULL && all_eq((unsigned char *)p, 16, 0x5A));
        memset(p, 0xC3, 64);
        p = realloc_s(p, 4096); CHECK(p != NULL && all_eq((unsigned char *)p, 64, 0xC3));
        memset(p, 0xC3, 4096); CHECK(((unsigned char *)p)[4095] == 0xC3);
        p = realloc_s(p, 8); CHECK(p != NULL && all_eq((unsigned char *)p, 8, 0xC3));
        release(&p);
        CHECK(p == NULL && g_live == 0);
    }

    {
        void *p = acquire_realloc(NULL, 48);
        CHECK(p != NULL && g_live == 1);
        memset(p, 0x77, 48);
        p = realloc_s(p, 96);
        CHECK(p != NULL && all_eq((unsigned char *)p, 48, 0x77));
        release(&p);
        CHECK(p == NULL && g_live == 0);
    }

    {
        void *p = malloc_s(32);
        CHECK(p != NULL);
        g_live++;
        memset(p, 0x6E, 32);
        CHECK(realloc_s(p, (size_t)-1) == NULL);
        CHECK(cplus_safe_ptr_is_live(p) && g_live == 1 && all_eq((unsigned char *)p, 32, 0x6E));
        release(&p);
        CHECK(p == NULL && g_live == 0);
    }

    {
        void *p = malloc_s(16); CHECK(p != NULL); g_live++;
        release(&p); CHECK(p == NULL && g_live == 0);
        p = malloc_s(128); CHECK(p != NULL); g_live++;
        memset(p, 0x44, 128); CHECK(((unsigned char *)p)[127] == 0x44);
        release(&p); CHECK(p == NULL && g_live == 0);
    }

    for (unsigned iter = 0; iter < 2400; ++iter) {
        size_t n0 = (size_t)1 + lcg_below(1024);
        size_t cap = n0;
        unsigned char v = (unsigned char)(1 + lcg_below(255));
        unsigned steps = lcg_below(8);
        void *p = acquire_realloc(NULL, n0);
        CHECK(p != NULL);
        memset(p, v, cap);
        for (unsigned s = 0; s < steps; ++s) {
            if (lcg_below(8) == 0) {
                CHECK(realloc_s(p, (size_t)-1) == NULL);
                CHECK(cplus_safe_ptr_is_live(p) && all_eq((unsigned char *)p, cap, v));
            } else {
                size_t nn = (size_t)1 + lcg_below(4096);
                void *np = realloc_s(p, nn);
                CHECK(np != NULL);
                p = np;
                CHECK(all_eq((unsigned char *)p, cap < nn ? cap : nn, v));
                memset(p, v, nn);
                cap = nn;
            }
        }
        release(&p);
        CHECK(p == NULL && g_live == 0);
    }

    CHECK(g_live == 0);
    if (failures == 0) {
        fprintf(stderr, "test_raii50_realloc_chains: %lu assertions\n", checks);
        printf("test_raii50_realloc_chains: ok\n");
        return 0;
    }
    printf("test_raii50_realloc_chains: FAILURES=%d\n", failures);
    return 1;
}
