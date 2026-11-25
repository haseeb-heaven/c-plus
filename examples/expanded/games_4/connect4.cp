/* connect4.cp -- deterministic 6x7 Connect-Four logic core with a CLI driver.
 *
 * This is NOT an interactive game.  There is no terminal UI, no keyboard loop,
 * no wall clock and no randomness.  A fixed move sequence comes from argv or
 * from one stdin line; the same input always yields byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/games_4/connect4.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/games_4/bin/connect4
 *
 * Run:
 *   bin/connect4 3 3 4 4 5 5 6 6           # columns 0..6 as argv tokens
 *   bin/connect4 33445566                  # compact single-digit columns
 *   printf '3 3 4 4 5 5 6 6\n' | bin/connect4 -
 *   bin/connect4                           # built-in fixed sequence
 *
 * Exit 0 on a valid sequence, 2 on any refusal.  Every refusal prints a
 * stable REFUSE code on stdout and an error record (code + category + text)
 * on stderr.  See NOTES.md for the teaching-model / non-proof statement.
 */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define ROWS 6
#define COLS 7
#define CELLS (ROWS * COLS)
#define MAX_LEN 15
#define MAX_VALUE 1000000

/* Stable refusal codes (also recorded in NOTES.md). */
#define E_OVERSIZE 1
#define E_RANGE 2
#define E_ILLEGAL 3
#define E_SYNTAX 4
#define E_COUNT 5

/* Built-in deterministic sequence: vertical red four in column 3. */
static const char *DEFAULT_MOVES = "3 2 3 2 3 2 3";

static const char *refuse_name(int code) {
    if (code == E_OVERSIZE) return "E_OVERSIZE";
    if (code == E_RANGE) return "E_RANGE";
    if (code == E_ILLEGAL) return "E_ILLEGAL";
    if (code == E_SYNTAX) return "E_SYNTAX";
    return "E_COUNT";
}

/* E_OVERSIZE / E_RANGE are bounds class -> CPLUS_EXIT_BOUNDS (101);
 * the remaining refusals are unsupported input -> 108. */
static cplus_exit_code refuse_exit_code(int code) {
    if (code == E_OVERSIZE || code == E_RANGE)
        return CPLUS_EXIT_BOUNDS;
    return CPLUS_EXIT_UNSUPPORTED;
}

/* Print a refusal transcript line plus the underlying error record.  The
 * caller then returns 2; no board state is mutated by a refused move. */
static void refuse(int code, const char *detail, int step) {
    error err = error();
    cplus_exit_code ec = refuse_exit_code(code);
    err.set(ec, detail);
    printf("REFUSE step=%d code=%d name=%s errcode=%d category=%d detail=%s\n",
           step, code, refuse_name(code), err.code(), err.category(), detail);
    fprintf(stderr, "connect4: error code=%d category=%d message=%s\n",
            err.code(), err.category(), err.what());
}

/* Skip C+ whitespace characters in a string value. */
static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

/* Parse one whitespace-separated token into a signed column value.
 * Returns 0 on success, otherwise a stable E_* refusal code. */
static int parse_int_token(string *s, size_t *cursor, int *out) {
    size_t n = s->size();
    size_t i = *cursor;
    char digits[MAX_LEN + 1];
    size_t len = 0;
    size_t k;
    int negative = 0;
    long value = 0;

    while (i < n && is_space(s->at(i)))
        i++;
    if (i >= n) {
        *cursor = i;
        return E_COUNT;
    }
    if (s->at(i) == '-') {
        negative = 1;
        i++;
        if (i >= n)
            return E_SYNTAX;
    }
    while (i < n && !is_space(s->at(i))) {
        char ch = s->at(i);
        if (ch < '0' || ch > '9')
            return E_SYNTAX;
        if (len >= MAX_LEN)
            return E_OVERSIZE;
        digits[len] = ch;
        len++;
        i++;
    }
    if (len == 0)
        return E_SYNTAX;
    /* Leading zeros are accepted, but value growth is capped before the
     * multiply so an oversized token can never wrap an int. */
    for (k = 0; k < len; k++) {
        int d = (int)(digits[k] - '0');
        if (value > MAX_VALUE)
            return E_OVERSIZE;
        value = value * 10 + d;
    }
    if (value > MAX_VALUE)
        return E_OVERSIZE;
    *cursor = i;
    *out = negative ? (int)(-value) : (int)value;
    return 0;
}

