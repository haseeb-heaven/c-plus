#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

/* The host lacks C11 <threads.h>, so the wrappers are pthreads-backed. */
int cplus_pthread_create(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*);
int cplus_pthread_join(pthread_t,void**);
int cplus_pthread_mutex_init(pthread_mutex_t*,const pthread_mutexattr_t*);
int cplus_pthread_mutex_lock(pthread_mutex_t*);
int cplus_pthread_mutex_unlock(pthread_mutex_t*);
int cplus_pthread_mutex_destroy(pthread_mutex_t*);
int cplus_pthread_cond_init(pthread_cond_t*,const pthread_condattr_t*);
int cplus_pthread_cond_signal(pthread_cond_t*);
int cplus_pthread_cond_broadcast(pthread_cond_t*);
int cplus_pthread_cond_wait(pthread_cond_t*,pthread_mutex_t*);
int cplus_pthread_cond_destroy(pthread_cond_t*);

static pthread_mutex_t g_mtx;
static long g_counter = 0;

static void *worker(void *arg){
    long n = *(long *)arg;
    for (long i = 0; i < n; i++){
        assert(cplus_pthread_mutex_lock(&g_mtx) == 0);
        g_counter++;
        assert(cplus_pthread_mutex_unlock(&g_mtx) == 0);
    }
    return (void *)(intptr_t)(n * 2);
}

static pthread_mutex_t w_mtx;
static pthread_cond_t  w_cnd;
static int w_ready = 0;

static void *waiter(void *arg){
    (void)arg;
    assert(cplus_pthread_mutex_lock(&w_mtx) == 0);
    while (!w_ready)
        assert(cplus_pthread_cond_wait(&w_cnd, &w_mtx) == 0);
    assert(cplus_pthread_mutex_unlock(&w_mtx) == 0);
    return NULL;
}

int main(void){
    long a1 = 1000, a2 = 1000;
    pthread_t t1, t2;

    assert(cplus_pthread_mutex_init(&g_mtx, NULL) == 0);

    assert(cplus_pthread_create(&t1, NULL, worker, &a1) == 0);
    assert(cplus_pthread_create(&t2, NULL, worker, &a2) == 0);

    void *r1 = NULL, *r2 = NULL;
    assert(cplus_pthread_join(t1, &r1) == 0);
    assert(cplus_pthread_join(t2, &r2) == 0);
    assert(g_counter == 2000);
    assert((long)(intptr_t)r1 == 2000);
    assert((long)(intptr_t)r2 == 2000);

    /* lock/unlock from the owner thread, then destroy */
    assert(cplus_pthread_mutex_lock(&g_mtx) == 0);
    assert(cplus_pthread_mutex_unlock(&g_mtx) == 0);
    assert(cplus_pthread_mutex_destroy(&g_mtx) == 0);

    /* condvar: waiter blocks until main sets the predicate and broadcasts */
    assert(cplus_pthread_mutex_init(&w_mtx, NULL) == 0);
    assert(cplus_pthread_cond_init(&w_cnd, NULL) == 0);

    pthread_t tw;
    assert(cplus_pthread_create(&tw, NULL, waiter, NULL) == 0);

    assert(cplus_pthread_mutex_lock(&w_mtx) == 0);
    w_ready = 1;
    assert(cplus_pthread_cond_broadcast(&w_cnd) == 0);
    assert(cplus_pthread_cond_signal(&w_cnd) == 0);
    assert(cplus_pthread_mutex_unlock(&w_mtx) == 0);

    assert(cplus_pthread_join(tw, NULL) == 0);
    assert(w_ready == 1);

    assert(cplus_pthread_cond_destroy(&w_cnd) == 0);
    assert(cplus_pthread_mutex_destroy(&w_mtx) == 0);

    puts("thread: ok");
    return 0;
}
