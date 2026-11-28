/* Deterministic driver for examples/games/tiles.c (2048 sliding-tile clone).
 * Original source included verbatim with main renamed and rand/srand bound to
 * a fixed-seed LCG.  Prints grid + score after each scripted move.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define DET_SEED 12345u
static uint32_t det_state = DET_SEED;
static void det_seed(unsigned seed) { (void)seed; det_state = DET_SEED; }
static int det_rand(void)
{
    det_state = det_state * 1103515245u + 12345u;
    return (int)((det_state >> 16) & 0x7fffu);
}
#define rand() det_rand()
#define srand(x) det_seed(x)
#define main original_tiles_main
#include "tiles.c"
#undef main
#undef srand
#undef rand

static void print_grid(void)
{
    for (int i = 0; i < 16; i++)
    {
        printf("%s%d", i ? "," : "", g[i / 4][i % 4]);
    }
    printf("\n");
}

int main(void)
{
    static const int script[] = {0, 2, 1, 3, 0, 0, 1, 2}; /* L,U,R,D,L,L,R,U */

    det_seed(0);
    memset(g, 0, sizeof g);
    score = 0;
    spawn();
    spawn();

    printf("SEED %u\n", DET_SEED);
    printf("INIT ");
    print_grid();

    for (unsigned i = 0; i < sizeof script / sizeof script[0]; i++)
    {
        int moved = move(script[i]);
        if (moved)
        {
            spawn();
        }
        printf("MOVE %d %d %d ", script[i], moved, score);
        print_grid();
    }
    printf("FINAL %d\n", score);
    return 0;
}