/* A multi-character token made only of columns 0..6 is the compact form:
 * one digit per move.  Tokens containing 7..9, '-' or other characters are
 * parsed as a single signed integer instead, so "7" and "-1" are still
 * refused as out-of-range columns rather than silently split. */
static int is_compact(string *s, size_t start, size_t end) {
    size_t i;
    if (end <= start + 1)
        return 0;
    for (i = start; i < end; i++) {
        char ch = s->at(i);
        if (ch < '0' || ch > '6')
            return 0;
    }
    return 1;
}

/* Parse a whole line/token into column moves.  Returns 0 or a stable code. */
static int parse_columns(const char *text, vector *out) {
    string s = string(text);
    size_t n = s.size();
    size_t i = 0;
    int rc;
    int parsed = 0;
    while (i < n) {
        size_t start;
        size_t end;
        while (i < n && is_space(s.at(i)))
            i++;
        if (i >= n)
            break;
        start = i;
        while (i < n && !is_space(s.at(i)))
            i++;
        end = i;
        if (is_compact(&s, start, end)) {
            size_t k;
            for (k = start; k < end; k++) {
                if (parsed >= CELLS)
                    return E_COUNT;
                out->push_back((int)(s.at(k) - '0'));
                parsed++;
            }
        } else {
            size_t cursor = start;
            int value = 0;
            rc = parse_int_token(&s, &cursor, &value);
            if (rc != 0)
                return rc;
            if (parsed >= CELLS)
                return E_COUNT;
            out->push_back(value);
            parsed++;
        }
    }
    if (parsed == 0)
        return E_COUNT;
    return 0;
}

class Connect4 {
    int cells[ROWS][COLS];
    int played;

    Connect4(void) {
        int r;
        int c;
        this->played = 0;
        for (r = 0; r < ROWS; r++) {
            for (c = 0; c < COLS; c++)
                this->cells[r][c] = 0;
        }
    }

    /* Every board read goes through this guard: no caller can index the
     * backing array with an unchecked coordinate. */
    int tile(int r, int c) {
        if (r < 0 || r >= ROWS || c < 0 || c >= COLS)
            return -1;
        return this->cells[r][c];
    }

    /* Drop a disc.  *out_row receives the landing row.  0 on success,
     * E_RANGE for a bad column, E_ILLEGAL for a full column. */
    int drop(int col, int player, int *out_row) {
        int r;
        if (col < 0 || col >= COLS)
            return E_RANGE;
        /* Gravity: lowest empty row in the chosen column. */
        for (r = ROWS - 1; r >= 0; r--) {
            if (this->cells[r][col] == 0) {
                this->cells[r][col] = player;
                this->played++;
                *out_row = r;
                return 0;
            }
        }
        return E_ILLEGAL;
    }

    /* 0 = no four, 1 = horizontal, 2 = vertical, 3 = diagonal '\',
     * 4 = diagonal '/'.  All neighbour reads use tile(), so a bounds bug
     * would be reported as a miss rather than an out-of-bounds access. */
    int win_dir(int row, int col, int player) {
        static const int dr[4] = {0, 1, 1, 1};
        static const int dc[4] = {1, 0, 1, -1};
        int d;
        for (d = 0; d < 4; d++) {
            int count = 1;
            int step;
            for (step = 1; step < 4; step++) {
                if (this->tile(row + dr[d] * step, col + dc[d] * step) != player)
                    break;
                count++;
            }
            for (step = 1; step < 4; step++) {
                if (this->tile(row - dr[d] * step, col - dc[d] * step) != player)
                    break;
                count++;
            }
            if (count >= 4)
                return d + 1;
        }
        return 0;
    }

