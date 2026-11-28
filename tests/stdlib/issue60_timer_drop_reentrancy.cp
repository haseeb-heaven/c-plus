/* Issue #60: timer::drop() must not call uv_run() recursively from inside a
 * libuv callback, and must not touch a loop that has already been dropped. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/async.hp"

static uv_loop_t *shared_loop;
static int callbacks;
static int nested_drop_ok;

static void on_tick(uv_timer_t *handle) {
    error e = error();
    timer nested = timer(shared_loop, &e);
    callbacks++;
    if (nested.valid()) {
        nested.drop();             /* called from inside uv_run() */
        nested_drop_ok = 1;
    }
    uv_stop(handle->loop);
}

int main(void) {
    error e = error();
    loop l = loop(&e);
    int fails = 0;
    loop closed = loop(&e);
    uv_loop_t *closed_native;
    timer t;
    timer stale;

    if (!l.valid()) return 1;
    shared_loop = l.native();
    t = timer(l.native(), &e);
    if (!t.valid()) return 2;
    if (t.start(on_tick, 1, 0, (void *)0, &e) != STDLIB_OK) return 3;
    if (l.run(UV_RUN_DEFAULT, &e) != STDLIB_OK) return 4;
    t.drop();
    if (callbacks != 1 || nested_drop_ok != 1) fails++;

    /* A loop dropped before its timer must make the timer fail closed. */
    closed_native = closed.native();
    closed.drop();
    stale = timer(closed_native, &e);
    if (stale.valid() != 0 || e.failed_now() == 0) fails++;
    stale.drop();

    printf("ISSUE60 %d\n", fails);
    l.drop();
    return fails ? 1 : 0;
}
