/* sudoku.cp -- deterministic 9x9 Sudoku logic core with an argv CLI.
 *
 * NOT an interactive game.  There is no terminal graphics, no keyboard loop,
 * no wall clock and no random choice.  A fixed puzzle, a fixed solution and a
 * fixed move program come from static data or argv; identical input always
 * produces byte-identical stdout.
 *
 * This is a teaching model of a Sudoku core with explicit bounds checks, NOT
 * a formal proof of memory safety.  Every refusal below is observed at run
 * time (an error record plus a nonzero exit), not derived by a proof engine.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/games_3/sudoku.cp \
 *       --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games_3/bin/sudoku
 *
 * Run:
 *   bin/sudoku                    # built-in fixed solve sequence
 *   bin/sudoku P 0 2 4 V          # place, then validate (argv tokens)
 *   bin/sudoku R 0 2 V            # remove, then validate
 *
 * Command grammar (argv tokens, consumed left to right):
 *   P r c d   place digit d (1..9) at row r, col c (both 0..8)
 *   R r c     remove a non-given value at row r, col c
 *   V         validate the whole grid (solved or not)
 *
 * Exit 0 on a fully accepted program, 2 on the first refused move.  Refused
 * moves print a REFUSE line with a stable symbolic error code.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define N 9
#define CELLS 81

/* Encoded commands stored in the program vector (operands are parsed ints;
 * these sentinels are negative and are only recognised at command slots). */
#define CMD_PLACE -100
#define CMD_REMOVE -101
#define CMD_VALIDATE -102

/* Stable symbolic refusal codes printed in the transcript. */
#define E_OK 0
#define E_PARSE 1
#define E_FORMAT 2
#define E_BOUNDS 3
#define E_GIVEN 4
#define E_CONFLICT 5
#define E_OCCUPIED 6
#define E_EMPTY 7

/* A well-known 9x9 puzzle: 0 denotes an empty cell. */
static const int GIVEN[CELLS] = {
    5,3,0, 0,7,0, 0,0,0,
    6,0,0, 1,9,5, 0,0,0,
    0,9,8, 0,0,0, 0,6,0,
    8,0,0, 0,6,0, 0,0,3,
    4,0,0, 8,0,3, 0,0,1,
    7,0,0, 0,2,0, 0,0,6,
    0,6,0, 0,0,0, 2,8,0,
    0,0,0, 4,1,9, 0,0,5,
    0,0,0, 0,8,0, 0,7,9
};

/* Fixed, verified solution of GIVEN.  Used only to build the built-in solve
 * program; it is never read during legality checking. */
static const int SOLUTION[CELLS] = {
    5,3,4, 6,7,8, 9,1,2,
    6,7,2, 1,9,5, 3,4,8,
    1,9,8, 3,4,2, 5,6,7,
    8,5,9, 7,6,1, 4,2,3,
    4,2,6, 8,5,3, 7,9,1,
    7,1,3, 9,2,4, 8,5,6,
    9,6,1, 5,3,7, 2,8,4,
    2,8,7, 4,1,9, 6,3,5,
    3,4,5, 2,8,6, 1,7,9
};

static const char *code_name(int code) {
    if (code == E_OK) return "E_OK";
    if (code == E_PARSE) return "E_PARSE";
    if (code == E_FORMAT) return "E_FORMAT";
    if (code == E_BOUNDS) return "E_BOUNDS";
    if (code == E_GIVEN) return "E_GIVEN";
    if (code == E_CONFLICT) return "E_CONFLICT";
    if (code == E_OCCUPIED) return "E_OCCUPIED";
    return "E_EMPTY";
}

static cplus_exit_code category_for(int code) {
    if (code == E_BOUNDS)
        return CPLUS_EXIT_BOUNDS;
    return CPLUS_EXIT_UNSUPPORTED;
}

/* Strict, overflow-aware integer parse.  Returns 0 on success, -1 when the
 * token is malformed, -2 when the magnitude is oversized. */
