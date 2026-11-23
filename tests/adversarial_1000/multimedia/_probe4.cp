/* Temporary oversized-window stress probe (removed before handoff). */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"

int main(void) {
    error e = error();
    sdl core = sdl((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS), &e);
    int k;
    for (k = 0; k < 40; k++) {
        int big = 4097 + k * 200;
        window x = window("adv", big, big, (unsigned)SDL_WINDOW_HIDDEN, &e);
        printf("k=%d big=%d valid=%d\n", k, big, x.valid());
        if (x.valid())
            x.drop();
    }
    core.drop();
    printf("PROBE4 done\n");
    return 0;
}
