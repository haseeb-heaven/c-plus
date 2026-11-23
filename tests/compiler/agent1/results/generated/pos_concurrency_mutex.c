#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/concurrency_mutex.c"
#include <pthread.h>
#include <stdio.h>
#include "cplus/cplus.h"

 struct Mutex {
    void *handle;
};
typedef struct Mutex Mutex;
#line 9 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/concurrency_mutex.c"


void Mutex_drop(Mutex *self) {
    if (self->handle != NULL) {
        cplus_mutex_free(self->handle);
        self->handle = NULL;
    }
}

Mutex Mutex_open(void) {
    Mutex m = {0};
    m.handle = cplus_mutex_new();
    return m;
Mutex_drop(&m); 
#line 24 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/concurrency_mutex.c"
}

 struct Lock {
    Mutex *owner;
};
typedef struct Lock Lock;
#line 26 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/concurrency_mutex.c"


void Lock_drop(Lock *self) {
    if (self->owner != NULL) {
        cplus_mutex_unlock(self->owner->handle);
        self->owner = NULL;
    }
}

Lock Lock_acquire(Mutex *m) {
    Lock l = {0};
    l.owner = m;
    cplus_mutex_lock(m->handle);
    return l;
Lock_drop(&l); 
#line 45 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/concurrency_mutex.c"
}

static int counter = 0;

static void *worker(void *arg) {
    Mutex *m = (Mutex *)arg;
    int i;
    for (i = 0; i < 20000; i++) {
        Lock guard = Lock_acquire(m);
        counter++;
        (void)guard;
    Lock_drop(&guard); 
#line 56 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/concurrency_mutex.c"
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
    { __typeof__(0) __cplus_ret = (0); Mutex_drop(&m);  return __cplus_ret;; }
Mutex_drop(&m); 
#line 69 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/concurrency_mutex.c"
}
