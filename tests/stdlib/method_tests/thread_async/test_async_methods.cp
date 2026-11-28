/* 100% per-method coverage for stdlib/async.hp.
 *
 * Covers all 16 real public callables: the 14 inventory entries plus the two
 * multi-line signatures the inventory parser misses (timer.start, read_file).
 * The `stdlib_timer_release_handle` helper is excluded by the inventory's
 * stdlib_ rule; it is exercised indirectly by timer.drop().
 *
 * Every method is called with a deterministic case whose observable result is
 * verified.  Timers use small fixed timeouts and stop the loop from inside a
 * callback, so uv_run is bounded by the callback itself; the harness also
 * enforces `timeout 30`.  No wall-clock-sensitive assertion is used: the only
 * time check is that sleep_ms(5) makes a monotonic uv_now advance (> 0).
 *
 * Each printed line is "<header> <method> PASS|FAIL".
 */
#include "stdlib/async.hp"
#include <string.h>

static int as_checks = 0;
static int as_failures = 0;

static void as_verify(const char *header, const char *method, int cond) {
    as_checks++;
    if (cond)
        print(header, method, "PASS");
    else {
        as_failures++;
        print(header, method, "FAIL");
    }
}

/* ---- callbacks (file scope, deterministic, self-bounding) ---------------- */
static int as_run_count = 0;
static void as_run_cb(uv_timer_t *t) {
    as_run_count++;
    uv_stop(t->loop);
}

static int as_start_count = 0;
static void as_start_cb(uv_timer_t *t) {
    int *p = (int *)t->data;
    (*p)++;
    uv_stop(t->loop);
}

static int as_stop_count = 0;
static void as_stop_cb(uv_timer_t *t) {
    loop *l = (loop *)t->data;
    as_stop_count++;
    if (as_stop_count >= 3)
        l->stop();
}

