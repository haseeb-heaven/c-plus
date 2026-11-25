/* Thread-local exception contexts: each thread has its own e4c_context. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "safe_except.h"
#include "safe_log.h"

static const struct e4c_exception_type WORKER_ERR = {NULL, "worker error"};

static void *worker(void *arg) {
    int id = *(int *)arg;
    cplus_except_init();   /* per-thread context supplier (idempotent) */
    volatile int caught = 0;
    for (int i = 0; i < 20; ++i) {
        TRY { THROW(WORKER_ERR, "thread %d iter %d", id, i); }
        CATCH (WORKER_ERR) { caught++; }
    }
    assert(caught == 20);
    return (void *)(long)caught;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    enum { N = 4 };
    pthread_t t[N];
    int ids[N];
    for (int i = 0; i < N; ++i) { ids[i] = i; pthread_create(&t[i], NULL, worker, &ids[i]); }
    for (int i = 0; i < N; ++i) { void *r = NULL; pthread_join(t[i], &r); assert((long)r == 20); }
    printf("except_threads: ok\n");
    return 0;
}
