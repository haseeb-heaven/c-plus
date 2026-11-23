#line 1 "examples/expanded/games/cores/maze.cp"
/* maze.cp -- deterministic fixed-layout maze logic core with a CLI driver.
 *
 * NOT an interactive game: the maze layout is hard-coded, the wall-collision
 * and goal rules are pure functions of the move sequence, and there is no
 * terminal graphics, key loop or wall clock.  Identical input always yields
 * byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/games/cores/maze.cp \
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
#include "cplus/cplus.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-22973-0.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-22973-1.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-22973-2.h"

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
    string s = string_ctor(text);
    size_t i;
    size_t n = string_size(&s);
    for (i = 0; i < n; i++) {
        char ch = string_at(&s, i);
        int code;
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t')
            continue;
        code = dir_code(ch);
        if (code < 0)
            { __typeof__(- 1) __cplus_ret = (- 1); string_drop(&s);  return __cplus_ret;; }
        vector_push_back(out, code);
    }
    { __typeof__(0) __cplus_ret = (0); string_drop(&s);  return __cplus_ret;; }
string_drop(&s); 
#line 86 "examples/expanded/games/cores/maze.cp"
}

/* Cell kind stored in the flattened grid. */
#define CELL_FLOOR 0
#define CELL_WALL 1
#define CELL_START 2
#define CELL_GOAL 3

struct Maze {
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
};
typedef struct Maze Maze;
#line 105 "examples/expanded/games/cores/maze.cp"
Maze Maze_ctor() {
Maze _cplus_self = {0};
#line 105 "examples/expanded/games/cores/maze.cp"
{
        int r;
        int c;
        _cplus_self.pr = 0;
        _cplus_self.pc = 0;
        _cplus_self.sr = 0;
        _cplus_self.sc = 0;
        _cplus_self.gr = 0;
        _cplus_self.gc = 0;
        _cplus_self.steps = 0;
        _cplus_self.blocked = 0;
        _cplus_self.goal = 0;
        for (r = 0; r < MAZE_H; r++) {
            const char *layout_row = NULL;
            __builtin_memcpy(&layout_row, g_layout + r, sizeof(layout_row));
            string srow = string_ctor(layout_row);
            for (c = 0; c < MAZE_W; c++) {
                char ch = srow.at(c);
                int kind = CELL_FLOOR;
                if (ch == '#')
                    kind = CELL_WALL;
                else if (ch == 'S')
                    kind = CELL_START;
                else if (ch == 'G')
                    kind = CELL_GOAL;
                _cplus_self.cells[r * MAZE_W + c] = kind;
                if (kind == CELL_START) {
                    _cplus_self.sr = r;
                    _cplus_self.sc = c;
                }
                if (kind == CELL_GOAL) {
                    _cplus_self.gr = r;
                    _cplus_self.gc = c;
                }
            }
        string_drop(&srow); 
#line 146 "examples/expanded/games/cores/maze.cp"
}
        _cplus_self.pr = _cplus_self.sr;
        _cplus_self.pc = _cplus_self.sc;
    }
return _cplus_self;
}
#line 145 "examples/expanded/games/cores/maze.cp"
int Maze_kind_at(Maze *self, int r , int c)
#line 145 "examples/expanded/games/cores/maze.cp"
{
        if (r < 0 || r >= MAZE_H || c < 0 || c >= MAZE_W)
            return CELL_WALL;
        return self->cells[r * MAZE_W + c];
    }
#line 152 "examples/expanded/games/cores/maze.cp"
int Maze_step(Maze *self, int dir , int * status)
#line 152 "examples/expanded/games/cores/maze.cp"
{
        int nr = self->pr;
        int nc = self->pc;
        int kind;
        if (dir == DIR_UP)
            nr = self->pr - 1;
        else if (dir == DIR_DOWN)
            nr = self->pr + 1;
        else if (dir == DIR_LEFT)
            nc = self->pc - 1;
        else
            nc = self->pc + 1;
        kind = Maze_kind_at(self, nr, nc);
        if (kind == CELL_WALL) {
            self->blocked++;
            *status = 0;
            return 0;
        }
        self->pr = nr;
        self->pc = nc;
        self->steps++;
        if (kind == CELL_GOAL) {
            self->goal = 1;
            *status = 2;
        } else {
            *status = 1;
        }
        return 0;
    }
