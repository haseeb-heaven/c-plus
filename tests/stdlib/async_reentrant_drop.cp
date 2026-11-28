/* Issue #222 regression: dropping an async::loop from inside its own uv_run()
 * callback must not leave loop::run() dereferencing a NULL token.
 *
 * The callback drops the loop (which clears loop.token and releases the
 * loop's token reference).  run() holds its own reference, clears `running`
 * on that stable token and returns normally.  Telemetry checks that the token
 * reference really reached zero and nothing leaked.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/async.hp"

static int ard_callbacks = 0;

static void ard_drop_loop_cb(uv_timer_t *t) {
    loop *owner = (loop *)t->data;
    ard_callbacks++;
    owner->drop();
}

int main(void) {
    error err = error();
    stdlib_telemetry_stats before;
    stdlib_telemetry_stats after;
    int rc = 0;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_telemetry_snapshot(&before);

    {
        loop reentrant = loop(&err);
        timer trigger = timer(reentrant.native(), &err);
        if (!reentrant.valid() || !trigger.valid()) {
            print("reentrant-setup", 0);
            return 1;
        }
        trigger.start(ard_drop_loop_cb, 0, 0, (void *)&reentrant, &err);
        rc = reentrant.run(UV_RUN_DEFAULT, &err);
        print("reentrant-run", rc == STDLIB_OK, err.failed_now() == 0,
              ard_callbacks == 1, reentrant.valid() == 0);
        if (rc != STDLIB_OK || err.failed_now() || ard_callbacks != 1 ||
            reentrant.valid())
            rc = 1;
        else
            rc = 0;
        trigger.drop();
        reentrant.drop();
        /* run() released its own token reference after uv_run returned; the
         * loop's reference went away in the callback and the timer's went
         * away above.  The shared token must have reached zero exactly once. */
        stdlib_telemetry_snapshot(&after);
        print("reentrant-counts", after.alloc_count - before.alloc_count,
              after.free_count - before.free_count);
        print("reentrant-refs",
              after.alloc_count - before.alloc_count ==
                  after.free_count - before.free_count);
        if (after.alloc_count - before.alloc_count !=
            after.free_count - before.free_count)
            rc = 1;
    }
    return rc;
}
