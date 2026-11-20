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

resource struct Lock {
    Mutex *owner;
};

void Lock_drop(Lock *self) {
    if (self->owner != NULL) {
        cplus_mutex_unlock(self->owner->handle);
        self->owner = NULL;
    }
}

Lock Lock_acquire(Mutex *m) {
    Lock l;
    l.owner = m;
    cplus_mutex_lock(m->handle);
    return l;
}

static int counter = 0;

static void *worker(void *arg) {
    Mutex *m = (Mutex *)arg;
    int i;
    for (i = 0; i < 20000; i++) {
        Lock guard = Lock_acquire(m);
        counter++;
        (void)guard;
    }
    return NULL;
}

int main(void) {
    Mutex m = Mutex_open();
    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker, &m);
    pthread_create(&t2, NULL, worker, &m);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("counter=%d\n", counter);
    return 0;
}