static int parse_int(const char *arg, int *out) {
    string s = string(arg);
    size_t n = s.size();
    size_t i = 0;
    int negative = 0;
    int value = 0;
    int digits = 0;
    if (n == 0)
        return -1;
    if (s.at(0) == '-') {
        negative = 1;
        i = 1;
    } else if (s.at(0) == '+') {
        i = 1;
    }
    if (i == n)
        return -1;
    for (; i < n; i++) {
        char ch = s.at(i);
        if (ch < '0' || ch > '9')
            return -1;
        /* Guard BEFORE the multiply so an oversized token cannot wrap. */
        if (value > 100000 || digits >= 8)
            return -2;
        value = value * 10 + (ch - '0');
        digits++;
    }
    *out = negative ? -value : value;
    return 0;
}

static void refuse_line(error *e, int step, const char *detail, int code,
                        const char *reason) {
    e->set(category_for(code), reason);
    printf("REFUSE step=%d %s code=%s reason=%s\n",
           step, detail, code_name(code), e->what());
}

static void refuse_arg(error *e, const char *token, int code,
                       const char *reason) {
    char detail[160];
    snprintf(detail, sizeof(detail), "arg=%s", token);
    refuse_line(e, 0, detail, code, reason);
}

class Sudoku {
    int cells[CELLS];
    int given[CELLS];
    int givens;
    int changes;

    Sudoku(void) {
        int i;
        this->givens = 0;
        this->changes = 0;
        for (i = 0; i < CELLS; i++) {
            this->cells[i] = GIVEN[i];
            if (GIVEN[i] != 0) {
                this->given[i] = 1;
                this->givens++;
            } else {
                this->given[i] = 0;
            }
        }
    }

    /* Row/col are checked BEFORE any multiplication or array access. */
    int index_of(int r, int c) {
        if (r < 0 || r >= N || c < 0 || c >= N)
            return -1;
        return r * N + c;
    }

    /* 1 when d may legally occupy idx given the current grid, else 0. */
    int legal_digit(int idx, int d) {
        int r = idx / N;
        int c = idx - (idx / N) * N;
        int br = (r / 3) * 3;
        int bc = (c / 3) * 3;
        int i;
        int dr;
        int dc;
        for (i = 0; i < N; i++) {
            if (this->cells[r * N + i] == d)
                return 0;
            if (this->cells[i * N + c] == d)
                return 0;
        }
        for (dr = 0; dr < 3; dr++) {
            for (dc = 0; dc < 3; dc++) {
                if (this->cells[(br + dr) * N + bc + dc] == d)
                    return 0;
            }
        }
        return 1;
    }

    int place(int r, int c, int d, int *out_code) {
        int idx = this->index_of(r, c);
        if (idx < 0) {
            *out_code = E_BOUNDS;
            return -1;
        }
        if (d < 1 || d > 9) {
            *out_code = E_BOUNDS;
            return -1;
        }
        if (this->given[idx]) {
            *out_code = E_GIVEN;
            return -1;
        }
        if (this->cells[idx] != 0) {
            *out_code = E_OCCUPIED;
            return -1;
        }
        if (this->legal_digit(idx, d) == 0) {
            *out_code = E_CONFLICT;
            return -1;
        }
        this->cells[idx] = d;
        this->changes++;
        *out_code = E_OK;
        return 0;
    }

    int remove(int r, int c, int *out_code) {
        int idx = this->index_of(r, c);
        if (idx < 0) {
            *out_code = E_BOUNDS;
            return -1;
        }
        if (this->given[idx]) {
            *out_code = E_GIVEN;
            return -1;
        }
        if (this->cells[idx] == 0) {
            *out_code = E_EMPTY;
            return -1;
        }
        this->cells[idx] = 0;
        this->changes++;
        *out_code = E_OK;
        return 0;
    }

