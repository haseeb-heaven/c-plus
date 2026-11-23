/* snake_core.cp -- deterministic, testable Snake game core (C+ / C+ stdlib).
 *
 * This is a MODEL of the game LOGIC, not the interactive terminal game.
 * There is no terminal I/O, no wall clock and no randomness from time():
 * the board is fixed-size (20x20), food comes from a fixed-seed LCG, and the
 * human-readable board is rendered into a caller-provided character buffer.
 * The entire state of a game is the SnakeCore struct, so any direction
 * sequence produces the same transcript on every run.
 *
 * The C+ stdlib container/errors are used for the two things that can be
 * refused: the snake body is a `vector` (stdlib/vector.hp) and refusals are
 * recorded in an `error` (stdlib/error.hp). All calls use the C names that
 * the c+ driver lowers those .hp headers to (vector_ctor, vector_insert,
 * error_set_category, ...). That keeps this file valid C when it is included
 * by snake_cli.cp, and valid C+ when it is built on its own.
 *
 * Build standalone (self-test):
 *   ./build/c+ build examples/expanded/games/snake/snake_core.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/games/snake/.build/snake_core
 *
 * The interactive game this models lives at examples/games/snake.c.
 */
#ifndef SNAKE_CORE_EMBEDDED
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#endif
#include <stddef.h>

#define SNAKE_W 20
#define SNAKE_H 20
#define SNAKE_CELLS (SNAKE_W * SNAKE_H)
#define SNAKE_INITIAL_LEN 5
#define SNAKE_MAX_STEPS 256
#define SNAKE_RENDER_CAP ((SNAKE_H * (SNAKE_W + 1)) + 1)

/* Directions and terminal game-over reasons. */
enum {
    SNAKE_UP = 0,
    SNAKE_DOWN = 1,
    SNAKE_LEFT = 2,
    SNAKE_RIGHT = 3
};
enum {
    SNAKE_REASON_NONE = 0,
    SNAKE_REASON_WALL = 1,
    SNAKE_REASON_SELF = 2,
    SNAKE_REASON_FULL = 3
};

typedef struct {
    vector body;       /* packed cells; index 0 is the head */
    int dir;
    int food;          /* packed cell, or -1 when there is none */
    int score;
    int game_over;
    int reason;
    int steps;
    unsigned int rng;  /* LCG state, seeded explicitly */
    error err;         /* last refusal, if any */
} SnakeCore;

/* ---- pure coordinate helpers (no I/O, no clock) ------------------------- */

static int snake_pack(int x, int y) { return y * SNAKE_W + x; }
static int snake_cell_x(int cell) { return cell % SNAKE_W; }
static int snake_cell_y(int cell) { return cell / SNAKE_W; }

static unsigned int snake_next_rand(SnakeCore *c) {
    /* Numerical Recipes LCG: same seed always yields the same food cells. */
    c->rng = c->rng * 1664525u + 1013904223u;
    return c->rng;
}

static int snake_occupied(SnakeCore *c, int cell) {
    size_t i;
    size_t n = vector_size(&c->body);
    for (i = 0; i < n; i++) {
        int value = 0;
        if (vector_at_checked(&c->body, i, &value) != 0)
            continue;
        if (value == cell)
            return 1;
    }
    return 0;
}

/* ---- public core API ---------------------------------------------------- */

void snake_core_place_food(SnakeCore *c);

void snake_core_init(SnakeCore *c, unsigned int seed) {
    int i;
    c->body = vector_ctor();
    c->dir = SNAKE_RIGHT;
    c->food = -1;
    c->score = 0;
    c->game_over = 0;
    c->reason = SNAKE_REASON_NONE;
    c->steps = 0;
    c->rng = seed;
    c->err = error_ctor();
    /* Head at (10,10); the tail trails to the left. */
    for (i = 0; i < SNAKE_INITIAL_LEN; i++) {
        if (vector_push_back(&c->body, snake_pack(10 - i, 10)) != 0) {
            error_set_category(&c->err, STDLIB_CATEGORY_MEMORY,
                               CPLUS_EXIT_RESOURCE,
                               "snake: cannot allocate initial body");
            c->game_over = 1;
            c->reason = SNAKE_REASON_FULL;
            return;
        }
    }
    snake_core_place_food(c);
}

