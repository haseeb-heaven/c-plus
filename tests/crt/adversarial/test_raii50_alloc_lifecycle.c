/*
 * test_raii50_alloc_lifecycle.c
 *
 * RAII / cleanup discipline over the checked allocator.
 *
 * A resource acquired with malloc_s() is ALWAYS released with free_s() on every
 * exit path: the straight success path, a simulated early-return (goto cleanup),
 * and an error path where a second acquisition fails. The ownership handle is
 * NULL after release, and a live-allocation ledger (g_live) proves nothing was
 * leaked. free_s() tolerates NULL and a double free. A deterministic LCG drives
 * 2400 acquire -> work -> release cycles.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

/* deterministic linear congruential generator */
static unsigned long g_lcg;
static void lcg_seed(unsigned long s) { g_lcg = s ? s : 1UL; }
static unsigned long lcg_next(void) {
    g_lcg = g_lcg * 6364136223846793005UL + 1442695040888963407UL;
    return g_lcg;
}
static unsigned lcg_below(unsigned n) {
    return (unsigned)((lcg_next() >> 33) % (unsigned long)n);
}

/* live-allocation ledger: incremented on a successful acquire, decremented on
 * release. A non-zero value at a checkpoint is a leak. */
static long g_live = 0;
static int acquire(void **h, size_t n) {
    void *p = malloc_s(n);
    *h = p;
    if (p != NULL) { g_live++; return CPLUS_SAFE_OK; }
    return CPLUS_SAFE_ENOMEM;
}
static void release(void **h) {
    if (*h) { free_s(*h); *h = NULL; g_live--; }
}

static void fill(void *p, size_t n, unsigned char v) { memset(p, v, n); }
static int all_eq(const unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i) if (p[i] != v) return 0;
    return 1;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    lcg_seed(0xA110CA7EUL);

    /* --- success path: acquire, work, release ----------------------------- */
    {
        void *h = NULL;
        CHECK(acquire(&h, 32) == CPLUS_SAFE_OK);
        CHECK(h != NULL);
        CHECK(g_live == 1);
        fill(h, 32, 0xA5);
        CHECK(all_eq((unsigned char *)h, 32, 0xA5));
        release(&h);
        CHECK(h == NULL);          /* handle NULL after release */
        CHECK(g_live == 0);        /* no leak                   */
    }

    /* --- simulated early-return: the cleanup label always runs ------------ */
    {
        void *h = NULL;
        volatile int bail = 1;     /* runtime-unknown so the path is reached */
        CHECK(acquire(&h, 16) == CPLUS_SAFE_OK);
        CHECK(h != NULL);
        if (bail) goto early_out;
        fill(h, 16, 0x11);         /* skipped work */
    early_out:
        release(&h);
        CHECK(h == NULL);
        CHECK(g_live == 0);
    }

    /* --- error path: second acquire fails, first is still released -------- */
    {
        void *a = NULL;
        void *b = NULL;
        CHECK(acquire(&a, 24) == CPLUS_SAFE_OK);
        CHECK(a != NULL);
        CHECK(g_live == 1);
        b = malloc_s((size_t)-1);
        CHECK(b == NULL);          /* failed alloc returns NULL */
        CHECK(g_live == 1);        /* ledger untouched by the failure */
        release(&a);
        CHECK(a == NULL);
        CHECK(g_live == 0);
    }

    /* --- zero-size allocation is defined and releasable ------------------- */
    {
        void *h = NULL;
        CHECK(acquire(&h, 0) == CPLUS_SAFE_OK);
        CHECK(h != NULL);
        release(&h);
        CHECK(h == NULL);
        CHECK(g_live == 0);
    }

    /* --- free_s tolerates NULL and a double free -------------------------- */
    {
        void *h = NULL;
        free_s(NULL);              /* NULL: defined no-op */
        CHECK(g_live == 0);
        CHECK(acquire(&h, 8) == CPLUS_SAFE_OK);
        CHECK(h != NULL);
        CHECK(free_checked_s(h) == CPLUS_SAFE_OK);
        g_live--;
        CHECK(free_checked_s(h) == CPLUS_SAFE_EINVAL); /* double free refused */
        h = NULL;
        CHECK(g_live == 0);
    }

    /* --- argument validation --------------------------------------------- */
    CHECK(malloc_s((size_t)-1) == NULL);

    /* --- randomized acquire -> work -> release, 2400 cycles --------------- */
    {
        unsigned iter;
        for (iter = 0; iter < 2400; ++iter) {
            void *h = NULL;
            size_t n = (size_t)1 + lcg_below(257);
            unsigned char v = (unsigned char)(1 + lcg_below(255));

            CHECK(acquire(&h, n) == CPLUS_SAFE_OK);
            CHECK(h != NULL);
            CHECK(g_live == 1);
            fill(h, n, v);
            CHECK(((unsigned char *)h)[0] == v);
            CHECK(((unsigned char *)h)[n - 1] == v);
            release(&h);
            CHECK(h == NULL);
            CHECK(g_live == 0);
        }
    }

    CHECK(g_live == 0);
    if (failures == 0) {
        fprintf(stderr, "test_raii50_alloc_lifecycle: %lu assertions\n", checks);
        printf("test_raii50_alloc_lifecycle: ok\n");
        return 0;
    }
    printf("test_raii50_alloc_lifecycle: FAILURES=%d\n", failures);
    return 1;
}
