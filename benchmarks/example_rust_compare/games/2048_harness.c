/* Deterministic driver for examples/games/2048.c.
 *
 * The original source is included verbatim with main renamed and rand/srand
 * redirected to a fixed-seed LCG.  No line of the game's own logic is edited.
 * every print below is board/score state produced by the original functions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DET_SEED 12345u
static uint32_t det_state = DET_SEED;
static void det_seed(unsigned seed)
{
    (void)seed; /* ignore the runtime seed on purpose: fully deterministic */
    det_state = DET_SEED;
}
static int det_rand(void)
{
    det_state = det_state * 1103515245u + 12345u;
    return (int)((det_state >> 16) & 0x7fffu);
}
#define rand() det_rand()
#define srand(x) det_seed(x)
#define main original_2048_main
#include "2048.c"
#undef main
#undef srand
#undef rand

static void print_board(uint8_t board[SIZE][SIZE])
{
    for (int x = 0; x < SIZE; x++)
    {
        for (int y = 0; y < SIZE; y++)
        {
            printf("%s%u", (x || y) ? "," : "", board[x][y]);
        }
    }
    printf("\n");
}

int main(void)
{
    uint8_t board[SIZE][SIZE];
    uint32_t score = 0;
    const char *moves = "adwsadsw";

    det_seed(0);
    memset(board, 0, sizeof board);
    initBoard(board);

    printf("SEED %u\n", DET_SEED);
    printf("INIT ");
    print_board(board);

    for (const char *p = moves; *p; ++p)
    {
        bool ok = false;
        switch (*p)
        {
        case 'a': ok = moveLeft(board, &score); break;
        case 'd': ok = moveRight(board, &score); break;
        case 'w': ok = moveUp(board, &score); break;
        case 's': ok = moveDown(board, &score); break;
        default: break;
        }
        if (ok)
        {
            addRandom(board); /* same as the game loop after a successful move */
        }
        printf("MOVE %c %d %u ", *p, ok ? 1 : 0, score);
        print_board(board);
    }

    printf("ENDED %d %d %d\n", gameEnded(board) ? 1 : 0,
           countEmpty(board), findPairDown(board) ? 1 : 0);
    printf("TESTS_OK %d\n", testSucceed() ? 1 : 0);
    return 0;
}