    int full(void) {
        return this->played == CELLS;
    }

    int count(void) {
        return this->played;
    }

    void render(void) {
        int r;
        int c;
        printf("  ");
        for (c = 0; c < COLS; c++)
            printf("%d", c);
        printf("\n");
        for (r = 0; r < ROWS; r++) {
            printf("  ");
            for (c = 0; c < COLS; c++) {
                int v = this->tile(r, c);
                if (v < 0)
                    putchar('?');
                else if (v == 0)
                    putchar('.');
                else if (v == 1)
                    putchar('R');
                else
                    putchar('Y');
            }
            printf("\n");
        }
    }
};

static const char *win_name(int dir) {
    if (dir == 1) return "horizontal";
    if (dir == 2) return "vertical";
    if (dir == 3) return "diag-down-right";
    return "diag-down-left";
}

static int read_stdin(vector *out) {
    char buf[512];
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
        return E_COUNT;
    return parse_columns(buf, out);
}

int main(int argc, char **argv) {
    vector moves = vector();
    Connect4 board = Connect4();
    int a;
    int i;
    int step = 0;
    int result = 0;
    int player;
    int col = 0;
    int row = 0;
    int rc;
    const char *result_name = "incomplete";
    const char *result_detail = "none";

    if (argc > 1) {
        const char *first = NULL;
        __builtin_memcpy(&first, argv + 1, sizeof(first));
        string first_str = string(first);
        if (first_str.equals("-")) {
            if (argc != 2) {
                refuse(E_COUNT, "stdin sentinel '-' must be the only argument", 0);
                return 2;
            }
            rc = read_stdin(&moves);
            if (rc != 0) {
                refuse(rc, "invalid stdin move sequence", 0);
                return 2;
            }
        } else {
            for (a = 1; a < argc; a++) {
                const char *arg = NULL;
                __builtin_memcpy(&arg, argv + a, sizeof(arg));
                if (arg == NULL) {
                    refuse(E_SYNTAX, "null argument", a);
                    return 2;
                }
                rc = parse_columns(arg, &moves);
                if (rc != 0) {
                    refuse(rc, "invalid column token", a);
                    return 2;
                }
            }
        }
    } else {
        rc = parse_columns(DEFAULT_MOVES, &moves);
        if (rc != 0) {
            refuse(rc, "internal default sequence is invalid", 0);
            return 2;
        }
    }

    printf("CONNECT4 rows=%d cols=%d moves=%d\n",
           ROWS, COLS, (int)moves.size());
    printf("INITIAL\n");
    board.render();

    for (i = 0; i < (int)moves.size(); i++) {
        rc = moves.at_checked((size_t)i, &col);
        if (rc != 0) {
            refuse(E_COUNT, "move vector read failed", i + 1);
            return 2;
        }
        step = i + 1;
        player = (i & 1) == 0 ? 1 : 2;
        rc = board.drop(col, player, &row);
        if (rc != 0) {
            if (rc == E_RANGE)
                refuse(E_RANGE, "column outside 0..6", step);
            else
                refuse(E_ILLEGAL, "column is full", step);
            return 2;
        }
        printf("STEP %d player=%s col=%d row=%d\n",
               step, player == 1 ? "R" : "Y", col, row);
        board.render();

        result = board.win_dir(row, col, player);
        if (result != 0) {
            result_name = "win";
            result_detail = win_name(result);
            break;
        }
        if (board.full()) {
            result_name = "draw";
            result_detail = "board-full";
            break;
        }
    }

    printf("RESULT %s detail=%s step=%d\n", result_name, result_detail, step);
    printf("FINAL result=%s played=%d moves=%d r=%d y=%d\n",
           result_name, board.count(), (int)moves.size(),
           (board.count() + 1) >> 1, board.count() >> 1);
    return 0;
}