    /* 1 exactly when every cell holds 1..9 and every row/col/box is a
     * permutation of 1..9.  Uses only in-range linear indices. */
    int solved(void) {
        int i;
        int r;
        int c;
        int dr;
        int dc;
        int seen[10];
        for (i = 0; i < CELLS; i++) {
            if (this->cells[i] < 1 || this->cells[i] > 9)
                return 0;
        }
        for (r = 0; r < N; r++) {
            for (i = 0; i <= 9; i++)
                seen[i] = 0;
            for (c = 0; c < N; c++)
                seen[this->cells[r * N + c]]++;
            for (i = 1; i <= 9; i++) {
                if (seen[i] != 1)
                    return 0;
            }
        }
        for (c = 0; c < N; c++) {
            for (i = 0; i <= 9; i++)
                seen[i] = 0;
            for (r = 0; r < N; r++)
                seen[this->cells[r * N + c]]++;
            for (i = 1; i <= 9; i++) {
                if (seen[i] != 1)
                    return 0;
            }
        }
        for (dr = 0; dr < 3; dr++) {
            for (dc = 0; dc < 3; dc++) {
                for (i = 0; i <= 9; i++)
                    seen[i] = 0;
                for (r = 0; r < 3; r++) {
                    for (c = 0; c < 3; c++) {
                        int idx = (dr * 3 + r) * N + dc * 3 + c;
                        seen[this->cells[idx]]++;
                    }
                }
                for (i = 1; i <= 9; i++) {
                    if (seen[i] != 1)
                        return 0;
                }
            }
        }
        return 1;
    }

    void render(void) {
        int r;
        int c;
        for (r = 0; r < N; r++) {
            printf("  ");
            for (c = 0; c < N; c++) {
                int v = this->cells[r * N + c];
                if (c == 3 || c == 6)
                    printf("| ");
                if (v == 0)
                    putchar('.');
                else
                    printf("%d", v);
                printf(" ");
            }
            printf("\n");
        }
    }

    int change_count(void) {
        return this->changes;
    }
};

/* Parse argv into a program vector.  Returns 0 on success, else a symbolic
 * refusal code with the reason and token outputs filled for the REFUSE line. */
static int parse_program(int argc, char **argv, vector *prog,
                         char *token_out, size_t token_cap,
                         const char **reason_out) {
    int a;
    for (a = 1; a < argc; a++) {
        const char *arg = NULL;
        __builtin_memcpy(&arg, argv + a, sizeof(arg));
        if (arg == NULL) {
            snprintf(token_out, token_cap, "<null>");
            *reason_out = "null argv token";
            return E_PARSE;
        }
        {
            string s = string(arg);
            if (s.size() == 1) {
                char ch = s.at(0);
                if (ch == 'P' || ch == 'p') {
                    prog->push_back(CMD_PLACE);
                    continue;
                }
                if (ch == 'R' || ch == 'r') {
                    prog->push_back(CMD_REMOVE);
                    continue;
                }
                if (ch == 'V' || ch == 'v') {
                    prog->push_back(CMD_VALIDATE);
                    continue;
                }
            }
        }
        {
            int value = 0;
            int pr = parse_int(arg, &value);
            if (pr != 0) {
                snprintf(token_out, token_cap, "%s", arg);
                *reason_out = pr == -2
                    ? "numeric token is oversized"
                    : "numeric token is malformed";
                return E_PARSE;
            }
            if (prog->push_back(value) != 0) {
                snprintf(token_out, token_cap, "%s", arg);
                *reason_out = "program vector refused the token";
                return E_FORMAT;
            }
        }
    }
    return E_OK;
}

