/* Issue #60 regression: timer::drop() must never run its owner loop.
 *
 * Two observable consequences of the old bounded uv_run(..., UV_RUN_NOWAIT)
 * drain are checked here:
 *   A. dropping a timer dispatched an unrelated, already-due timer;
 *   B. dropping a timer from inside a callback re-entered uv_run() and
 *      dispatched an idle callback while the outer callback was still active.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/async.hp"

static int r_canary_ticks = 0;
static int r_idle_ticks = 0;
static int r_in_callback = 0;
static int r_nested_dispatch = 0;

static void r_canary_cb(uv_timer_t *t) {
    (void)t;
    r_canary_ticks++;
}

static void r_noop_cb(uv_timer_t *t) {
    (void)t;
}

static void r_idle_cb(uv_idle_t *idle) {
    r_idle_ticks++;
    if (r_in_callback)
        r_nested_dispatch++;
    uv_idle_stop(idle);
}

static void r_dropper_cb(uv_timer_t *t) {
    timer *held = (timer *)t->data;
    r_in_callback = 1;
    held->drop();
    r_in_callback = 0;
}

int main(void) {
    error err = error();
    int rc = 0;

    /* A: a due canary must not be dispatched by another timer's drop(). */
    loop loop_a = loop(&err);
    if (!loop_a.valid())
        return 1;
    timer canary = timer(loop_a.native(), &err);
    timer victim = timer(loop_a.native(), &err);
    if (!canary.valid() || !victim.valid())
        return 2;
    canary.start(r_canary_cb, 0, 0, (void *)0, &err);
    victim.start(r_noop_cb, 100000, 0, (void *)0, &err);
    victim.drop();
    print("drop-no-dispatch", r_canary_ticks == 0, victim.valid() == 0);
    if (r_canary_ticks != 0)
        rc = 1;
    canary.stop(&err);
    canary.drop();
    loop_a.drop();

    /* B: drop() from a timer callback must not re-enter the running loop. */
    loop loop_b = loop(&err);
    if (!loop_b.valid())
        return 3;
    uv_idle_t idle;
    if (uv_idle_init(loop_b.native(), &idle) != 0)
        return 4;
    if (uv_idle_start(&idle, r_idle_cb) != 0)
        return 5;
    timer dropper = timer(loop_b.native(), &err);
    timer held = timer(loop_b.native(), &err);
    if (!dropper.valid() || !held.valid())
        return 6;
    held.start(r_noop_cb, 100000, 0, (void *)0, &err);
    dropper.start(r_dropper_cb, 0, 0, (void *)&held, &err);
    if (loop_b.run(UV_RUN_DEFAULT, &err) != STDLIB_OK)
        return 7;
    print("drop-no-reentry", r_idle_ticks == 1, r_nested_dispatch == 0,
          held.valid() == 0);
    if (r_idle_ticks != 1 || r_nested_dispatch != 0)
        rc = 1;
    uv_close((uv_handle_t *)&idle, (uv_close_cb)0);
    dropper.drop();
    held.drop();
    loop_b.drop();

    return rc;
}
