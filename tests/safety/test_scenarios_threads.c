/* Thread-safety & concurrency scenarios (S41-S45) for the 45-scenario matrix:
 * concurrent malloc_s/free_s, concurrent cplus_malloc/cplus_free, race-free
 * safe_stats, mutex/condvar lifecycle, and cross-thread ownership hand-off. */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "install-include/cplus_libc.h"
#include "safe_alloc.h"
#include "safe_log.h"
#include "safe_stats.h"

enum { NUM_THREADS = 4, ITERS = 250 };

typedef struct handoff_box {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    void *buf;
    unsigned long long gen;
    int ready;
    int consumed;
} handoff_box;

static void *alloc_worker(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < ITERS; ++i) {
        size_t sz = (size_t)((id + 1) * 16 + (i % 64));
        void *sp = malloc_s(sz);
        assert(sp != NULL && cplus_safe_ptr_size(sp) == sz);
        memset(sp, id & 0x7f, sz);
        assert(free_checked_s(sp) == CPLUS_SAFE_OK);

        void *lp = cplus_calloc(4, sizeof(int));
        assert(lp != NULL && cplus_alloc_length(lp) == 4);
        assert(cplus_free_checked(&lp) == 0 && lp == NULL);

        cplus_safe_stat_record("mt_worker", (i & 1) == 0);
    }
    return NULL;
}

static void *consumer_worker(void *arg) {
    handoff_box *box = (handoff_box *)arg;
    void *owned = NULL;
    pthread_mutex_lock(&box->mu);
    while (!box->ready)
        pthread_cond_wait(&box->cv, &box->mu);
    assert(cplus_safe_ptr_is_live(box->buf) == 1);
    assert(cplus_safe_ptr_check_generation(box->buf, box->gen) == 1);
    assert(cplus_safe_transfer_ownership(&owned, &box->buf) == CPLUS_SAFE_OK);
    assert(box->buf == NULL && owned != NULL);
    assert(cplus_safe_ptr_check_generation(owned, box->gen) == 0);
    box->consumed = 1;
    pthread_cond_signal(&box->cv);
    pthread_mutex_unlock(&box->mu);
    assert(strcmp((const char *)owned, "cross-thread-payload") == 0);
    free_s(owned);
    return NULL;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_stats_reset();

    size_t safe_base = cplus_safe_alloc_live_count();
    size_t libc_base = cplus_alloc_live_count();

    pthread_t th[NUM_THREADS];
    int ids[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; ++i) {
        ids[i] = i;
        assert(pthread_create(&th[i], NULL, alloc_worker, &ids[i]) == 0);
    }
    for (int i = 0; i < NUM_THREADS; ++i)
        assert(pthread_join(th[i], NULL) == 0);

    cplus_safe_stat st;
    assert(cplus_safe_stats_get("mt_worker", &st) == 1);
    assert(st.calls == (unsigned long)(NUM_THREADS * ITERS));
    assert(st.ok + st.fail == st.calls);

    /* Cross-thread ownership hand-off */
    handoff_box box;
    cplus_safe_alloc_meta meta;
    pthread_t consumer;
    pthread_mutex_init(&box.mu, NULL);
    pthread_cond_init(&box.cv, NULL);
    box.ready = 0;
    box.consumed = 0;
    box.buf = malloc_s(64);
    assert(box.buf != NULL);
    strcpy((char *)box.buf, "cross-thread-payload");
    assert(cplus_safe_alloc_get_meta(box.buf, &meta) == CPLUS_SAFE_OK);
    box.gen = meta.generation;

    assert(pthread_create(&consumer, NULL, consumer_worker, &box) == 0);
    pthread_mutex_lock(&box.mu);
    box.ready = 1;
    pthread_cond_signal(&box.cv);
    while (!box.consumed)
        pthread_cond_wait(&box.cv, &box.mu);
    pthread_mutex_unlock(&box.mu);
    assert(pthread_join(consumer, NULL) == 0);

    pthread_cond_destroy(&box.cv);
    pthread_mutex_destroy(&box.mu);

    assert(cplus_safe_alloc_live_count() == safe_base);
    assert(cplus_alloc_live_count() == libc_base);
    puts("test_scenarios_threads: ok");
    return 0;
}
