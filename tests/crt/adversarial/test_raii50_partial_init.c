/*
 * test_raii50_partial_init.c
 *
 * Partial-initialization cleanup. A composite acquisition grabs several
 * heap blocks; if any one of them fails, every block acquired BEFORE the
 * failure is released (unwound in reverse order) so nothing leaks and no
 * half-built object is left behind. The failure is a genuine malloc_s()
 * OOM (SIZE_MAX), not a simulated branch. A live-allocation ledger must read
 * zero after every scenario, and every handle must be NULL. A deterministic
 * LCG runs 2400 randomized partial-failure scenarios.
 */
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
static int acq(void **h, size_t n) {
    void *p = malloc_s(n);
    *h = p;
    if (p != NULL) { g_live++; return CPLUS_SAFE_OK; }
    return CPLUS_SAFE_ENOMEM;
}
static void rel(void **h) {
    if (*h) { free_s(*h); *h = NULL; g_live--; }
}

#define MAXR 8

/* Acquire up to MAXR blocks; force a real OOM at slot `fail_at` (== MAXR means
 * no failure). On failure, unwind every block acquired so far. */
static void scenario(unsigned fail_at) {
    void *h[MAXR];
    size_t i;
    int complete = 1;

    for (i = 0; i < MAXR; ++i) h[i] = NULL;

    for (i = 0; i < MAXR; ++i) {
        int rc;
        if ((unsigned)i == fail_at) {
            h[i] = malloc_s((size_t)-1);
            rc = h[i] ? CPLUS_SAFE_OK : CPLUS_SAFE_ENOMEM;
        } else {
            rc = acq(&h[i], 8 + i);
        }
        if (rc != CPLUS_SAFE_OK) {
            size_t j = i;
            CHECK(h[i] == NULL);                /* failed slot left NULL */
            while (j > 0) { --j; rel(&h[j]); }  /* unwind in reverse    */
            complete = 0;
            break;
        }
        CHECK(h[i] != NULL);
    }

    if (complete) {
        for (i = 0; i < MAXR; ++i) rel(&h[i]);
    }
    for (i = 0; i < MAXR; ++i) CHECK(h[i] == NULL);
    CHECK(g_live == 0);                         /* no leak on any path   */
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    lcg_seed(0x0BADF00DUL);

    /* --- classic two-resource partial init: allocate A, fail B, free A ---- */
    {
        void *a = NULL;
        void *b = NULL;
        CHECK(acq(&a, 64) == CPLUS_SAFE_OK);
        CHECK(a != NULL);
        CHECK(g_live == 1);
        memset(a, 0x3C, 64);
        b = malloc_s((size_t)-1);
        CHECK(b == NULL);  /* B fails  */
        CHECK(b == NULL);
        CHECK(g_live == 1);                    /* A still owned, B never got */
        rel(&a);                               /* free A on the failure path */
        CHECK(a == NULL);
        CHECK(b == NULL);
        CHECK(g_live == 0);                    /* nothing leaked             */
    }

    /* --- three-resource partial init with a mid-chain failure ------------- */
    {
        void *r[3];
        size_t i;
        for (i = 0; i < 3; ++i) r[i] = NULL;
        CHECK(acq(&r[0], 16) == CPLUS_SAFE_OK);
        CHECK(acq(&r[1], 16) == CPLUS_SAFE_OK);
        CHECK(g_live == 2);
        r[2] = malloc_s((size_t)-1);
        CHECK(r[2] == NULL);
        CHECK(r[2] == NULL);
        rel(&r[1]);
        rel(&r[0]);
        for (i = 0; i < 3; ++i) CHECK(r[i] == NULL);
        CHECK(g_live == 0);
    }

    /* --- exhaustive forced-failure positions, including no failure -------- */
    {
        unsigned f;
        for (f = 0; f <= MAXR; ++f) scenario(f);
    }
    CHECK(g_live == 0);

    /* --- randomized partial-failure scenarios, 2400 rounds ---------------- */
    {
        unsigned iter;
        for (iter = 0; iter < 2400; ++iter) {
            scenario(lcg_below(MAXR + 1));
            CHECK(g_live == 0);
        }
    }

    /* --- a failed acquisition never mutates a previously held handle ----- */
    {
        void *keep = NULL;
        void *fresh = NULL;
        CHECK(acq(&keep, 32) == CPLUS_SAFE_OK);
        memset(keep, 0x9E, 32);
        fresh = malloc_s((size_t)-1);
        CHECK(fresh == NULL);
        CHECK(fresh == NULL);
        CHECK(((unsigned char *)keep)[0] == 0x9E);   /* untouched */
        CHECK(((unsigned char *)keep)[31] == 0x9E);
        rel(&keep);
        CHECK(keep == NULL);
        CHECK(g_live == 0);
    }

    if (failures == 0) {
        fprintf(stderr, "test_raii50_partial_init: %lu assertions\n", checks);
        printf("test_raii50_partial_init: ok\n");
        return 0;
    }
    printf("test_raii50_partial_init: FAILURES=%d\n", failures);
    return 1;
}
