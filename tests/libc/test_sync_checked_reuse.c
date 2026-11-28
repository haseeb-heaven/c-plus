/* Regression for issue #195: a DONE marker must not survive a legitimate
 * re-initialisation of a mutex/condvar/thread at the same address.  Prints
 * "sync_reuse: <checks> <failures>" and exits non-zero on failure. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdio.h>

#include "../../libc/install-include/cplus_threads.h"
#include "../../libc/install-include/cplus_sync_checked.h"

static int g_checks;
static int g_failures;

#define CHECK_EQ(actual, expected, name)                                      \
    do {                                                                       \
        int a_ = (int)(actual);                                                \
        int e_ = (int)(expected);                                              \
        ++g_checks;                                                            \
        if (a_ != e_) {                                                        \
            ++g_failures;                                                      \
            fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", name, a_, e_); \
        }                                                                      \
    } while (0)

static int worker(void *arg)
{
    return (int)(long)arg;
}

static void test_stack_address_reuse(void)
{
    int i;
    for (i = 0; i < 32; ++i) {
        cplus_mtx_t m;
        cplus_cnd_t c;
        CHECK_EQ(cplus_mtx_init(&m, CPLUS_MTX_PLAIN), CPLUS_THRD_SUCCESS,
                 "reused mtx init");
        CHECK_EQ(cplus_mtx_destroy_checked(&m), 0, "reused mtx destroy");
        CHECK_EQ(cplus_cnd_init(&c), CPLUS_THRD_SUCCESS, "reused cnd init");
        CHECK_EQ(cplus_cnd_destroy_checked(&c), 0, "reused cnd destroy");
    }
}

static void test_thread_handle_reuse(void)
{
    int i;
    for (i = 0; i < 16; ++i) {
        cplus_thrd_t t;
        int res = -1;
        CHECK_EQ(cplus_thrd_create(&t, worker, (void *)(long)7),
                 CPLUS_THRD_SUCCESS, "reused thrd create");
        CHECK_EQ(cplus_thrd_join_checked(t, &res), 0, "reused thrd join");
        CHECK_EQ(res, 7, "reused thrd result");
    }
}

int main(void)
{
    test_stack_address_reuse();
    test_thread_handle_reuse();
    printf("sync_reuse: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
