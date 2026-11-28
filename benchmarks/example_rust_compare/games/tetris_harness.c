/* Deterministic driver for examples/games/tetris.c.
 * The original curses game is included verbatim with main renamed and
 * rand/srand bound to a fixed-seed LCG.  Only the real rules functions are
 * exercised: shape generation, 7-bag, collision, rotation kicks, soft/hard
 * drop, line clears, scoring and gravity.  Rendering and the real-time loop
 * are intentionally not driven (see RESULTS.md).
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
#define main original_tetris_main
#include "tetris.c"
#undef main
#undef srand
#undef rand

static void print_board(const Game *g)
{
    for (int row = 0; row < BOARD_H; row++)
    {
        if (row)
        {
            printf("/");
        }
        for (int col = 0; col < BOARD_W; col++)
        {
            printf("%d", g->board[row][col]);
        }
    }
    printf("\n");
}

static void print_state(const char *op, const Game *g)
{
    printf("OP %s piece=%d rot=%d x=%d y=%d next=%d score=%d lines=%d level=%d over=%d\n",
           op, g->piece, g->rot, g->x, g->y, g->next, g->score, g->lines,
           g->level, g->over ? 1 : 0);
    print_board(g);
}

int main(void)
{
    Game g;
    int script[] = {KEY_UP, KEY_LEFT, KEY_DOWN, KEY_DOWN, ' ',
                    KEY_UP, KEY_RIGHT, ' ', KEY_DOWN, ' '};
    const char *names[] = {"ROT", "LEFT", "SOFT", "SOFT", "DROP",
                           "ROT", "RIGHT", "DROP", "SOFT", "DROP"};

    det_seed(0);
    init_shapes();

    printf("SEED %u\n", DET_SEED);
    for (int p = 0; p < NUM_PIECES; p++)
    {
        for (int r = 0; r < NUM_ROTS; r++)
        {
            printf("SHAPE %d %d ", p, r);
            for (int i = 0; i < BOX; i++)
            {
                for (int j = 0; j < BOX; j++)
                {
                    printf("%d", shape[p][r][i][j] ? 1 : 0);
                }
            }
            printf("\n");
        }
    }

    bag_pos = NUM_PIECES;
    printf("BAG ");
    for (int i = 0; i < 2 * NUM_PIECES; i++)
    {
        printf("%s%d", i ? "," : "", next_piece());
    }
    printf("\n");

    memset(&g, 0, sizeof g);
    g.mode = &MODES[0];
    g.level = MODES[0].start_level;
    g.running = 1;
    bag_pos = NUM_PIECES;
    g.next = next_piece();
    spawn(&g);
    print_state("START", &g);

    for (unsigned i = 0; i < sizeof script / sizeof script[0]; i++)
    {
        handle_input(&g, script[i], 0);
        print_state(names[i], &g);
    }

    memset(g.board, 0, sizeof g.board);
    for (int c = 0; c < BOARD_W; c++)
    {
        g.board[BOARD_H - 1][c] = 1;
    }
    clear_lines(&g);
    printf("CLEAR1 score=%d lines=%d level=%d ", g.score, g.lines, g.level);
    print_board(&g);

    g.lines = 9;
    for (int c = 0; c < BOARD_W; c++)
    {
        g.board[BOARD_H - 1][c] = 1;
    }
    clear_lines(&g);
    printf("CLEAR2 score=%d lines=%d level=%d ", g.score, g.lines, g.level);
    print_board(&g);

    for (int level = 1; level <= 12; level++)
    {
        g.level = level;
        printf("GRAV %d %d\n", level, drop_interval_ms(&g));
    }
    return 0;
}
