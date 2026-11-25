/* checkers.cp -- deterministic 8x8 checkers logic core with a CLI driver.
 *
 * NOT an interactive game and NOT full tournament checkers.  Rules are the
 * explicit subset listed in NOTES.md: men move diagonally forward, jumps
 * capture one adjacent opponent, reaching the far row promotes to king, and
 * a player with no pieces or no legal move loses.  There are NO forced
 * captures, NO multi-jump chains, NO kings-row backward restrictions beyond
 * direction, no clocks and no randomness.  Fixed argv/stdin moves only.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/games_4/checkers.cp \
 *       --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games_4/bin/checkers
 *
 * Run (a move is exactly four digits: from-row from-col to-row to-col):
 *   bin/checkers 2130 5041 1021 4132 2334 3223 0110 2301
 *   bin/checkers 21305041102141322334322301102301  # compact 4-digit moves
 *   printf '2130 5041 1021 4132 2334 3223 0110 2301\n' | bin/checkers -
 *   bin/checkers                                    # built-in sequence
 *
 * Exit 0 on a valid sequence, 2 on any refusal.  Every refusal prints a
 * stable REFUSE code on stdout and an error record on stderr.
 */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define SIZE 8
#define MAX_MOVE_LEN 4

/* Stable refusal codes (shared with connect4.cp and chesslite.cp). */
#define E_OVERSIZE 1
#define E_RANGE 2
#define E_ILLEGAL 3
#define E_SYNTAX 4
#define E_COUNT 5

/* Piece codes. */
#define EMPTY 0
#define RED_MAN 1
#define RED_KING 2
#define WHITE_MAN 3
#define WHITE_KING 4

#define PLAYER_RED 1
#define PLAYER_WHITE 2

/* Built-in sequence: a capture (move 8) promoting a white man at row 0. */
static const char *DEFAULT_MOVES =
    "2130 5041 1021 4132 2334 3223 0110 2301";

static const char *refuse_name(int code) {
    if (code == E_OVERSIZE) return "E_OVERSIZE";
    if (code == E_RANGE) return "E_RANGE";
    if (code == E_ILLEGAL) return "E_ILLEGAL";
    if (code == E_SYNTAX) return "E_SYNTAX";
    return "E_COUNT";
}

static cplus_exit_code refuse_exit_code(int code) {
    if (code == E_OVERSIZE || code == E_RANGE)
        return CPLUS_EXIT_BOUNDS;
    return CPLUS_EXIT_UNSUPPORTED;
}

static void refuse(int code, const char *detail, int step) {
    error err = error();
    cplus_exit_code ec = refuse_exit_code(code);
    err.set(ec, detail);
    printf("REFUSE step=%d code=%d name=%s errcode=%d category=%d detail=%s\n",
           step, code, refuse_name(code), err.code(), err.category(), detail);
    fprintf(stderr, "checkers: error code=%d category=%d message=%s\n",
            err.code(), err.category(), err.what());
}

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

/* Parse one 4-digit move token, or the next 4-digit group of a compact run
 * whose length is an exact multiple of 4.  0 on success, else a stable code. */
static int parse_move(string *s, size_t *cursor, int *fr, int *fc,
                      int *tr, int *tc) {
    size_t n = s->size();
    size_t i = *cursor;
    size_t start;
    size_t len;
    size_t k;
    int coords[4];

    while (i < n && is_space(s->at(i)))
        i++;
    if (i >= n) {
        *cursor = i;
        return E_COUNT;
    }
    start = i;
    while (i < n && !is_space(s->at(i)))
        i++;
    len = i - start;
    if (len > MAX_MOVE_LEN) {
        size_t g;
        /* A compact run must be whole 4-digit groups of coordinates 0..7. */
        if ((len % MAX_MOVE_LEN) != 0)
            return E_OVERSIZE;
        for (g = 0; g < len; g++) {
            char ch = s->at(start + g);
            if (ch < '0' || ch > '7')
                return E_RANGE;
        }
        len = MAX_MOVE_LEN;
    } else {
        if (len != MAX_MOVE_LEN)
            return E_SYNTAX;
    }
    for (k = 0; k < 4; k++) {
        char ch = s->at(start + k);
        if (ch < '0' || ch > '7')
            return E_RANGE;
        coords[k] = (int)(ch - '0');
    }
    *cursor = start + MAX_MOVE_LEN;
    *fr = coords[0];
    *fc = coords[1];
    *tr = coords[2];
    *tc = coords[3];
    return 0;
}

/* Parse all moves in a token/line; a compact run of digits is split into
 * consecutive 4-digit groups, exactly like separate tokens. */
