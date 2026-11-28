/* Issue #60 regression, part two: lifetime safety when a loop is dropped
 * before a timer, plus the no-reentrancy contract for timer::drop().
 *
 * All outcomes are observed synchronously: no sleeps, no wall-clock races,
 * no reliance on callback ordering beyond libuv's documented timer ordering.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/async.hp"

static int lr_canary_ticks = 0;
static int lr_victim_ticks = 0;
static int lr_loop_first_ticks = 0;

static void lr_canary_cb(uv_timer_t *h) {
    (void)h;
    lr_canary_ticks++;
}

static void lr_victim_cb(uv_timer_t *h) {
    (void)h;
    lr_victim_ticks++;
}

static void lr_loop_first_cb(uv_timer_t *h) {
    (void)h;
    lr_loop_first_ticks++;
}

int main(void) {
    error err = error();
    int rc = 0;

    /* (a) A due timer is not dispatched by another timer's drop(), and the
     * dropped timer is no longer usable. */
    loop first = loop(&err);
    if (!first.valid())
        return 1;
    timer canary = timer(first.native(), &err);
    timer victim = timer(first.native(), &err);
    if (!canary.valid() || !victim.valid())
        return 2;
    canary.start(lr_canary_cb, 0, 0, (void *)0, &err);
    victim.start(lr_victim_cb, 100000, 0, (void *)0, &err);
    victim.drop();
    print("lifetime-no-dispatch", lr_canary_ticks == 0,
          lr_victim_ticks == 0, victim.valid() == 0);
    if (lr_canary_ticks != 0 || lr_victim_ticks != 0 || victim.valid())
        rc = 1;
    canary.stop(&err);
    canary.drop();
    first.drop();

    /* (b) The owner loop is destroyed first.  Every timer operation after
     * that point must fail closed, and dropping the timer must release only
     * its own bookkeeping - never dereference the dead loop.  Reaching this
     * print at all proves no UAF/segfault occurred. */
    loop second = loop(&err);
    if (!second.valid())
        return 3;
    timer survivor = timer(second.native(), &err);
    if (!survivor.valid())
        return 4;
    survivor.start(lr_loop_first_cb, 1, 0, (void *)0, &err);
    second.drop();
    print("lifetime-loop-first", second.valid() == 0,
          survivor.valid() == 0, survivor.is_active() == 0);
    if (second.valid() || survivor.valid() || survivor.is_active())
        rc = 1;
    print("lifetime-fail-closed",
          survivor.stop(&err) == STDLIB_FAIL,
          survivor.start(lr_loop_first_cb, 1, 0, (void *)0, &err) ==
              STDLIB_FAIL,
          err.failed_now());
    survivor.drop();
    print("lifetime-drop-after-loop", survivor.valid() == 0,
          lr_loop_first_ticks == 0);
    if (survivor.valid())
        rc = 1;

    return rc;
}
