// CASE 3 / P1 "Thread-safety analysis — prevent data races".
// Two threads call file::write on the same borrowed FILE* with no guard.
//
// EXPECT: ACCEPT under `c+ check --strict` (capability gap: C+ emits no
// [CPLUS- diagnostic for concurrent writes through one borrowed stream).
// This file is CHECK-ONLY. It is never built or run by this handoff: doing so
// would execute a genuine data race on a single FILE stream.
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/thread.hp"
#include "stdlib/file.hp"

typedef struct {
    file *out; /* borrowed FILE*; no lock serialises the two writers */
} fw_ctx;

static void *fw_worker(void *arg) {
    fw_ctx *ctx = (fw_ctx *)arg;
    file *out = ctx->out;
    int i;
    /* UNSAFE: both workers fwrite() to the same borrowed FILE* concurrently. */
    for (i = 0; i < 16; i++)
        out->write("thread-line\n");
    return NULL;
}

int main(void) {
    error err = error();
    file fw_shared = file_borrow(stdout);
    fw_ctx fw_state;
    fw_state.out = &fw_shared;

    thread fw_a = thread();
    thread fw_b = thread();
    fw_a.start(fw_worker, &fw_state, &err);
    fw_b.start(fw_worker, &fw_state, &err);
    fw_a.join(NULL, &err);
    fw_b.join(NULL, &err);
    fw_a.drop();
    fw_b.drop();
    fw_shared.drop();
    return 0;
}
