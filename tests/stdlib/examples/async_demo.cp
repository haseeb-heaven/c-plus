/* Demo: libuv timer loop plus file I/O. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/async.hp"

static int ticks = 0;

static void on_tick(uv_timer_t *timer) {
    int *count = (int *)timer->data;
    (*count)++;
    if (*count >= 5) {
        uv_timer_stop(timer);
        uv_stop(timer->loop);
    }
}

int main(void) {
    error err = error();
    loop main_loop = loop(&err);
    if (!main_loop.valid()) {
        print("no loop", err.what());
        return 1;
    }
    print("loop now", main_loop.now_ms() > 0);

    timer ticker = timer(main_loop.native(), &err);
    if (!ticker.valid())
        return 2;
    ticker.start(on_tick, 10, 10, (void *)&ticks, &err);
    main_loop.run(UV_RUN_DEFAULT, &err);
    print("ticks", ticks, "active", ticker.is_active());

    char buf[512];
    long long read = read_file(&main_loop, "tests/stdlib/data/sample.json", buf,
                               sizeof(buf) - 1, &err);
    if (read > 0) {
        buf[read] = '\0';
        print("read bytes", read, buf);
    }

    long long missing = read_file(&main_loop, "/nonexistent/demo.json", buf,
                                  sizeof(buf), &err);
    print("missing", missing < 0, uv_error_text((int)missing) != NULL);
    return 0;
}
