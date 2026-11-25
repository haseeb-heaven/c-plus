#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
/* Deterministic snake board: fixed start, scripted directions. */
#define W 8
#define H 8
static int step(int bx, int by, int dx, int dy) {
    error e = error();
    int nx = bx + dx, ny = by + dy;
    if (nx < 0 || nx >= W || ny < 0 || ny >= H)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                              "snake: wall collision"), -1;
    return nx + ny * W;
}
int main(void) {
    int p = step(3, 3, 1, 0);
    int bad = step(0, 0, -1, 0);
    print("SNAKE_GAME", p, bad);
    return (p == 28 && bad == -1) ? 0 : 1;
}
