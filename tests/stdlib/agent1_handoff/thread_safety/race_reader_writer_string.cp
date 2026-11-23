// CASE 2 / P1 "Thread-safety analysis — prevent data races".
// One thread reads a stdlib string while another thread drop()s it.
//
// EXPECT: ACCEPT under `c+ check --strict` (capability gap: C+ does not model
// cross-thread lifetime/ownership, so no [CPLUS- diagnostic is emitted).
// This file is CHECK-ONLY. It is never built or run by this handoff: doing so
// would execute a genuine use-after-free data race.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"
#include "stdlib/string.hp"

typedef struct {
    string *text; /* shared string; no guard synchronises reader vs dropper */
} rws_ctx;

static void *rws_reader(void *arg) {
    rws_ctx *ctx = (rws_ctx *)arg;
    string *s = ctx->text;
    const char *raw;
    /* UNSAFE: reads chars/count while another thread may call drop(). */
    raw = s->c_str();
    (void)raw;
    (void)s->size();
    return NULL;
}

static void *rws_dropper(void *arg) {
    rws_ctx *ctx = (rws_ctx *)arg;
    string *s = ctx->text;
    /* UNSAFE: releases the backing buffer concurrently with rws_reader(). */
    s->drop();
    return NULL;
}

int main(void) {
    error err = error();
    string rws_text = string("hello");
    rws_ctx rws_shared;
    rws_shared.text = &rws_text;

    thread rws_reader_t = thread();
    thread rws_dropper_t = thread();
    rws_reader_t.start(rws_reader, &rws_shared, &err);
    rws_dropper_t.start(rws_dropper, &rws_shared, &err);
    rws_reader_t.join(NULL, &err);
    rws_dropper_t.join(NULL, &err);
    rws_reader_t.drop();
    rws_dropper_t.drop();
    rws_text.drop();
    return 0;
}