/* Place food on the first free cell after a fixed-seed PRNG offset. */
void snake_core_place_food(SnakeCore *c) {
    unsigned int start;
    int k;
    start = snake_next_rand(c) % (unsigned int)SNAKE_CELLS;
    for (k = 0; k < SNAKE_CELLS; k++) {
        int cell = (int)(((unsigned int)start + (unsigned int)k) %
                         (unsigned int)SNAKE_CELLS);
        if (!snake_occupied(c, cell)) {
            c->food = cell;
            return;
        }
    }
    c->food = -1;
    c->game_over = 1;
    c->reason = SNAKE_REASON_FULL;
    error_set_category(&c->err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "snake: no free cell for food");
}

/* Deterministic scenario setup: put food at a known cell. */
void snake_core_set_food(SnakeCore *c, int x, int y) {
    if (x < 0 || x >= SNAKE_W || y < 0 || y >= SNAKE_H) {
        error_set_category(&c->err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                           "snake: food cell out of range");
        return;
    }
    if (snake_occupied(c, snake_pack(x, y))) {
        error_set_category(&c->err, STDLIB_CATEGORY_INVALID,
                           CPLUS_EXIT_UNSUPPORTED,
                           "snake: food cannot be placed on the body");
        return;
    }
    c->food = snake_pack(x, y);
}

/* Advance one cell in `dir`. 0 = moved, -1 = refused (see c->err / reason). */
int snake_core_step(SnakeCore *c, int dir) {
    int head = 0;
    int nx, ny, new_head, growing, limit, i;

    if (c->game_over) {
        error_set_category(&c->err, STDLIB_CATEGORY_LIFECYCLE,
                           CPLUS_EXIT_UNSUPPORTED,
                           "snake: step refused, game already over");
        return -1;
    }
    if (dir < SNAKE_UP || dir > SNAKE_RIGHT) {
        error_set_category(&c->err, STDLIB_CATEGORY_INVALID,
                           CPLUS_EXIT_UNSUPPORTED, "snake: invalid direction");
        return -1;
    }
    if ((dir == SNAKE_LEFT && c->dir == SNAKE_RIGHT) ||
        (dir == SNAKE_RIGHT && c->dir == SNAKE_LEFT) ||
        (dir == SNAKE_UP && c->dir == SNAKE_DOWN) ||
        (dir == SNAKE_DOWN && c->dir == SNAKE_UP)) {
        error_set_category(&c->err, STDLIB_CATEGORY_INVALID,
                           CPLUS_EXIT_UNSUPPORTED,
                           "snake: 180-degree reversal refused");
        return -1;
    }
    if (vector_at_checked(&c->body, 0, &head) != 0) {
        error_set_category(&c->err, STDLIB_CATEGORY_LIFECYCLE,
                           CPLUS_EXIT_UNSUPPORTED, "snake: head is missing");
        return -1;
    }
    c->dir = dir;

    nx = snake_cell_x(head);
    ny = snake_cell_y(head);
    if (dir == SNAKE_UP)
        ny--;
    else if (dir == SNAKE_DOWN)
        ny++;
    else if (dir == SNAKE_LEFT)
        nx--;
    else
        nx++;

    if (nx < 0 || nx >= SNAKE_W || ny < 0 || ny >= SNAKE_H) {
        c->game_over = 1;
        c->reason = SNAKE_REASON_WALL;
        error_set_category(&c->err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                           "snake: wall collision");
        return -1;
    }

    new_head = snake_pack(nx, ny);
    growing = (new_head == c->food);

    /* Without food the tail vacates its cell, so it is not an obstacle. */
    limit = (int)vector_size(&c->body);
    if (!growing && limit > 0)
        limit--;
    for (i = 0; i < limit; i++) {
        int segment = 0;
        if (vector_at_checked(&c->body, (size_t)i, &segment) != 0)
            continue;
        if (segment == new_head) {
            c->game_over = 1;
            c->reason = SNAKE_REASON_SELF;
            error_set_category(&c->err, STDLIB_CATEGORY_LIFECYCLE,
                               CPLUS_EXIT_UNSUPPORTED, "snake: self collision");
            return -1;
        }
    }

    if (vector_insert(&c->body, 0, new_head) != 0) {
        error_set_category(&c->err, STDLIB_CATEGORY_MEMORY,
                           CPLUS_EXIT_RESOURCE, "snake: cannot grow body");
        return -1;
    }
    if (growing) {
        c->score++;
        snake_core_place_food(c);
    } else {
        int discarded = 0;
        vector_pop_back(&c->body, &discarded);
    }
    c->steps++;
    return 0;
}

