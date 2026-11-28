/* libuv wrapper test: looping timer, loop control, file read, failures. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/async.hp"

static int ticks = 0;

static void on_tick(uv_timer_t *timer) {
    int *target = (int *)timer->data;
    (*target)++;
    if (*target >= 3) {
        uv_timer_stop(timer);
        uv_stop(timer->loop);
    }
}

int main(void) {
    error err = error();
    loop main_loop = loop(&err);
    if (!main_loop.valid())
        return 1;
    print("loop", main_loop.valid(), main_loop.now_ms() > 0,
          main_loop.native() != NULL);

    void *user = (void *)&ticks;
    timer ticker = timer(main_loop.native(), &err);
    if (!ticker.valid())
        return 2;
    if (ticker.start(on_tick, 5, 5, user, &err) != STDLIB_OK)
        return 3;
    print("timer-active", ticker.is_active());
    if (main_loop.run(UV_RUN_DEFAULT, &err) != STDLIB_OK)
        return 4;
    print("ticks", ticks, ticker.is_active() == 0);

    timer once = timer(main_loop.native(), &err);
    once.start(on_tick, 1000, 0, (void *)&ticks, &err);
    once.stop(&err);
    print("stopped", once.is_active() == 0);

    char buf[256];
    long long read = read_file(&main_loop, "tests/stdlib/data/sample.json", buf,
                               sizeof(buf) - 1, &err);
    if (read > 0)
        buf[read] = '\0';
    print("read", read > 0, strstr(buf, "cplus") != NULL);

    long long missing = read_file(&main_loop, "/nonexistent/cplus/nope.txt", buf,
                                  sizeof(buf), &err);
    print("missing", missing < 0, err.failed_now(), uv_error_text(-2) != NULL);

    sleep_ms(1);
    loop uninitialised;
    int bad_run = uninitialised.run(UV_RUN_DEFAULT, &err);
    print("bad-loop", uninitialised.valid() == 0, bad_run == STDLIB_FAIL,
          err.failed_now());

    timer no_loop = timer(NULL, &err);
    print("bad-timer", no_loop.valid() == 0, err.failed_now());
    return 0;
}
