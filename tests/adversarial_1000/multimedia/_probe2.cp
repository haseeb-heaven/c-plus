/* Temporary behavior probe 2 (removed before handoff). */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"

static void trywin(int w, int h) {
    error e = error();
    window x = window("p", w, h, (unsigned)SDL_WINDOW_HIDDEN, &e);
    printf("win %d %d -> valid=%d err=%d what=%s\n", w, h, x.valid(),
           e.failed_now(), e.what());
    x.drop();
}

int main(void) {
    error e = error();
    sdl core = sdl((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS), &e);
    trywin(0, 48);
    trywin(1, 0);
    trywin(-1, 48);
    trywin(4096, 4096);
    trywin(65536, 65536);
    trywin(1 << 20, 1 << 20);
    trywin(1 << 30, 1 << 30);
    trywin(2147483647, 2147483647);
    core.drop();
    printf("PROBE2 done\n");
    return 0;
}