static int parse_moves(const char *text, vector *out) {
    string s = string(text);
    size_t n = s.size();
    size_t i = 0;
    int rc;
    int parsed = 0;
    for (;;) {
        int fr = 0, fc = 0, tr = 0, tc = 0;
        rc = parse_move(&s, &i, &fr, &fc, &tr, &tc);
        if (rc == E_COUNT)
            break;
        if (rc != 0)
            return rc;
        if (parsed >= 64)
            return E_COUNT;
        out->push_back(fr);
        out->push_back(fc);
        out->push_back(tr);
        out->push_back(tc);
        parsed++;
    }
    if (parsed == 0)
        return E_COUNT;
    return 0;
}

class Checkers {
    int cells[SIZE][SIZE];
    int current;
    int red_count;
    int white_count;
    int ply;

    Checkers(void) {
        int r;
        int c;
        this->current = PLAYER_RED;
        this->red_count = 0;
        this->white_count = 0;
        this->ply = 0;
        for (r = 0; r < SIZE; r++) {
            for (c = 0; c < SIZE; c++) {
                this->cells[r][c] = EMPTY;
                if (((r + c) & 1) == 1) {
                    if (r < 3) {
                        this->cells[r][c] = RED_MAN;
                        this->red_count++;
                    } else if (r > 4) {
                        this->cells[r][c] = WHITE_MAN;
                        this->white_count++;
                    }
                }
            }
        }
    }

    /* All reads go through a guard so an unchecked coordinate can never
     * index the backing array. */
    int tile(int r, int c) {
        if (r < 0 || r >= SIZE || c < 0 || c >= SIZE)
            return -1;
        return this->cells[r][c];
    }

    int is_king(int piece) {
        return piece == RED_KING || piece == WHITE_KING;
    }

    int owner(int piece) {
        if (piece == RED_MAN || piece == RED_KING)
            return PLAYER_RED;
        if (piece == WHITE_MAN || piece == WHITE_KING)
            return PLAYER_WHITE;
        return 0;
    }

    int dark(int r, int c) {
        return r >= 0 && r < SIZE && c >= 0 && c < SIZE && ((r + c) & 1) == 1;
    }

    int forward(int piece, int dr) {
        if (this->is_king(piece))
            return dr == 1 || dr == -1;
        if (piece == RED_MAN)
            return dr == 1;
        return dr == -1;
    }

    /* Apply a move.  0 on success; *captured/*promoted/*status are reported
     * for the transcript.  E_RANGE / E_ILLEGAL otherwise; state is untouched
     * on refusal. */
    int apply(int fr, int fc, int tr, int tc,
              int *captured, int *promoted, int *status) {
        int piece;
        int dr;
        int dc;
        int adr;
        int adc;
        int mid;
        int dest;
        if (!this->dark(fr, fc) || !this->dark(tr, tc))
            return E_ILLEGAL;
        piece = this->tile(fr, fc);
        if (piece < 0 || this->owner(piece) != this->current)
            return E_ILLEGAL;
        dest = this->tile(tr, tc);
        if (dest != EMPTY)
            return E_ILLEGAL;
        dr = tr - fr;
        dc = tc - fc;
        adr = dr < 0 ? -dr : dr;
        adc = dc < 0 ? -dc : dc;
        if (adr != 1 && adr != 2)
            return E_ILLEGAL;
        if (adc != adr)
            return E_ILLEGAL;
        if (!this->forward(piece, dr < 0 ? -1 : 1))
            return E_ILLEGAL;
        *captured = 0;
        if (adr == 2) {
            mid = this->tile(fr + dr / 2, fc + dc / 2);
            if (mid <= 0 || this->owner(mid) == this->current)
                return E_ILLEGAL;
            this->cells[fr + dr / 2][fc + dc / 2] = EMPTY;
            if (this->owner(mid) == PLAYER_RED)
                this->red_count--;
            else
                this->white_count--;
            *captured = 1;
        }
        this->cells[fr][fc] = EMPTY;
        this->cells[tr][tc] = piece;
        *promoted = 0;
        if ((piece == RED_MAN && tr == SIZE - 1) ||
            (piece == WHITE_MAN && tr == 0)) {
            this->cells[tr][tc] = piece + 1;
            *promoted = 1;
        }
        this->ply++;
        *status = 0;
        return 0;
    }

    /* Does `player` have at least one legal move?  Used for the no-move loss
     * condition; it never mutates state. */
    int has_move(int player) {
        int r;
        int c;
        for (r = 0; r < SIZE; r++) {
            for (c = 0; c < SIZE; c++) {
                int piece = this->tile(r, c);
                int dirs[2];
                int nd;
                int k;
                if (piece <= 0 || this->owner(piece) != player)
                    continue;
                nd = 0;
                if (this->is_king(piece) || piece == RED_MAN) {
                    dirs[nd] = 1;
                    nd++;
                }
                if (this->is_king(piece) || piece == WHITE_MAN) {
                    dirs[nd] = -1;
                    nd++;
                }
                for (k = 0; k < nd; k++) {
                    int dr = dirs[k];
                    int dc;
                    for (dc = -1; dc <= 1; dc += 2) {
                        int nr = r + dr;
                        int nc = c + dc;
                        if (this->dark(nr, nc) && this->tile(nr, nc) == EMPTY)
                            return 1;
                        nr = r + 2 * dr;
                        nc = c + 2 * dc;
                        if (this->dark(nr, nc) && this->tile(nr, nc) == EMPTY) {
                            int mid = this->tile(r + dr, c + dc);
                            if (mid > 0 && this->owner(mid) != player)
                                return 1;
                        }
                    }
                }
            }
        }
        return 0;
    }

