// CASE 1 / P1 "Thread-safety analysis — prevent data races".
// Shared stdlib vector mutated by two threads with NO guard/lock.
//
// EXPECT: ACCEPT under `c+ check --strict` (capability gap: C+ emits no
// [CPLUS- diagnostic for unsynchronised access to a shared container).
// This file is CHECK-ONLY. It is never built or run by this handoff: doing so
// would execute a genuine data race. Compile-time evidence is sufficient.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"
#include "stdlib/vector.hp"

typedef struct {
    vector *items; /* shared vector; no mutex, no guard protects it */
} rv_ctx;

static void *rv_worker(void *arg) {
    rv_ctx *ctx = (rv_ctx *)arg;
    vector *shared = ctx->items;
    int i;
    /* UNSAFE: two workers call push_back() on the same vector concurrently;
     * vector::push_back mutates items/used/space/generation with no locking. */
    for (i = 0; i < 16; i++)
        shared->push_back(i);
    return NULL;
}

int main(void) {
    error err = error();
    vector rv_items = vector();
    rv_ctx rv_shared;
    rv_shared.items = &rv_items;

    thread rv_a = thread();
    thread rv_b = thread();
    rv_a.start(rv_worker, &rv_shared, &err);
    rv_b.start(rv_worker, &rv_shared, &err);
    rv_a.join(NULL, &err);
    rv_b.join(NULL, &err);
    rv_a.drop();
    rv_b.drop();
    rv_items.drop();
    return 0;
}
