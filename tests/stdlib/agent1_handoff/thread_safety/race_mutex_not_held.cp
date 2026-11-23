// CASE 4 / P1 "Thread-safety analysis — prevent data races".
// A mutex is created, but no lock/guard is held when the shared state is
// accessed by two threads (a "present but unused" synchronisation primitive).
//
// EXPECT: ACCEPT under `c+ check --strict` (capability gap: C+ does not verify
// that a created mutex is actually locked around each shared-state access).
// This file is CHECK-ONLY. It is never built or run by this handoff: doing so
// would execute a genuine data race on `counter`.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"

typedef struct {
    mutex *mtx;  /* created by main(), but NEVER locked by these workers */
    int *counter;/* shared state accessed without holding mtx */
} mnh_ctx;

static void *mnh_worker(void *arg) {
    mnh_ctx *ctx = (mnh_ctx *)arg;
    int *counter = ctx->counter;
    int i;
    /* UNSAFE: ctx->mtx exists but no lock()/guard() is taken here. */
    for (i = 0; i < 64; i++)
        (*counter) += 1;
    return NULL;
}

int main(void) {
    error err = error();
    mutex mnh_lock = mutex(); /* the mutex is created ... */
    int mnh_counter = 0;
    mnh_ctx mnh_shared;
    mnh_shared.mtx = &mnh_lock;
    mnh_shared.counter = &mnh_counter;

    thread mnh_a = thread();
    thread mnh_b = thread();
    /* ... but neither worker holds it while touching mnh_counter. */
    mnh_a.start(mnh_worker, &mnh_shared, &err);
    mnh_b.start(mnh_worker, &mnh_shared, &err);
    mnh_a.join(NULL, &err);
    mnh_b.join(NULL, &err);
    mnh_a.drop();
    mnh_b.drop();
    mnh_lock.drop();
    return 0;
}