    void next_turn(void) {
        this->current = this->current == PLAYER_RED
                            ? PLAYER_WHITE
                            : PLAYER_RED;
    }

    int side(void) { return this->current; }
    int reds(void) { return this->red_count; }
    int whites(void) { return this->white_count; }
    int plies(void) { return this->ply; }

    void render(void) {
        int r;
        int c;
        printf("  ");
        for (c = 0; c < SIZE; c++)
            printf("%d", c);
        printf("\n");
        for (r = 0; r < SIZE; r++) {
            printf("%d ", r);
            for (c = 0; c < SIZE; c++) {
                int v = this->tile(r, c);
                if (((r + c) & 1) == 0)
                    putchar(' ');
                else if (v == RED_MAN)
                    putchar('r');
                else if (v == RED_KING)
                    putchar('R');
                else if (v == WHITE_MAN)
                    putchar('w');
                else if (v == WHITE_KING)
                    putchar('W');
                else if (v < 0)
                    putchar('?');
                else
                    putchar('.');
            }
            printf("\n");
        }
    }
};

static const char *side_name(int side) {
    return side == PLAYER_RED ? "red" : "white";
}

static int read_stdin(vector *out) {
    char buf[512];
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
        return E_COUNT;
    return parse_moves(buf, out);
}

int main(int argc, char **argv) {
    vector moves = vector();
    Checkers board = Checkers();
    int a;
    int i;
    int step = 0;
    int rc;
    int fr = 0, fc = 0, tr = 0, tc = 0;
    int captured = 0;
    int promoted = 0;
    int status = 0;
    int mover;
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
                rc = parse_moves(arg, &moves);
                if (rc != 0) {
                    refuse(rc, "invalid move token", a);
                    return 2;
                }
            }
        }
    } else {
        rc = parse_moves(DEFAULT_MOVES, &moves);
        if (rc != 0) {
            refuse(rc, "internal default sequence is invalid", 0);
            return 2;
        }
    }
    if ((moves.size() & 3) != 0) {
        refuse(E_SYNTAX, "move sequence is not a whole number of moves", 0);
        return 2;
    }

    printf("CHECKERS size=%d moves=%d\n", SIZE, (int)(moves.size() / 4));
    printf("INITIAL\n");
    board.render();

    for (i = 0; i < (int)moves.size(); i += 4) {
        step = i / 4 + 1;
        rc = moves.at_checked((size_t)i, &fr);
        if (rc == 0)
            rc = moves.at_checked((size_t)(i + 1), &fc);
        if (rc == 0)
            rc = moves.at_checked((size_t)(i + 2), &tr);
        if (rc == 0)
            rc = moves.at_checked((size_t)(i + 3), &tc);
        if (rc != 0) {
            refuse(E_COUNT, "move vector read failed", step);
            return 2;
        }
        mover = board.side();
        rc = board.apply(fr, fc, tr, tc, &captured, &promoted, &status);
        if (rc != 0) {
            if (rc == E_RANGE)
                refuse(E_RANGE, "coordinate outside 0..7", step);
            else
                refuse(E_ILLEGAL, "illegal checkers move", step);
            return 2;
        }
        printf("STEP %d side=%s from=(%d,%d) to=(%d,%d) capture=%d promote=%d\n",
               step, side_name(mover), fr, fc, tr, tc, captured, promoted);
        board.render();

        if (mover == PLAYER_RED) {
            if (board.whites() == 0) {
                result_name = "win";
                result_detail = "red:white-pieces-exhausted";
                break;
            }
            if (!board.has_move(PLAYER_WHITE)) {
                result_name = "win";
                result_detail = "red:white-has-no-move";
                break;
            }
        } else {
            if (board.reds() == 0) {
                result_name = "win";
                result_detail = "white:red-pieces-exhausted";
                break;
            }
            if (!board.has_move(PLAYER_RED)) {
                result_name = "win";
                result_detail = "white:red-has-no-move";
                break;
            }
        }
        board.next_turn();
    }

    printf("RESULT %s detail=%s step=%d\n", result_name, result_detail, step);
    printf("FINAL result=%s plies=%d moves=%d side=%s red=%d white=%d\n",
           result_name, board.plies(), (int)(moves.size() / 4),
           side_name(board.side()), board.reds(), board.whites());
    return 0;
}
