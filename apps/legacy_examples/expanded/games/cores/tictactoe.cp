/* tictactoe.cp -- deterministic 3x3 tic-tac-toe logic core with a CLI driver.
 *
 * NOT an interactive game.  Two deterministic players alternate turns: player
 * X follows a fixed cell sequence and player O follows another.  With no
 * arguments those built-in sequences are used; arguments override them with a
 * fixed move sequence (cell indices 0..8, row-major).  There is no terminal
 * graphics, opponent AI search, random choice or wall clock.  Identical input
 * always yields byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/games/cores/tictactoe.cp \
 *       --strict --sanitize=none -I. -o build/tictactoe_core
 *
 * Run:
 *   build/tictactoe_core 0 1 4 5 8       # argv tokens (X,O,X,O,X)
 *   build/tictactoe_core 01458           # one compact token
 *   printf '01458\n' | build/tictactoe_core -  # stdin
 *   build/tictactoe_core                 # built-in fixed sequences
 *
 * Exit 0 on valid input (including a completed win/draw), 2 on invalid input
 * or an illegal move (occupied cell / out-of-range index).
 */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define CELLS 9

/* Built-in deterministic plans.  Interleaved: X=0,O=1,X=4,O=5,X=8 -> X wins
 * the main diagonal. */
static const int X_PLAN[5] = {0, 4, 8, 2, 6};
static const int O_PLAN[4] = {1, 5, 3, 7};

static const int LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
    {0, 4, 8}, {2, 4, 6}
};

static const char *LINE_NAMES[8] = {
    "row0", "row1", "row2", "col0", "col1", "col2", "diag-main", "diag-anti"
};

static const char *mark(int player) {
    return player == 1 ? "X" : "O";
}

/* Parse a compact digit sequence such as "01458" into cell indices. */
static int parse_moves(const char *text, vector *out) {
    string s = string(text);
    size_t i;
    size_t n = s.size();
    for (i = 0; i < n; i++) {
        char ch = s.at(i);
        int idx;
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t')
            continue;
        if (ch < '0' || ch > '8')
            return -1;
        idx = (int)(ch - '0');
        out->push_back(idx);
    }
    return 0;
}

class Board {
    int cells[CELLS];
    int played;

    Board(void) {
        int i;
        for (i = 0; i < CELLS; i++)
            this->cells[i] = 0;
        this->played = 0;
    }

    /* 0 on success, -1 for an out-of-range or occupied cell. */
    int place(int index, int player) {
        if (index < 0 || index >= CELLS)
            return -1;
        if (this->cells[index] != 0)
            return -1;
        this->cells[index] = player;
        this->played++;
        return 0;
    }

    /* Winning line index, or -1 for no winner. */
    int winner_line(void) {
        int l;
        for (l = 0; l < 8; l++) {
            int a = LINES[l][0];
            int b = LINES[l][1];
            int c = LINES[l][2];
            if (this->cells[a] != 0 &&
                this->cells[a] == this->cells[b] &&
                this->cells[a] == this->cells[c])
                return l;
        }
        return -1;
    }

    int draw(void) {
        return this->played == CELLS && this->winner_line() < 0;
    }

    int tile(int index) {
        return this->cells[index];
    }

    int count(void) {
        return this->played;
    }

    void render(void) {
        int r;
        int c;
        for (r = 0; r < 3; r++) {
            printf("  ");
            for (c = 0; c < 3; c++) {
                int v = this->cells[r * 3 + c];
                if (c != 0)
                    printf(" | ");
                if (v == 0)
                    putchar('.');
                else if (v == 1)
                    putchar('X');
                else
                    putchar('O');
            }
            printf("\n");
            if (r != 2)
                printf("  --+---+--\n");
        }
    }
};

static void fail(const char *message) {
    error err = error();
    err.set(CPLUS_EXIT_UNSUPPORTED, message);
    fprintf(stderr, "tictactoe: %s\n", err.what());
}

static int read_stdin(vector *out) {
    char buf[512];
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
        return -1;
    return parse_moves(buf, out);
}

int main(int argc, char **argv) {
    vector moves = vector();
    Board board = Board();
    int i;
    int a;
    int player;
    int line;
    int end_step = 0;
    const char *result = "incomplete";
    const char *detail = "none";

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
                fail("invalid cell index in stdin input (expected 0-8)");
                return 2;
            }
        } else {
            for (a = 1; a < argc; a++) {
                const char *arg = NULL;
                __builtin_memcpy(&arg, argv + a, sizeof(arg));
                if (arg == NULL || parse_moves(arg, &moves) != 0) {
                    fail("invalid cell token (expected digits 0-8)");
                    return 2;
                }
            }
        }
    } else {
        for (i = 0; i < 9; i++) {
            int idx = (i & 1) == 0 ? X_PLAN[i >> 1] : O_PLAN[i >> 1];
            moves.push_back(idx);
        }
    }
    if (moves.size() == 0) {
        fail("empty move sequence");
        return 2;
    }
    if (moves.size() > 9) {
        fail("too many moves (board has only 9 cells)");
        return 2;
    }

    print("TICTACTOE players=X:fixed,O:fixed moves=", moves.size());
    print("INITIAL");
    board.render();

    for (i = 0; i < (int)moves.size(); i++) {
        int idx = moves.at(i);
        player = (i & 1) == 0 ? 1 : 2;
        if (board.place(idx, player) != 0) {
            printf("ILLEGAL step=%d player=%s cell=%d\n",
                   i + 1, mark(player), idx);
            fail("illegal move: cell out of range or already occupied");
            return 2;
        }
        printf("STEP %d %s cell=%d pos=(%d,%d)\n",
               i + 1, mark(player), idx, idx / 3, idx - (idx / 3) * 3);
        board.render();

        line = board.winner_line();
        if (line >= 0) {
            result = "win";
            detail = LINE_NAMES[line];
            end_step = i + 1;
            break;
        }
        if (board.draw()) {
            result = "draw";
            detail = "board-full";
            end_step = i + 1;
            break;
        }
    }

    printf("RESULT %s detail=%s step=%d\n", result, detail, end_step);
    printf("FINAL result=%s moves_played=%d x=%d o=%d\n",
           result, board.count(), (board.count() + 1) >> 1, board.count() >> 1);
    return 0;
}
