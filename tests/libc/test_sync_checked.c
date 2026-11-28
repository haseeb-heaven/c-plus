/* Standalone verification for the additive CHECKED sync wrappers in
 * libc/src/thread/sync_checked.c. C99 + -pthread, no <threads.h> on this host.
 *
 * Covers: valid init/destroy/join, NULL (and zeroed-handle) refusal,
 * double-destroy refusal without crashing, double-join refusal, rollback on
 * an underlying pthread failure, concurrent double-destroy (exactly one
 * winner), and oldest-first reclaim of completed DONE records so the bounded
 * side table cannot permanently refuse every later destroy. Output contract:
 *
 *   sync_checked: <checks> <failures>
 *
 * with a non-zero exit status iff failures > 0.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libc/install-include/cplus_threads.h"
#include "../../libc/install-include/cplus_sync_checked.h"

static int g_checks;
static int g_failures;

#define CHECK(cond, name)                                                     \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failures;                                                     \
            fprintf(stderr, "FAIL: %s\n", (name));                            \
        }                                                                     \
    } while (0)

#define CHECK_EQ(actual, expected, name)                                      \
    do {                                                                      \
        int a_ = (int)(actual);                                               \
        int e_ = (int)(expected);                                             \
        ++g_checks;                                                           \
        if (a_ != e_) {                                                       \
            ++g_failures;                                                     \
            fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", (name), a_, e_); \
        }                                                                     \
    } while (0)

static int worker(void *arg)
{
    (void)arg;
    return 42;
}

/* ---- concurrent double-destroy: exactly one caller may win -------------- */
typedef struct race_arg_s {
    cplus_mtx_t *mtx;
    pthread_mutex_t guard;
    int successes;
    int refusals;
    int others;
} race_arg;

static void *race_destroy(void *p)
{
    race_arg *a = (race_arg *)p;
    int rc = cplus_mtx_destroy_checked(a->mtx);
    (void)pthread_mutex_lock(&a->guard);
    if (rc == 0) ++a->successes;
    else if (rc == EINVAL) ++a->refusals;
    else ++a->others;
    (void)pthread_mutex_unlock(&a->guard);
    return NULL;
}

static void test_concurrent_destroy(void)
{
    enum { N = 4 };
    cplus_mtx_t m;
    race_arg a;
    pthread_t tids[N];
    int i;

    CHECK_EQ(cplus_mtx_init(&m, CPLUS_MTX_PLAIN), CPLUS_THRD_SUCCESS,
             "race mtx init");
    memset(&a, 0, sizeof a);
    a.mtx = &m;
    CHECK_EQ(pthread_mutex_init(&a.guard, NULL), 0, "race guard init");
    for (i = 0; i < N; ++i) {
        CHECK_EQ(pthread_create(&tids[i], NULL, race_destroy, &a), 0,
                 "race thread create");
    }
    for (i = 0; i < N; ++i) {
        CHECK_EQ(pthread_join(tids[i], NULL), 0, "race thread join");
    }
    CHECK_EQ(a.successes, 1, "concurrent destroy: exactly one success");
    CHECK_EQ(a.refusals, N - 1, "concurrent destroy: others refused EINVAL");
    CHECK_EQ(a.others, 0, "concurrent destroy: no unexpected error codes");
    CHECK_EQ(pthread_mutex_destroy(&a.guard), 0, "race guard destroy");
}

