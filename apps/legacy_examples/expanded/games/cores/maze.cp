/* maze.cp -- deterministic fixed-layout maze logic core with a CLI driver.
 *
 * NOT an interactive game: the maze layout is hard-coded, the wall-collision
 * and goal rules are pure functions of the move sequence, and there is no
 * terminal graphics, key loop or wall clock.  Identical input always yields
 * byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/games/cores/maze.cp \
 *       --strict --sanitize=none -I. -o build/maze_core
 *
 * Run:
 *   build/maze_core U D D D D R R R R R      # argv tokens
 *   build/maze_core UDDDDRRRRR               # one compact token
 *   printf 'UDDDDRRRRR\n' | build/maze_core -  # stdin
 *   build/maze_core                          # built-in default sequence
 *
 * Exit 0 on valid input, 2 on invalid input.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define MAZE_W 9
#define MAZE_H 7
#define MAZE_N (MAZE_W * MAZE_H)
#define DEFAULT_MOVES "UDDDDRRRRR"

/* Fixed layout: # wall, . floor, S start, G goal.  No randomness. */
static const char *g_layout[MAZE_H] = {
    "#########",
    "#S..#...#",
    "#.#.#.#.#",
    "#.#...#.#",
    "#.#####.#",
    "#.....G.#",
    "#########"
};

#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_RIGHT 3

static int dir_code(char ch) {
    if (ch == 'U' || ch == 'u')
        return DIR_UP;
    if (ch == 'D' || ch == 'd')
        return DIR_DOWN;
    if (ch == 'L' || ch == 'l')
        return DIR_LEFT;
    if (ch == 'R' || ch == 'r')
        return DIR_RIGHT;
    return -1;
}

static const char *dir_name(int code) {
    if (code == DIR_UP)
        return "U";
    if (code == DIR_DOWN)
        return "D";
    if (code == DIR_LEFT)
        return "L";
    return "R";
}

static int parse_moves(const char *text, vector *out) {
    string s = string(text);
    size_t i;
    size_t n = s.size();
    for (i = 0; i < n; i++) {
        char ch = s.at(i);
        int code;
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t')
            continue;
        code = dir_code(ch);
        if (code < 0)
            return -1;
        out->push_back(code);
    }
    return 0;
}

/* Cell kind stored in the flattened grid. */
#define CELL_FLOOR 0
#define CELL_WALL 1
#define CELL_START 2
#define CELL_GOAL 3

class Maze {
    int cells[MAZE_N];
    int pr;
    int pc;
    int sr;
    int sc;
    int gr;
    int gc;
    int steps;
    int blocked;
    int goal;

    Maze(void) {
        int r;
        int c;
        this->pr = 0;
        this->pc = 0;
        this->sr = 0;
        this->sc = 0;
        this->gr = 0;
        this->gc = 0;
        this->steps = 0;
        this->blocked = 0;
        this->goal = 0;
        for (r = 0; r < MAZE_H; r++) {
            const char *layout_row = NULL;
            char rowbuf[MAZE_W + 1];
            size_t rowlen;
            __builtin_memcpy(&layout_row, g_layout + r, sizeof(layout_row));
            rowlen = strlen(layout_row);
            if (rowlen > MAZE_W)
                rowlen = MAZE_W;
            __builtin_memcpy(rowbuf, layout_row, rowlen);
            rowbuf[rowlen] = '\0';
            for (c = 0; c < MAZE_W; c++) {
                char ch = rowbuf[c];
                int kind = CELL_FLOOR;
                if (ch == '#')
                    kind = CELL_WALL;
                else if (ch == 'S')
                    kind = CELL_START;
                else if (ch == 'G')
                    kind = CELL_GOAL;
                this->cells[r * MAZE_W + c] = kind;
                if (kind == CELL_START) {
                    this->sr = r;
                    this->sc = c;
                }
                if (kind == CELL_GOAL) {
                    this->gr = r;
                    this->gc = c;
                }
            }
        }
        this->pr = this->sr;
        this->pc = this->sc;
    }

    int kind_at(int r, int c) {
        if (r < 0 || r >= MAZE_H || c < 0 || c >= MAZE_W)
            return CELL_WALL;
        return this->cells[r * MAZE_W + c];
    }

