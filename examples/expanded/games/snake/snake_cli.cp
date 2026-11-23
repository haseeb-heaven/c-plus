/* snake_cli.cp -- deterministic CLI driver for snake_core.cp.
 *
 * Usage:
 *   snake_cli <growth|wall|self> [direction-sequence ...]
 *
 * Each character of every remaining argument is one bounded step:
 *   U/u/W/w = up, D/d/S/s = down, L/l/A/a = left, R/r = right.
 * If no direction arguments are given, a bounded sequence is read from stdin.
 *
 * The driver prints an INIT line, a board-state transcript after every step,
 * and a final SCENARIO line with the score and whether the expected outcome
 * was reached. It exits 0 only when the scenario's expected outcome matches.
 * Steps are bounded (SNAKE_MAX_STEPS / MAX_DIRS), so this never loops on input.
 *
 * Build:
 *   ./build/c+ build examples/expanded/games/snake/snake_cli.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/games/snake/.build/snake_cli
 */
#define SNAKE_CORE_EMBEDDED 1
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "snake_core.cp"

#include <stdio.h>

#define MAX_DIRS 256
#define SEED 12345u

enum { SC_GROWTH = 0, SC_WALL = 1, SC_SELF = 2 };

/* argv is an FFI pointer: copy the slot so the strict checker can prove the
 * read, then use the resulting C string normally. */
static const char *cli_arg(char **p, size_t idx) {
    const char *value = NULL;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static int parse_dir(char ch, int *out) {
    if (ch == 'U' || ch == 'u' || ch == 'W' || ch == 'w') {
        *out = SNAKE_UP;
        return 0;
    }
    if (ch == 'D' || ch == 'd' || ch == 'S' || ch == 's') {
        *out = SNAKE_DOWN;
        return 0;
    }
    if (ch == 'L' || ch == 'l' || ch == 'A' || ch == 'a') {
        *out = SNAKE_LEFT;
        return 0;
    }
    if (ch == 'R' || ch == 'r') {
        *out = SNAKE_RIGHT;
        return 0;
    }
    return -1;
}

static void print_board(SnakeCore *c, char *buf, size_t cap) {
    if (snake_core_render(c, buf, cap) == 0)
        fputs(buf, stdout);
    else
        fputs("<render failed>\n", stdout);
}

int main(int argc, char **argv) {
    SnakeCore c;
    char board[SNAKE_RENDER_CAP];
    char dirs[MAX_DIRS];
    int ndirs = 0;
    int scenario = SC_GROWTH;
    const char *scenario_name = "growth";
    int start = 1;
    int food_x = 11, food_y = 10;
    int expected_score = 1, expected_over = 0, expected_reason = SNAKE_REASON_NONE;
    int i, k, step, ok = 0;

    if (argc > 1) {
        const char *s = cli_arg(argv, 1);
        if (s[0] == 'g' || s[0] == 'G') {
            scenario = SC_GROWTH;
            scenario_name = "growth";
            food_x = 11; food_y = 10;
            expected_score = 1; expected_over = 0;
            expected_reason = SNAKE_REASON_NONE;
        } else if (s[0] == 'w' || s[0] == 'W') {
            scenario = SC_WALL;
            scenario_name = "wall";
            food_x = 0; food_y = 0;
            expected_score = 0; expected_over = 1;
            expected_reason = SNAKE_REASON_WALL;
        } else if (s[0] == 's' || s[0] == 'S') {
            scenario = SC_SELF;
            scenario_name = "self";
            food_x = 0; food_y = 0;
            expected_score = 0; expected_over = 1;
            expected_reason = SNAKE_REASON_SELF;
        } else {
            fprintf(stderr, "usage: snake_cli <growth|wall|self> [directions]\n");
            return 2;
        }
        start = 2;
    }

    /* Directions from argv, else a bounded read from stdin. */
    for (i = start; i < argc && ndirs < MAX_DIRS; i++) {
        const char *token = cli_arg(argv, (size_t)i);
        for (k = 0; token[k] != '\0' && ndirs < MAX_DIRS; k++) {
            int dir = 0;
            if (parse_dir(token[k], &dir) != 0) {
                fprintf(stderr, "ignoring unknown direction '%c'\n", token[k]);
                continue;
            }
            dirs[ndirs++] = dir;
        }
    }
    if (start >= argc) {
        int ch;
        while (ndirs < MAX_DIRS && (ch = getchar()) != EOF) {
            int dir = 0;
            if (parse_dir((char)ch, &dir) == 0)
                dirs[ndirs++] = dir;
        }
    }

    snake_core_init(&c, SEED);
    snake_core_set_food(&c, food_x, food_y);
    (void)scenario; /* scenario is encoded in the setup values above */

    printf("INIT scenario %s seed %u len %u\n", scenario_name, SEED,
           (unsigned)snake_core_size(&c));
    print_board(&c, board, sizeof(board));

    for (step = 0; step < ndirs && step < SNAKE_MAX_STEPS; step++) {
        int moved = snake_core_step(&c, dirs[step]);
        printf("STEP %d DIR %s MOVED %d SCORE %d OVER %d REASON %s\n",
               step + 1, snake_core_dir_name(dirs[step]), moved ? 0 : 1,
               snake_core_score(&c), snake_core_game_over(&c),
               snake_core_reason_name(snake_core_reason(&c)));
        print_board(&c, board, sizeof(board));
        if (snake_core_game_over(&c))
            break;
    }

    printf("FINAL score %d\n", snake_core_score(&c));
    ok = (snake_core_score(&c) == expected_score &&
          snake_core_game_over(&c) == expected_over &&
          snake_core_reason(&c) == expected_reason) ? 1 : 0;
    printf("SCENARIO %s score %d over %d reason %s ok %d\n",
           scenario_name, snake_core_score(&c), snake_core_game_over(&c),
           snake_core_reason_name(snake_core_reason(&c)), ok);
    snake_core_drop(&c);
    return ok ? 0 : 1;
}
