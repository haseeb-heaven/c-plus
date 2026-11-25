/* game2048.cp -- deterministic 4x4 2048 logic core with a CLI driver.
 *
 * This is NOT an interactive game.  There is no terminal graphics, no keyboard
 * loop and no wall clock.  A fixed-seed xorshift64* generator supplies every
 * spawned tile, and the move sequence comes from argv or stdin.  The same
 * input always produces byte-identical stdout, so the core can be used as a
 * regression fixture.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/games/cores/game2048.cp \
 *       --strict --sanitize=none -I. -o build/game2048_core
 *
 * Run:
 *   build/game2048_core R D L U R D        # moves as argv tokens
 *   build/game2048_core RDLURD             # one compact token
 *   printf 'RDLURD\n' | build/game2048_core -   # moves from stdin
 *   build/game2048_core                    # built-in default sequence
 *
 * Exit 0 on valid input, 2 on invalid input.
 */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

/* Direction codes. */
#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_RIGHT 3

#define DEFAULT_MOVES "RDLURDLUDD"

/* The one and only seed: no time(), no getrandom(), no address entropy. */
static unsigned long long g_rng = 0x9E3779B97F4A7C15ULL;

static void rng_reseed(unsigned long long seed) {
    g_rng = seed;
}

static unsigned long long rnext(void) {
    unsigned long long x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return x * 2685821657736338717ULL;
}

/* Uniform index in [0, n), n > 0, via multiply-shift of the high 32 bits.
 * Avoids a variable divisor (the strict checker rejects those). */
static size_t rbelow(unsigned long long r, size_t n) {
    unsigned long long hi = r >> 32;
    return (size_t)((hi * (unsigned long long)n) >> 32);
}

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

/* Parse a compact sequence such as "RDlu" into direction codes.  Whitespace
 * is skipped; every other character must be a direction. */
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

class Board2048 {
    int cells[16];
    int score;
    int spawns;

    Board2048(void) {
        int i;
        for (i = 0; i < 16; i++)
            this->cells[i] = 0;
        this->score = 0;
        this->spawns = 0;
    }

    /* Spawn one tile in a uniformly chosen empty cell.  2 with p=9/10, 4
     * with p=1/10, exactly as in the retail 2048 rule.  Returns 1 when a
     * tile was placed, 0 when the board was full. */
    int spawn(int *out_index, int *out_value) {
        int empty[16];
        int n = 0;
        int i;
        size_t pick;
        int value;
        for (i = 0; i < 16; i++) {
            if (this->cells[i] == 0) {
                empty[n] = i;
                n++;
            }
        }
        if (n == 0)
            return 0;
        pick = rbelow(rnext(), (size_t)n);
        value = rbelow(rnext(), 10) == 0 ? 4 : 2;
        this->cells[empty[pick]] = value;
        this->spawns++;
        if (out_index != NULL)
            *out_index = empty[pick];
        if (out_value != NULL)
            *out_value = value;
        return 1;
    }

    /* Traversal order for line `line` under direction `dir`: position j maps
     * to a board index.  LEFT/RIGHT walk rows; UP/DOWN walk columns. */
    int traverse_index(int dir, int line, int j) {
        if (dir == DIR_LEFT)
            return line * 4 + j;
        if (dir == DIR_RIGHT)
            return line * 4 + (3 - j);
        if (dir == DIR_UP)
            return j * 4 + line;
        return (3 - j) * 4 + line;
    }

    /* Apply a move.  Returns 0 on success; *changed, *gained and the spawn
     * report are written for the transcript. */
    int move(int dir, int *changed, int *gained,
             int *spawned, int *spawn_r, int *spawn_c, int *spawn_v) {
        int before[16];
        int gain = 0;
        int i;
        int line;
        int j;
        for (i = 0; i < 16; i++)
            before[i] = this->cells[i];

        for (line = 0; line < 4; line++) {
            int vals[4];
            int packed[4];
            int merged[4];
            int k = 0;
            int m = 0;
            for (j = 0; j < 4; j++)
                vals[j] = this->cells[this->traverse_index(dir, line, j)];
            /* Compress non-zero tiles toward the target edge. */
            for (j = 0; j < 4; j++) {
                if (vals[j] != 0) {
                    packed[k] = vals[j];
                    k++;
                }
            }
            /* Merge each pair at most once, then compact again. */
            j = 0;
            while (j < k) {
                if (j + 1 < k && packed[j] == packed[j + 1]) {
                    merged[m] = packed[j] + packed[j + 1];
                    gain += merged[m];
                    j += 2;
                } else {
                    merged[m] = packed[j];
                    j += 1;
                }
                m++;
            }
            for (; m < 4; m++)
                merged[m] = 0;
            for (j = 0; j < 4; j++)
                this->cells[this->traverse_index(dir, line, j)] = merged[j];
        }

        *changed = 0;
        for (i = 0; i < 16; i++) {
            if (before[i] != this->cells[i]) {
                *changed = 1;
                break;
            }
        }
        *gained = gain;
        this->score += gain;

        *spawned = 0;
        if (*changed) {
            int idx = 0;
            int value = 0;
            if (this->spawn(&idx, &value)) {
                *spawned = 1;
                *spawn_r = idx / 4;
                *spawn_c = idx - (idx / 4) * 4;
                *spawn_v = value;
            }
        }
        return 0;
    }

    void render(void) {
        int r;
        int c;
        for (r = 0; r < 4; r++) {
            printf("  ");
            for (c = 0; c < 4; c++) {
                if (c != 0)
                    printf(" ");
                printf("%4d", this->cells[r * 4 + c]);
            }
            printf("\n");
        }
    }

    int total(void) {
        return this->score;
    }

    int tile(int index) {
        return this->cells[index];
    }

    int spawn_count(void) {
        return this->spawns;
    }
};

static void fail(const char *message) {
    error err = error();
    err.set(CPLUS_EXIT_UNSUPPORTED, message);
    fprintf(stderr, "game2048: %s\n", err.what());
}

static int read_stdin(vector *out) {
    char buf[512];
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
        return -1;
    return parse_moves(buf, out);
}

int main(int argc, char **argv) {
    vector moves = vector();
    Board2048 board = Board2048();
    int i;
    int a;
    int changed = 0;
    int gained = 0;
    int spawned = 0;
    int spawn_r = 0;
    int spawn_c = 0;
    int spawn_v = 0;
    int total_changed = 0;
    int noop = 0;

    rng_reseed(0x9E3779B97F4A7C15ULL);

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

    board.spawn(NULL, NULL);
    board.spawn(NULL, NULL);

    print("GAME2048 seed=0x9E3779B97F4A7C15 moves=", moves.size());
    print("START score=0");
    board.render();

    for (i = 0; i < (int)moves.size(); i++) {
        int dir = moves.at(i);
        board.move(dir, &changed, &gained, &spawned,
                   &spawn_r, &spawn_c, &spawn_v);
        if (changed) {
            total_changed++;
            printf("MOVE %d %s changed=1 gained=%d score=%d spawn=",
                   i + 1, dir_name(dir), gained, board.total());
            if (spawned)
                printf("r%dc%d:%d\n", spawn_r, spawn_c, spawn_v);
            else
                printf("none\n");
        } else {
            noop++;
            printf("MOVE %d %s changed=0 gained=0 score=%d spawn=none\n",
                   i + 1, dir_name(dir), board.total());
        }
        board.render();
    }

    print("FINAL score=", board.total(), "moves=", moves.size(),
          "effective=", total_changed, "noops=", noop,
          "spawns=", board.spawn_count());
    print("FINAL_BOARD");
    board.render();
    return 0;
}
