/* Deterministic driver for examples/games/snake.c.
 * Original source included verbatim with main renamed and rand/srand bound to
 * a fixed-seed LCG.  Input() is replaced by a fixed direction script; Logic()
 * is called unchanged so collision/growth/fruit rules are the original ones.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

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
#define main original_snake_main
#include "snake.c"
#undef main
#undef srand
#undef rand

static void print_tail(void)
{
    for (int i = 0; i < tailLength; i++)
    {
        printf("%s%d,%d", i ? ";" : "", tailX[i], tailY[i]);
    }
}

int main(void)
{
    static const char script[] = "dddwwaaaaaaaaaaaa";
    int setup_fx, setup_fy;

    det_seed(0);
    gameover = 0;
    score = 0;
    tailLength = 0;
    for (int i = 0; i < 100; i++) { tailX[i] = 0; tailY[i] = 0; }
    dir = RIGHT;
    headX = WIDTH / 2;
    headY = HEIGHT / 2;

    /* same placement calls as Setup(), with the deterministic rand() */
    det_seed(0);
    setup_fx = det_rand() % WIDTH;
    setup_fy = det_rand() % HEIGHT;

    /* Fix the fruit three cells ahead so the scripted run eats it. */
    fruitX = headX + 3;
    fruitY = headY;

    printf("SEED %u\n", DET_SEED);
    printf("SETUP_FRUIT %d,%d\n", setup_fx, setup_fy);
    printf("START head=%d,%d fruit=%d,%d dir=%d\n", headX, headY, fruitX, fruitY, dir);

    for (const char *p = script; *p && !gameover; ++p)
    {
        switch (*p)
        {
        case 'a': dir = LEFT; break;
        case 'd': dir = RIGHT; break;
        case 'w': dir = UP; break;
        case 's': dir = DOWN; break;
        default: break;
        }
        Logic();
        printf("STEP %c head=%d,%d tail=", *p, headX, headY);
        print_tail();
        printf(" score=%d fruit=%d,%d over=%d\n", score, fruitX, fruitY, gameover ? 1 : 0);
    }
    printf("FINAL score=%d over=%d\n", score, gameover ? 1 : 0);
    return 0;
}