int snake_core_score(const SnakeCore *c) { return c->score; }
int snake_core_game_over(const SnakeCore *c) { return c->game_over; }
int snake_core_reason(const SnakeCore *c) { return c->reason; }
int snake_core_steps(const SnakeCore *c) { return c->steps; }
int snake_core_dir(const SnakeCore *c) { return c->dir; }
int snake_core_food(const SnakeCore *c) { return c->food; }
size_t snake_core_size(SnakeCore *c) { return vector_size(&c->body); }
int snake_core_error_failed(SnakeCore *c) {
    return error_failed_now(&c->err);
}
const char *snake_core_error_what(SnakeCore *c) { return error_what(&c->err); }

/* Render the board into `buf` (no I/O). Returns 0, or -1 if `buf` is too
 * small. Rows are SNAKE_W chars followed by '\n'; a final NUL is written. */
int snake_core_render(SnakeCore *c, char *buf, size_t cap) {
    size_t pos = 0;
    int x, y, i;
    int head = 0;
    size_t n = vector_size(&c->body);
    if (buf == NULL || cap < (size_t)SNAKE_RENDER_CAP)
        return -1;
    if (n > 0)
        vector_at_checked(&c->body, 0, &head);
    for (y = 0; y < SNAKE_H; y++) {
        for (x = 0; x < SNAKE_W; x++) {
            char ch = '.';
            int cell = snake_pack(x, y);
            if (cell == c->food)
                ch = 'F';
            for (i = 1; i < (int)n; i++) {
                int segment = 0;
                if (vector_at_checked(&c->body, (size_t)i, &segment) == 0 &&
                    segment == cell) {
                    ch = 'o';
                    break;
                }
            }
            if (n > 0 && head == cell)
                ch = 'O';
            buf[pos++] = ch;
        }
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return 0;
}

void snake_core_drop(SnakeCore *c) { vector_drop(&c->body); }

const char *snake_core_reason_name(int reason) {
    if (reason == SNAKE_REASON_WALL)
        return "wall";
    if (reason == SNAKE_REASON_SELF)
        return "self";
    if (reason == SNAKE_REASON_FULL)
        return "full";
    return "none";
}

const char *snake_core_dir_name(int dir) {
    if (dir == SNAKE_UP)
        return "U";
    if (dir == SNAKE_DOWN)
        return "D";
    if (dir == SNAKE_LEFT)
        return "L";
    return "R";
}

/* ---- standalone deterministic self-test --------------------------------- */
#ifndef SNAKE_CORE_EMBEDDED
int main(void) {
    SnakeCore c;
    char buf[SNAKE_RENDER_CAP];
    int checks = 0, failed = 0, i;

#define CORE_CHECK(cond)          \
    do {                          \
        checks++;                 \
        if (!(cond))              \
            failed++;             \
    } while (0)

    snake_core_init(&c, 12345u);
    CORE_CHECK(snake_core_size(&c) == 5 && snake_core_score(&c) == 0 &&
               !snake_core_game_over(&c) && !snake_core_error_failed(&c));
    CORE_CHECK(snake_core_render(&c, buf, sizeof(buf)) == 0 &&
               buf[0] == '.' && buf[(20 * 21) - 1] == '\n');

    snake_core_set_food(&c, 11, 10);
    CORE_CHECK(snake_core_step(&c, SNAKE_RIGHT) == 0 &&
               snake_core_score(&c) == 1 && snake_core_size(&c) == 6 &&
               !snake_core_game_over(&c));
    CORE_CHECK(snake_core_step(&c, SNAKE_LEFT) == -1 &&
               snake_core_error_failed(&c) && !snake_core_game_over(&c));
    snake_core_drop(&c);

    snake_core_init(&c, 12345u);
    snake_core_set_food(&c, 0, 0);
    for (i = 0; i < 10 && !snake_core_game_over(&c); i++)
        snake_core_step(&c, SNAKE_RIGHT);
    CORE_CHECK(snake_core_game_over(&c) &&
               snake_core_reason(&c) == SNAKE_REASON_WALL &&
               snake_core_score(&c) == 0);
    snake_core_drop(&c);

    snake_core_init(&c, 12345u);
    snake_core_set_food(&c, 0, 0);
    snake_core_step(&c, SNAKE_RIGHT);
    snake_core_step(&c, SNAKE_UP);
    snake_core_step(&c, SNAKE_LEFT);
    snake_core_step(&c, SNAKE_DOWN);
    CORE_CHECK(snake_core_game_over(&c) &&
               snake_core_reason(&c) == SNAKE_REASON_SELF &&
               snake_core_score(&c) == 0);
    snake_core_drop(&c);

    print("SNAKE_CORE_SELFTEST", checks, failed);
    return failed == 0 ? 0 : 1;
}
#endif