#line 182 "examples/expanded/games/cores/maze.cp"
void Maze_render(Maze *self)
#line 182 "examples/expanded/games/cores/maze.cp"
{
        int r;
        int c;
        for (r = 0; r < MAZE_H; r++) {
            printf("  ");
            for (c = 0; c < MAZE_W; c++) {
                char ch;
                if (r == self->pr && c == self->pc) {
                    ch = '@';
                } else {
                    int kind = self->cells[r * MAZE_W + c];
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
#line 208 "examples/expanded/games/cores/maze.cp"
int Maze_start_r(Maze *self)
#line 208 "examples/expanded/games/cores/maze.cp"
{ return self->sr; }
#line 209 "examples/expanded/games/cores/maze.cp"
int Maze_start_c(Maze *self)
#line 209 "examples/expanded/games/cores/maze.cp"
{ return self->sc; }
#line 210 "examples/expanded/games/cores/maze.cp"
int Maze_goal_r(Maze *self)
#line 210 "examples/expanded/games/cores/maze.cp"
{ return self->gr; }
#line 211 "examples/expanded/games/cores/maze.cp"
int Maze_goal_c(Maze *self)
#line 211 "examples/expanded/games/cores/maze.cp"
{ return self->gc; }
#line 212 "examples/expanded/games/cores/maze.cp"
int Maze_row(Maze *self)
#line 212 "examples/expanded/games/cores/maze.cp"
{ return self->pr; }
#line 213 "examples/expanded/games/cores/maze.cp"
int Maze_col(Maze *self)
#line 213 "examples/expanded/games/cores/maze.cp"
{ return self->pc; }
#line 214 "examples/expanded/games/cores/maze.cp"
int Maze_step_count(Maze *self)
#line 214 "examples/expanded/games/cores/maze.cp"
{ return self->steps; }
#line 215 "examples/expanded/games/cores/maze.cp"
int Maze_block_count(Maze *self)
#line 215 "examples/expanded/games/cores/maze.cp"
{ return self->blocked; }
#line 216 "examples/expanded/games/cores/maze.cp"
int Maze_reached(Maze *self)
#line 216 "examples/expanded/games/cores/maze.cp"
{ return self->goal; }
#line 219 "examples/expanded/games/cores/maze.cp"


static void fail(const char *message) {
    error err = error_ctor();
    error_set(&err, CPLUS_EXIT_UNSUPPORTED, message);
    fprintf(stderr, "maze: %s\n", error_what(&err));
}

static int read_stdin(vector *out) {
    char buf[512];
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
        return -1;
    return parse_moves(buf, out);
}

int main(int argc, char **argv) {
    vector moves = vector_ctor();
    Maze maze = Maze_ctor();
    int i;
    int a;
    int status = 0;
    int stopped_early = 0;
    int stop_step = 0;

    if (argc > 1) {
        const char *first = NULL;
        __builtin_memcpy(&first, argv + 1, sizeof(first));
        string first_str = string_ctor(first);
        if (string_equals(&first_str, "-")) {
            if (argc != 2) {
                fail("' - ' must be the only argument when reading stdin");
                { __typeof__(2) __cplus_ret = (2); string_drop(&first_str); vector_drop(&moves);  return __cplus_ret;; }
            }
            if (read_stdin(&moves) != 0) {
                fail("invalid direction in stdin input");
                { __typeof__(2) __cplus_ret = (2); string_drop(&first_str); vector_drop(&moves);  return __cplus_ret;; }
            }
        } else {
            for (a = 1; a < argc; a++) {
                const char *arg = NULL;
                __builtin_memcpy(&arg, argv + a, sizeof(arg));
                if (arg == NULL || parse_moves(arg, &moves) != 0) {
                    fail("invalid direction token (expected U/D/L/R)");
                    { __typeof__(2) __cplus_ret = (2); string_drop(&first_str); vector_drop(&moves);  return __cplus_ret;; }
                }
            }
        }
    string_drop(&first_str); 
#line 304 "examples/expanded/games/cores/maze.cp"
} else {
        if (parse_moves(DEFAULT_MOVES, &moves) != 0) {
            fail("internal default sequence is invalid");
            { __typeof__(2) __cplus_ret = (2); vector_drop(&moves);  return __cplus_ret;; }
        }
    }
    if (vector_size(&moves) == 0) {
        fail("empty move sequence");
        { __typeof__(2) __cplus_ret = (2); vector_drop(&moves);  return __cplus_ret;; }
    }

    printf("MAZE %dx%d start=(%d,%d) goal=(%d,%d)\n",
           MAZE_W, MAZE_H, Maze_start_r(&maze), Maze_start_c(&maze),
           Maze_goal_r(&maze), Maze_goal_c(&maze));
    print("INITIAL");
    Maze_render(&maze);

    for (i = 0; i < (int)vector_size(&moves); i++) {
        int dir = vector_at(&moves, i);
        Maze_step(&maze, dir, &status);
        if (status == 0)
            printf("STEP %d %s blocked pos=(%d,%d)\n",
                   i + 1, dir_name(dir), Maze_row(&maze), Maze_col(&maze));
        else if (status == 1)
            printf("STEP %d %s moved pos=(%d,%d)\n",
                   i + 1, dir_name(dir), Maze_row(&maze), Maze_col(&maze));
        else
            printf("STEP %d %s GOAL pos=(%d,%d)\n",
                   i + 1, dir_name(dir), Maze_row(&maze), Maze_col(&maze));
        Maze_render(&maze);
        if (status == 2) {
            stopped_early = 1;
            stop_step = i + 1;
            break;
        }
    }

    if (Maze_reached(&maze)) {
        printf("GOAL step=%d pos=(%d,%d)\n",
               stop_step, Maze_row(&maze), Maze_col(&maze));
    } else {
        print("NO_GOAL");
    }
    printf("FINAL steps=%d blocked=%d goal=%d pos=(%d,%d) moves=%d\n",
           Maze_step_count(&maze), Maze_block_count(&maze), Maze_reached(&maze),
           Maze_row(&maze), Maze_col(&maze), (int)vector_size(&moves));
    if (stopped_early)
        printf("STOP reason=goal at step=%d\n", stop_step);
    { __typeof__(0) __cplus_ret = (0); vector_drop(&moves);  return __cplus_ret;; }
vector_drop(&moves); 
#line 353 "examples/expanded/games/cores/maze.cp"
}