    /* status: 0 blocked, 1 moved, 2 goal reached. */
    int step(int dir, int *status) {
        int nr = this->pr;
        int nc = this->pc;
        int kind;
        if (dir == DIR_UP)
            nr = this->pr - 1;
        else if (dir == DIR_DOWN)
            nr = this->pr + 1;
        else if (dir == DIR_LEFT)
            nc = this->pc - 1;
        else
            nc = this->pc + 1;
        kind = this->kind_at(nr, nc);
        if (kind == CELL_WALL) {
            this->blocked++;
            *status = 0;
            return 0;
        }
        this->pr = nr;
        this->pc = nc;
        this->steps++;
        if (kind == CELL_GOAL) {
            this->goal = 1;
            *status = 2;
        } else {
            *status = 1;
        }
        return 0;
    }

    void render(void) {
        int r;
        int c;
        for (r = 0; r < MAZE_H; r++) {
            printf("  ");
            for (c = 0; c < MAZE_W; c++) {
                char ch;
                if (r == this->pr && c == this->pc) {
                    ch = '@';
                } else {
                    int kind = this->cells[r * MAZE_W + c];
                    if (kind == CELL_WALL)
                        ch = '#';
                    else if (kind == CELL_START)
                        ch = 'S';
                    else if (kind == CELL_GOAL)
                        ch = 'G';
                    else
                        ch = '.';
                }
                putchar(ch);
            }
            printf("\n");
        }
    }

    int start_r(void) { return this->sr; }
    int start_c(void) { return this->sc; }
    int goal_r(void) { return this->gr; }
    int goal_c(void) { return this->gc; }
    int row(void) { return this->pr; }
    int col(void) { return this->pc; }
    int step_count(void) { return this->steps; }
    int block_count(void) { return this->blocked; }
    int reached(void) { return this->goal; }
};

static void fail(const char *message) {
    error err = error();
    err.set(CPLUS_EXIT_UNSUPPORTED, message);
    fprintf(stderr, "maze: %s\n", err.what());
}

static int read_stdin(vector *out) {
    char buf[512];
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
        return -1;
    return parse_moves(buf, out);
}

int main(int argc, char **argv) {
    vector moves = vector();
    Maze maze = Maze();
    int i;
    int a;
    int status = 0;
    int stopped_early = 0;
    int stop_step = 0;

    if (argc > 1) {
        const char *first = NULL;
        __builtin_memcpy(&first, argv + 1, sizeof(first));
        string first_str = string(first);
        if (first_str.equals("-")) {
            if (argc != 2) {
                fail("' - ' must be the only argument when reading stdin");
                return 2;
            }
            if (read_stdin(&moves) != 0) {
                fail("invalid direction in stdin input");
                return 2;
            }
        } else {
            for (a = 1; a < argc; a++) {
                const char *arg = NULL;
                __builtin_memcpy(&arg, argv + a, sizeof(arg));
                if (arg == NULL || parse_moves(arg, &moves) != 0) {
                    fail("invalid direction token (expected U/D/L/R)");
                    return 2;
                }
            }
        }
    } else {
        if (parse_moves(DEFAULT_MOVES, &moves) != 0) {
            fail("internal default sequence is invalid");
            return 2;
        }
    }
    if (moves.size() == 0) {
        fail("empty move sequence");
        return 2;
    }

    printf("MAZE %dx%d start=(%d,%d) goal=(%d,%d)\n",
           MAZE_W, MAZE_H, maze.start_r(), maze.start_c(),
           maze.goal_r(), maze.goal_c());
    print("INITIAL");
    maze.render();

    for (i = 0; i < (int)moves.size(); i++) {
        int dir = moves.at(i);
        maze.step(dir, &status);
        if (status == 0)
            printf("STEP %d %s blocked pos=(%d,%d)\n",
                   i + 1, dir_name(dir), maze.row(), maze.col());
        else if (status == 1)
            printf("STEP %d %s moved pos=(%d,%d)\n",
                   i + 1, dir_name(dir), maze.row(), maze.col());
        else
            printf("STEP %d %s GOAL pos=(%d,%d)\n",
                   i + 1, dir_name(dir), maze.row(), maze.col());
        maze.render();
        if (status == 2) {
            stopped_early = 1;
            stop_step = i + 1;
            break;
        }
    }

    if (maze.reached()) {
        printf("GOAL step=%d pos=(%d,%d)\n",
               stop_step, maze.row(), maze.col());
    } else {
        print("NO_GOAL");
    }
    printf("FINAL steps=%d blocked=%d goal=%d pos=(%d,%d) moves=%d\n",
           maze.step_count(), maze.block_count(), maze.reached(),
           maze.row(), maze.col(), (int)moves.size());
    if (stopped_early)
        printf("STOP reason=goal at step=%d\n", stop_step);
    return 0;
}
