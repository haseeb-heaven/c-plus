/* Demo F: RAII-guarded mutex with native threads.
 *
 * The guard is an owning resource: it unlocks when the loop iteration or
 * function scope exits, including early returns.  C+ currently provides the
 * primitives and deterministic lock release; static verification that every
 * shared mutation is locked is still on the roadmap (see docs/LIMITATIONS.md). */
#include <pthread.h>
#include <stdio.h>
#include "cplus/cplus.h"

resource struct Mutex {
    void *handle;
};

void Mutex_drop(Mutex *self) {
    if (self->handle != NULL) {
        cplus_mutex_free(self->handle);
        self->handle = NULL;
    }
}

Mutex Mutex_open(void) {
    Mutex m;
    m.handle = cplus_mutex_new();
    return m;
}

resource struct Guard {
    Mutex *mutex;
};

void Guard_drop(Guard *self) {
    if (self->mutex != NULL) {
        cplus_mutex_unlock(self->mutex->handle);
        self->mutex = NULL;
    }
}

Guard Guard_lock(Mutex *mutex) {
    Guard g;
    g.mutex = mutex;
    cplus_mutex_lock(mutex->handle);
    return g;
}

static long shared_counter = 0;

static void *worker(void *arg) {
    Mutex *mutex = (Mutex *)arg;
    int i;
    for (i = 0; i < 50000; i++) {
        Guard guard = Guard_lock(mutex);
        shared_counter++;
        (void)guard;
    }
    return NULL;
}

int main(void) {
    Mutex mutex = Mutex_open();
    pthread_t a, b, c;
    pthread_create(&a, NULL, worker, &mutex);
    pthread_create(&b, NULL, worker, &mutex);
    pthread_create(&c, NULL, worker, &mutex);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    pthread_join(c, NULL);
    printf("counter=%ld\n", shared_counter);
    return 0;
}