/* ---- completed DONE records are reclaimed, never a permanent ENOMEM ------ */
static void test_reclaim(void)
{
    enum { EXTRA = 8 };
    const size_t limit = (size_t)CPLUS_SYNC_CHECKED_CAPACITY * 2u + EXTRA;
    cplus_mtx_t **objs;
    size_t i;

    objs = (cplus_mtx_t **)calloc(limit, sizeof *objs);
    CHECK(objs != NULL, "reclaim object array alloc");
    if (objs == NULL) return;

    for (i = 0; i < limit; ++i) {
        objs[i] = (cplus_mtx_t *)calloc(1, sizeof *objs[i]);
        if (objs[i] == NULL) break;
        CHECK_EQ(cplus_mtx_init(objs[i], CPLUS_MTX_PLAIN), CPLUS_THRD_SUCCESS,
                 "reclaim mtx init");
        /* Crossing CPLUS_SYNC_CHECKED_CAPACITY used to brick the table with a
         * permanent ENOMEM. Completed records must be recycled instead. */
        CHECK_EQ(cplus_mtx_destroy_checked(objs[i]), 0,
                 "checked destroy accepted beyond table capacity");
    }
    CHECK_EQ(i, limit, "every distinct object was destroyed");

    for (i = 0; i < limit; ++i) {
        if (objs[i] != NULL) free(objs[i]);
    }
    free(objs);
}

int main(void)
{
    cplus_mtx_t m;
    cplus_mtx_t m2; /* distinct address: section (1) already recorded &m */
    cplus_cnd_t c;
    cplus_thrd_t t;
    cplus_thrd_t zero;
    int res = -1;

    /* (1) valid mutex init / checked destroy, then repeat refusal */
    CHECK_EQ(cplus_mtx_init(&m, CPLUS_MTX_PLAIN), CPLUS_THRD_SUCCESS,
             "mtx init");
    CHECK_EQ(cplus_mtx_destroy_checked(&m), 0, "checked mtx destroy");
    CHECK_EQ(cplus_mtx_destroy_checked(&m), EINVAL,
             "checked mtx double destroy refused (no crash)");

    /* (2) valid cond init / checked destroy, then repeat refusal */
    CHECK_EQ(cplus_cnd_init(&c), CPLUS_THRD_SUCCESS, "cnd init");
    CHECK_EQ(cplus_cnd_destroy_checked(&c), 0, "checked cnd destroy");
    CHECK_EQ(cplus_cnd_destroy_checked(&c), EINVAL,
             "checked cnd double destroy refused (no crash)");

    /* (3) NULL / invalid handles */
    CHECK_EQ(cplus_mtx_destroy_checked(NULL), EINVAL, "NULL mutex refused");
    CHECK_EQ(cplus_cnd_destroy_checked(NULL), EINVAL, "NULL cond refused");
    memset(&zero, 0, sizeof zero);
    CHECK_EQ(cplus_thrd_join_checked(zero, &res), EINVAL,
             "zeroed thread handle refused");

    /* (4) valid thread join, then repeat refusal */
    CHECK_EQ(cplus_thrd_create(&t, worker, NULL), CPLUS_THRD_SUCCESS,
             "thread create");
    CHECK_EQ(cplus_thrd_join_checked(t, &res), 0, "checked thread join");
    CHECK_EQ(res, 42, "join result propagated");
    CHECK_EQ(cplus_thrd_join_checked(t, &res), EINVAL,
             "checked double join refused (no crash)");

    /* (5) underlying failure rolls the reservation back so a retry works.
     * Some pthread implementations allow destroying a locked mutex; accept
     * either observation, but never unlock an object already destroyed. */
    {
        int rc;
        CHECK_EQ(cplus_mtx_init(&m2, CPLUS_MTX_PLAIN), CPLUS_THRD_SUCCESS,
                 "retry mtx init");
        CHECK_EQ(cplus_mtx_lock(&m2), CPLUS_THRD_SUCCESS, "retry mtx lock");
        rc = cplus_mtx_destroy_checked(&m2);
        CHECK(rc == 0 || rc == EBUSY, "locked destroy returns defined code");
        if (rc != 0) {
            CHECK_EQ(cplus_mtx_unlock(&m2), CPLUS_THRD_SUCCESS,
                     "retry mtx unlock");
            CHECK_EQ(cplus_mtx_destroy_checked(&m2), 0,
                     "retry after rolled-back failure succeeds");
        }
    }

    /* (6) concurrent double-destroy */
    test_concurrent_destroy();

    /* (7) completed records are reclaimed after the table fills */
    test_reclaim();

    printf("sync_checked: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