int main(int argc, char **argv) {
    Sudoku board = Sudoku();
    vector prog = vector();
    error e = error();
    char token[160];
    const char *reason = "none";
    int parse_code;
    int i;
    int pc = 0;
    int step = 0;
    int refused = 0;
    int final_code = E_OK;
    int solved_now = 0;

    if (argc > 1) {
        parse_code = parse_program(argc, argv, &prog, token, sizeof(token), &reason);
        if (parse_code != E_OK) {
            refuse_arg(&e, token, parse_code, reason);
            printf("SUDOKU_DONE status=REFUSED code=%s\n",
                   code_name(parse_code));
            return 2;
        }
    } else {
        /* Built-in solve program: place every solution value into every empty
         * cell, row-major, then validate.  Fully deterministic. */
        for (i = 0; i < CELLS; i++) {
            if (GIVEN[i] == 0) {
                int r = i / N;
                int c = i - (i / N) * N;
                prog.push_back(CMD_PLACE);
                prog.push_back(r);
                prog.push_back(c);
                prog.push_back(SOLUTION[i]);
            }
        }
        prog.push_back(CMD_VALIDATE);
    }
    if (prog.size() == 0) {
        refuse_line(&e, 0, "program=empty", E_FORMAT, "empty move program");
        printf("SUDOKU_DONE status=REFUSED code=%s\n", code_name(E_FORMAT));
        return 2;
    }

    printf("SUDOKU grid=9x9 givens=%d program=%zu\n",
           board.givens, prog.size());
    print("INITIAL");
    board.render();

    while (pc < (int)prog.size()) {
        int cmd = 0;
        char detail[200];
        int code = E_OK;
        if (prog.at_checked((size_t)pc, &cmd) != 0) {
            refuse_line(&e, step, "fetch=command", E_FORMAT,
                        "explicit bounds check failed on command fetch");
            refused = 1;
            final_code = E_FORMAT;
            break;
        }
        pc++;

        if (cmd == CMD_PLACE) {
            int r = 0;
            int c = 0;
            int d = 0;
            int rc;
            if (pc + 3 > (int)prog.size()) {
                refuse_line(&e, step + 1, "cmd=P operands=missing",
                            E_FORMAT, "place needs r c d");
                refused = 1;
                final_code = E_FORMAT;
                break;
            }
            if (prog.at_checked((size_t)pc, &r) != 0 ||
                prog.at_checked((size_t)(pc + 1), &c) != 0 ||
                prog.at_checked((size_t)(pc + 2), &d) != 0) {
                refuse_line(&e, step + 1, "cmd=P operands=oob", E_FORMAT,
                            "explicit bounds check failed on operands");
                refused = 1;
                final_code = E_FORMAT;
                break;
            }
            pc += 3;
            step++;
            snprintf(detail, sizeof(detail), "cmd=P r=%d c=%d d=%d", r, c, d);
            rc = board.place(r, c, d, &code);
            if (rc != 0) {
                refuse_line(&e, step, detail, code,
                            code == E_BOUNDS ? "cell index or digit out of range (0..8 / 1..9)"
                            : code == E_GIVEN ? "cell is a fixed given"
                            : code == E_OCCUPIED ? "cell already filled"
                            : "digit conflicts with row, column or box");
                refused = 1;
                final_code = code;
                break;
            }
            printf("STEP %d %s code=E_OK\n", step, detail);
        } else if (cmd == CMD_REMOVE) {
            int r = 0;
            int c = 0;
            int rc;
            if (pc + 2 > (int)prog.size()) {
                refuse_line(&e, step + 1, "cmd=R operands=missing",
                            E_FORMAT, "remove needs r c");
                refused = 1;
                final_code = E_FORMAT;
                break;
            }
            if (prog.at_checked((size_t)pc, &r) != 0 ||
                prog.at_checked((size_t)(pc + 1), &c) != 0) {
                refuse_line(&e, step + 1, "cmd=R operands=oob", E_FORMAT,
                            "explicit bounds check failed on operands");
                refused = 1;
                final_code = E_FORMAT;
                break;
            }
            pc += 2;
            step++;
            snprintf(detail, sizeof(detail), "cmd=R r=%d c=%d", r, c);
            rc = board.remove(r, c, &code);
            if (rc != 0) {
                refuse_line(&e, step, detail, code,
                            code == E_BOUNDS ? "cell index out of range (0..8)"
                            : code == E_GIVEN ? "cell is a fixed given"
                            : "cell is already empty");
                refused = 1;
                final_code = code;
                break;
            }
            printf("STEP %d %s code=E_OK\n", step, detail);
        } else if (cmd == CMD_VALIDATE) {
            step++;
            solved_now = board.solved();
            printf("VALIDATE step=%d code=E_OK solved=%d\n", step, solved_now);
        } else {
            char bad[80];
            snprintf(bad, sizeof(bad), "cmd=%d unknown", cmd);
            refuse_line(&e, step + 1, bad, E_FORMAT,
                        "unknown command token (expected P/R/V)");
            refused = 1;
            final_code = E_FORMAT;
            break;
        }
    }

    if (refused) {
        printf("SUDOKU_DONE status=REFUSED code=%s\n", code_name(final_code));
        return 2;
    }

    printf("FINAL solved=%d changes=%d steps=%d\n",
           board.solved(), board.change_count(), step);
    print("FINAL_BOARD");
    board.render();
    printf("SUDOKU_DONE status=OK solved=%d\n", board.solved());
    return 0;
}
