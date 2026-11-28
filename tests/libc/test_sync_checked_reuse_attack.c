/* Deterministic regressions for issue #195: CHECKED sync guards must retire
 * completed DONE records and re-arm on legitimate init/create at the same
 * identity. Both failures used to be permanent for the rest of the process:
 * either every destroy returned ENOMEM, or a freshly initialised object at a
 * recycled stack/heap address was refused with EINVAL and its OS handle leaked.
 *
 * Output: "sync_checked_reuse_attack: <checks> <failures>".
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libc/install-include/cplus_sync_checked.h"
#include "../../libc/install-include/cplus_threads.h"

static int g_checks;
static int g_failures;

#define CHECK_EQ(actual, expected, name)                                       \
    do {                                                                       \
        int a_ = (int)(actual);                                                \
        int e_ = (int)(expected);                                              \
        ++g_checks;                                                            \
        if (a_ != e_) {                                                        \
            ++g_failures;                                                      \
            fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", (name), a_, e_); \
        }                                                                      \
    } while (0)

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) { ++g_failures; fprintf(stderr, "FAIL: %s\n", (name)); }  \
    } while (0)

/* Same stack slot in the same helper frame on two successive calls. */
static int stack_mutex_round(void)
{
    cplus_mtx_t m;
    int rc;
    CHECK_EQ(cplus_mtx_init(&m, CPLUS_MTX_PLAIN), CPLUS_THRD_SUCCESS,
             "stack mtx init");
    rc = cplus_mtx_destroy_checked(&m);
    CHECK_EQ(rc, 0, "stack mtx checked destroy");
    /* The just-destroyed identity is still refused until it is re-initialised. */
    CHECK_EQ(cplus_mtx_destroy_checked(&m), EINVAL, "stack mtx double destroy");
    return rc;
}

static int stack_cond_round(void)
{
    cplus_cnd_t c;
    CHECK_EQ(cplus_cnd_init(&c), CPLUS_THRD_SUCCESS, "stack cnd init");
    CHECK_EQ(cplus_cnd_destroy_checked(&c), 0, "stack cnd checked destroy");
    return 0;
}

static int worker(void *arg)
{
    (void)arg;
    return 7;
}

static int thread_round(void)
{
    cplus_thrd_t t;
    int res = -1;
    CHECK_EQ(cplus_thrd_create(&t, worker, NULL), CPLUS_THRD_SUCCESS,
             "thread create");
    CHECK_EQ(cplus_thrd_join_checked(t, &res), 0, "checked thread join");
    CHECK_EQ(res, 7, "join result");
    return res;
}

static void test_same_address_reuse(void)
{
    enum { ROUNDS = 3 };
    cplus_mtx_t *m;
    cplus_cnd_t *c;
    void *first_m;
    void *first_c;
    int i;

    for (i = 0; i < ROUNDS; ++i) {
        CHECK_EQ(stack_mutex_round(), 0, "same stack mutex address reused");
        CHECK_EQ(stack_cond_round(), 0, "same stack condvar address reused");
        CHECK_EQ(thread_round(), 7, "thread handle reused");
    }

    /* The same heap storage is initialised and checked-destroyed repeatedly:
     * this is the address-reuse case without depending on host malloc's
     * free-list policy (which may or may not hand back the same address). */
    m = (cplus_mtx_t *)malloc(sizeof(cplus_mtx_t));
    c = (cplus_cnd_t *)malloc(sizeof(cplus_cnd_t));
    CHECK(m != NULL && c != NULL, "heap object alloc");
    if (m == NULL || c == NULL) { free(m); free(c); return; }
    first_m = m;
    first_c = c;
    for (i = 0; i < ROUNDS; ++i) {
        CHECK(m == first_m, "same mutex heap address each round");
        CHECK(c == first_c, "same condvar heap address each round");
        CHECK_EQ(cplus_mtx_init(m, CPLUS_MTX_PLAIN), CPLUS_THRD_SUCCESS,
                 "heap mtx init");
        CHECK_EQ(cplus_cnd_init(c), CPLUS_THRD_SUCCESS, "heap cnd init");
        CHECK_EQ(cplus_mtx_destroy_checked(m), 0, "heap mtx destroy checked");
        CHECK_EQ(cplus_cnd_destroy_checked(c), 0, "heap cnd destroy checked");
    }
    free(first_m);
    free(first_c);
}

/* Cross the table capacity twice with distinct live objects. No ENOMEM:
 * DONE records are reclaimed in completion order. */
static void test_no_permanent_enomem(void)
{
    enum { N = (int)CPLUS_SYNC_CHECKED_CAPACITY * 2 + 9 };
    cplus_mtx_t objs[N];
    int i;

    for (i = 0; i < N; ++i) {
        CHECK_EQ(cplus_mtx_init(&objs[i], CPLUS_MTX_PLAIN), CPLUS_THRD_SUCCESS,
                 "bulk mtx init");
        CHECK_EQ(cplus_mtx_destroy_checked(&objs[i]), 0,
                 "bulk checked destroy after capacity");
    }
}

int main(void)
{
    test_same_address_reuse();
    test_no_permanent_enomem();
    printf("sync_checked_reuse_attack: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