int main(void) {
    error as_err = error();
    const char *as_fixture = "tests/stdlib/method_tests/thread_async/fixture.txt";

    /* ================= loop (7) ================= */
    {
        loop as_ctor_loop = loop(&as_err);
        as_verify("async.hp", "loop.ctor",
                  as_ctor_loop.valid() != 0 && as_err.ok() != 0);
    }
    {
        loop as_valid_loop = loop(&as_err);
        loop as_valid_zero; /* zero-initialised: initialized == 0 */
        as_verify("async.hp", "loop.valid",
                  as_valid_loop.valid() != 0 && as_valid_zero.valid() == 0);
    }
    {
        loop as_native_loop = loop(&as_err);
        as_verify("async.hp", "loop.native", as_native_loop.native() != NULL);
    }
    {
        loop as_run_loop = loop(&as_err);
        timer as_run_timer = timer(as_run_loop.native(), &as_err);
        as_run_count = 0;
        int as_run_start = as_run_timer.start(as_run_cb, 1, 0, NULL, &as_err);
        int as_run_rc = as_run_loop.run(UV_RUN_DEFAULT, &as_err);
        int as_run_err_ok = as_err.ok() != 0;
        loop as_run_zero; /* invalid loop rejects run() */
        int as_run_bad = as_run_zero.run(UV_RUN_DEFAULT, &as_err);
        int as_run_bad_err = as_err.failed_now() != 0;
        as_verify("async.hp", "loop.run",
                  as_run_start == STDLIB_OK && as_run_rc == STDLIB_OK &&
                      as_run_err_ok && as_run_count == 1 &&
                      as_run_bad == STDLIB_FAIL && as_run_bad_err);
    }
    {
        loop as_stop_loop = loop(&as_err);
        timer as_stop_timer = timer(as_stop_loop.native(), &as_err);
        as_stop_count = 0;
        int as_stop_start =
            as_stop_timer.start(as_stop_cb, 1, 1, (void *)&as_stop_loop, &as_err);
        int as_stop_rc = as_stop_loop.run(UV_RUN_DEFAULT, &as_err);
        as_verify("async.hp", "loop.stop",
                  as_stop_start == STDLIB_OK && as_stop_rc == STDLIB_OK &&
                      as_stop_count == 3);
    }
    {
        loop as_now_loop = loop(&as_err);
        uv_update_time(as_now_loop.native());
        unsigned long long as_t0 = as_now_loop.now_ms();
        sleep_ms(5); /* monotonic: guarantees >= 5 ms have elapsed */
        uv_update_time(as_now_loop.native());
        unsigned long long as_t1 = as_now_loop.now_ms();
        loop as_now_zero;
        as_verify("async.hp", "loop.now_ms",
                  as_t1 > as_t0 && as_now_zero.now_ms() == 0);
    }
    {
        loop as_drop_loop = loop(&as_err);
        as_drop_loop.drop();
        as_verify("async.hp", "loop.drop", as_drop_loop.valid() == 0);
    }

    /* ================= timer (6) ================= */
    {
        loop as_tctor_loop = loop(&as_err);
        timer as_tctor = timer(as_tctor_loop.native(), &as_err);
        int as_tctor_ok = as_tctor.valid() != 0 && as_err.ok() != 0;
        timer as_bad_tctor = timer(NULL, &as_err);
        int as_bad_ok = as_bad_tctor.valid() == 0 && as_err.failed_now() != 0;
        as_verify("async.hp", "timer.ctor", as_tctor_ok && as_bad_ok);
    }
    {
        loop as_tvalid_loop = loop(&as_err);
        timer as_tvalid = timer(as_tvalid_loop.native(), &as_err);
        timer as_tvalid_zero; /* zero-initialised */
        as_verify("async.hp", "timer.valid",
                  as_tvalid.valid() != 0 && as_tvalid_zero.valid() == 0);
    }
    {
        loop as_start_loop = loop(&as_err);
        timer as_start_timer = timer(as_start_loop.native(), &as_err);
        as_start_count = 0;
        int as_start_rc = as_start_timer.start(as_start_cb, 1, 0,
                                               (void *)&as_start_count, &as_err);
        int as_start_err_ok = as_err.ok() != 0;
        int as_active = as_start_timer.is_active() != 0;
        int as_start_bad = as_start_timer.start(NULL, 1, 0, NULL, &as_err);
        int as_start_loop_rc = as_start_loop.run(UV_RUN_DEFAULT, &as_err);
        as_verify("async.hp", "timer.start",
                  as_start_rc == STDLIB_OK && as_start_err_ok && as_active &&
                      as_start_bad == STDLIB_FAIL && as_start_count == 1 &&
                      as_start_loop_rc == STDLIB_OK);
    }
    {
        loop as_tstop_loop = loop(&as_err);
        timer as_tstop_timer = timer(as_tstop_loop.native(), &as_err);
        as_tstop_timer.start(as_run_cb, 1, 1, NULL, &as_err);
        int as_before = as_tstop_timer.is_active() != 0;
        int as_stop_rc = as_tstop_timer.stop(&as_err);
        int as_after = as_tstop_timer.is_active() == 0;
        timer as_tstop_zero;
        int as_zero_stop = as_tstop_zero.stop(&as_err);
        as_verify("async.hp", "timer.stop",
                  as_before && as_stop_rc == STDLIB_OK && as_after &&
                      as_zero_stop == STDLIB_FAIL);
    }
    {
        loop as_active_loop = loop(&as_err);
        timer as_active_timer = timer(as_active_loop.native(), &as_err);
        int as_inactive0 = as_active_timer.is_active() == 0;
        as_active_timer.start(as_run_cb, 1, 1, NULL, &as_err);
        int as_active1 = as_active_timer.is_active() != 0;
        as_active_timer.stop(&as_err);
        int as_inactive2 = as_active_timer.is_active() == 0;
        as_verify("async.hp", "timer.is_active",
                  as_inactive0 && as_active1 && as_inactive2);
    }
    {
        loop as_tdrop_loop = loop(&as_err);
        timer as_tdrop_timer = timer(as_tdrop_loop.native(), &as_err);
        as_tdrop_timer.start(as_run_cb, 1, 1, NULL, &as_err);
        as_tdrop_timer.drop(); /* stops, closes and drains the handle */
        as_verify("async.hp", "timer.drop", as_tdrop_timer.valid() == 0);
    }

    /* ================= free functions (3 public callables) ================ */
    {
        loop as_read_loop = loop(&as_err);
        char as_buf[128];
        long long as_n = read_file(&as_read_loop, as_fixture, as_buf,
                                   sizeof(as_buf) - 1, &as_err);
        int as_read_ok = as_err.ok() != 0;
        if (as_n > 0)
            as_buf[(int)as_n] = '\0';
        int as_data_ok = as_n > 0 &&
                         strncmp(as_buf, "CPLUS_METHOD_TEST_FIXTURE_12345", 31) == 0;
        long long as_missing = read_file(&as_read_loop, "/nonexistent/cplus/mt.txt",
                                         as_buf, sizeof(as_buf), &as_err);
        int as_missing_err = as_err.failed_now() != 0;
        as_verify("async.hp", "read_file",
                  as_read_ok && as_data_ok && as_missing < 0 && as_missing_err);
    }
    {
        loop as_sleep_loop = loop(&as_err);
        uv_update_time(as_sleep_loop.native());
        unsigned long long as_s0 = as_sleep_loop.now_ms();
        sleep_ms(5); /* guarantees >= 5 ms of monotonic time elapses */
        uv_update_time(as_sleep_loop.native());
        unsigned long long as_s1 = as_sleep_loop.now_ms();
        as_verify("async.hp", "sleep_ms", as_s1 > as_s0);
    }
    {
        const char *as_text = uv_error_text(-2);
        int as_text_ok = as_text != NULL &&
                         strcmp(as_text, uv_strerror(-2)) == 0 &&
                         uv_error_text(0) != NULL;
        as_verify("async.hp", "uv_error_text", as_text_ok);
    }

    print("METHOD_TEST", "async", as_checks, as_failures);
    return as_failures == 0 ? 0 : 1;
}
