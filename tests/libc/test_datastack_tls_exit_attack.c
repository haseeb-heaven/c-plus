/* Deterministic regression for issue #209: thread-local DataStack backing
 * memory must be released when a worker thread exits. Each worker allocates a
 * scratch block in its TLS descriptor and exits; the pthread-key destructor
 * armed by the C+ thread wrappers reclaims it. The diagnostic counter makes
 * the reclamation observable after pthread_join.
 *
 * Output: "datastack_tls_exit_attack: <checks> <failures>".
 */
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../libc/src/stdlib/datastack_tls_internal.h"

/*Registration is armed by the hardened C+ creation wrapper.*/
int cplus_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int cplus_pthread_join(pthread_t, void **);

static int g_checks;
static int g_failures;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) { ++g_failures; fprintf(stderr, "FAIL: %s\n", (name)); }  \
    } while (0)

static void *heap_worker(void *arg)
{
    size_t n = (size_t)(uintptr_t)arg;
    (void)arg;
    (void)cplus_datastack_tls_thread_register(); /* also idempotent */
    stdlib_tls_datastack.base = (unsigned char *)malloc(n);
    if (stdlib_tls_datastack.base == NULL) return (void *)1;
    stdlib_tls_datastack.top = stdlib_tls_datastack.base;
    stdlib_tls_datastack.capacity = n;
    stdlib_tls_datastack.committed = n;
    stdlib_tls_datastack.backing = 0;
    memset(stdlib_tls_datastack.base, 0x5A, n);
    return NULL;
}

static void *idle_worker(void *arg)
{
    (void)arg;
    return NULL;
}

int main(void)
{
    enum { WORKERS = 8 };
    pthread_t tids[WORKERS];
    unsigned long before, after;
    void *rv;
    int i;

    before = cplus_datastack_tls_reclaim_count();
    for (i = 0; i < WORKERS; ++i) {
        if (cplus_pthread_create(&tids[i], NULL, heap_worker,
                                 (void *)(uintptr_t)65536u) != 0) {
            ++g_checks; ++g_failures;
            fprintf(stderr, "FAIL: worker create %d\n", i);
            break;
        }
    }
    for (i = 0; i < WORKERS; ++i) {
        CHECK(cplus_pthread_join(tids[i], &rv) == 0, "worker join");
        CHECK(rv == NULL, "worker allocated its scratch block");
    }
    after = cplus_datastack_tls_reclaim_count();
    CHECK(after == before + (unsigned long)WORKERS,
          "one destructor reclamation per exiting worker");

    /* A worker with no scratch allocation must not trigger a reclaim. */
    before = after;
    CHECK(cplus_pthread_create(&tids[0], NULL, idle_worker, NULL) == 0,
          "idle worker create");
    CHECK(cplus_pthread_join(tids[0], NULL) == 0, "idle worker join");
    CHECK(cplus_datastack_tls_reclaim_count() == before,
          "no reclaim for unused TLS descriptor");

    printf("datastack_tls_exit_attack: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
