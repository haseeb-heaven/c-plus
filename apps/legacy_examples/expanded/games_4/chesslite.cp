/* chesslite.cp -- deterministic CHESS SUBSET logic core with a CLI driver.
 *
 * SUBSET, NOT FULL CHESS.  Only pawns, rooks and knights exist.  There is no
 * king, queen or bishop, no castling, en passant, pawn promotion, check,
 * checkmate, stalemate distinction, draw rule, clock, opening book or search.
 * "Win" here means only: opponent has no pieces left or no legal move.  It is
 * a bounded teaching model of legality/collision checks, not a chess engine.
 *
 * NOT interactive: fixed argv/stdin move sequences, no randomness, no wall
 * clock; identical input always yields byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/games_4/chesslite.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/games_4/bin/chesslite
 *
 * Run (a move is exactly four digits: from-row from-col to-row to-col):
 *   bin/chesslite 0120 6343 1323 6151 0625 6757 2544 6656 1434 4334
 *   bin/chesslite 0120634313236151062567572544665614344334  # compact
 *   printf '0120 6343 ... 4334\n' | bin/chesslite -
 *   bin/chesslite                                           # built-in default
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

/* Stable refusal codes (shared with connect4.cp and checkers.cp). */
#define E_OVERSIZE 1
#define E_RANGE 2
#define E_ILLEGAL 3
#define E_SYNTAX 4
#define E_COUNT 5

/* Piece codes. */
#define EMPTY 0
#define W_PAWN 1
#define W_ROOK 2
#define W_KNIGHT 3
#define B_PAWN 4
#define B_ROOK 5
#define B_KNIGHT 6

#define PLAYER_WHITE 1
#define PLAYER_BLACK 2

/* Built-in legal sequence; move 10 is a black pawn capturing a white pawn. */
static const char *DEFAULT_MOVES =
    "0120 6343 1323 6151 0625 6757 2544 6656 1434 4334";

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
    fprintf(stderr, "chesslite: error code=%d category=%d message=%s\n",
            err.code(), err.category(), err.what());
}

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

/* Parse one 4-digit move, or the next group of a compact run of 4-digit
 * groups.  0 on success, otherwise a stable E_* code. */
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

class ChessLite {
    int cells[SIZE][SIZE];
    int current;
    int white_count;
    int black_count;
    int ply;
    int captures;

    ChessLite(void) {
        int c;
        this->current = PLAYER_WHITE;
        this->white_count = 0;
        this->black_count = 0;
        this->ply = 0;
        this->captures = 0;
        for (c = 0; c < SIZE; c++) {
            int r;
            for (r = 0; r < SIZE; r++) {
                this->cells[r][c] = EMPTY;
            }
        }
        this->cells[0][0] = W_ROOK;
        this->cells[0][1] = W_KNIGHT;
        this->cells[0][6] = W_KNIGHT;
        this->cells[0][7] = W_ROOK;
        this->cells[7][0] = B_ROOK;
        this->cells[7][1] = B_KNIGHT;
        this->cells[7][6] = B_KNIGHT;
        this->cells[7][7] = B_ROOK;
        for (c = 0; c < SIZE; c++) {
            this->cells[1][c] = W_PAWN;
            this->cells[6][c] = B_PAWN;
        }
        this->white_count = 16;
        this->black_count = 16;
    }

    /* All reads go through a guard: an unchecked coordinate reports a miss
     * instead of touching the backing array. */
    int tile(int r, int c) {
        if (r < 0 || r >= SIZE || c < 0 || c >= SIZE)
            return -1;
        return this->cells[r][c];
    }

    int owner(int piece) {
        if (piece == W_PAWN || piece == W_ROOK || piece == W_KNIGHT)
            return PLAYER_WHITE;
        if (piece == B_PAWN || piece == B_ROOK || piece == B_KNIGHT)
            return PLAYER_BLACK;
        return 0;
    }

    /* Pure legality probe for `player`.  0 when legal, E_ILLEGAL otherwise.
     * Bounds are checked separately by the caller (so it can report E_RANGE).
     * *capture receives 1 when the destination holds an opponent piece. */
    int probe(int player, int fr, int fc, int tr, int tc, int *capture) {
        int piece = this->tile(fr, fc);
        int dest = this->tile(tr, tc);
        int dr;
        int dc;
        int ar;
        int ac;
        if (piece <= 0 || this->owner(piece) != player)
            return E_ILLEGAL;
        if (dest > 0 && this->owner(dest) == player)
            return E_ILLEGAL;
        *capture = dest > 0 ? 1 : 0;
        dr = tr - fr;
        dc = tc - fc;
        ar = dr < 0 ? -dr : dr;
        ac = dc < 0 ? -dc : dc;

        if (piece == W_PAWN || piece == B_PAWN) {
            int forward = piece == W_PAWN ? 1 : -1;
            int home = piece == W_PAWN ? 1 : 6;
            if (dr == forward && dc == 0)
                return dest == EMPTY ? 0 : E_ILLEGAL;
            if (dr == forward && ac == 1)
                return *capture ? 0 : E_ILLEGAL;
            if (dr == 2 * forward && dc == 0 && fr == home &&
                this->tile(fr + forward, fc) == EMPTY && dest == EMPTY)
                return 0;
            return E_ILLEGAL;
        }
        if (piece == W_ROOK || piece == B_ROOK) {
            int sr;
            int sc;
            int r;
            int c;
            int steps;
            if (!((dr == 0 && dc != 0) || (dc == 0 && dr != 0)))
                return E_ILLEGAL;
            sr = dr > 0 ? 1 : -1;
            sc = dc > 0 ? 1 : -1;
            r = fr + sr;
            c = fc + sc;
            steps = 0;
            while (r != tr || c != tc) {
                if (steps >= SIZE || this->tile(r, c) != EMPTY)
                    return E_ILLEGAL;
                r += sr;
                c += sc;
                steps++;
            }
            return 0;
        }
        if (piece == W_KNIGHT || piece == B_KNIGHT) {
            if ((ar == 1 && ac == 2) || (ar == 2 && ac == 1))
                return 0;
            return E_ILLEGAL;
        }
        return E_ILLEGAL;
    }

    /* 0 on success; E_RANGE / E_ILLEGAL otherwise.  No state change on
     * refusal, because the probe runs completely before any mutation. */
    int apply(int fr, int fc, int tr, int tc, int *capture, int *mover) {
        int piece;
        int rc;
        if (fr < 0 || fr >= SIZE || fc < 0 || fc >= SIZE ||
            tr < 0 || tr >= SIZE || tc < 0 || tc >= SIZE)
            return E_RANGE;
        rc = this->probe(this->current, fr, fc, tr, tc, capture);
        if (rc != 0)
            return rc;
        piece = this->cells[fr][fc];
        *mover = piece;
        if (*capture) {
            if (this->owner(this->cells[tr][tc]) == PLAYER_WHITE)
                this->white_count--;
            else
                this->black_count--;
            this->captures++;
        }
        this->cells[fr][fc] = EMPTY;
        this->cells[tr][tc] = piece;
        this->ply++;
        return 0;
    }

    /* Does `player` have any legal move?  Pure; used for the no-move loss
     * condition.  Bounded nested loops over 8x8 source/destination pairs. */
    int has_move(int player) {
        int fr;
        int fc;
        int tr;
        int tc;
        for (fr = 0; fr < SIZE; fr++) {
            for (fc = 0; fc < SIZE; fc++) {
                if (this->owner(this->tile(fr, fc)) != player)
                    continue;
                for (tr = 0; tr < SIZE; tr++) {
                    for (tc = 0; tc < SIZE; tc++) {
                        int capture = 0;
                        if (this->probe(player, fr, fc, tr, tc, &capture) == 0)
                            return 1;
                    }
                }
            }
        }
        return 0;
    }

    void next_turn(void) {
        this->current = this->current == PLAYER_WHITE
                            ? PLAYER_BLACK
                            : PLAYER_WHITE;
    }

    int side(void) { return this->current; }
    int whites(void) { return this->white_count; }
    int blacks(void) { return this->black_count; }
    int plies(void) { return this->ply; }
    int taken(void) { return this->captures; }

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
                if (v == W_PAWN) putchar('P');
                else if (v == W_ROOK) putchar('R');
                else if (v == W_KNIGHT) putchar('N');
                else if (v == B_PAWN) putchar('p');
                else if (v == B_ROOK) putchar('r');
                else if (v == B_KNIGHT) putchar('n');
                else if (v < 0) putchar('?');
                else putchar('.');
            }
            printf("\n");
        }
    }
};

static const char *side_name(int side) {
    return side == PLAYER_WHITE ? "white" : "black";
}

static const char *piece_name(int piece) {
    if (piece == W_PAWN) return "P";
    if (piece == W_ROOK) return "R";
    if (piece == W_KNIGHT) return "N";
    if (piece == B_PAWN) return "p";
    if (piece == B_ROOK) return "r";
    return "n";
}

static int read_stdin(vector *out) {
    char buf[512];
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
        return E_COUNT;
    return parse_moves(buf, out);
}

int main(int argc, char **argv) {
    vector moves = vector();
    ChessLite board = ChessLite();
    int a;
    int i;
    int step = 0;
    int rc;
    int fr = 0, fc = 0, tr = 0, tc = 0;
    int capture = 0;
    int mover = EMPTY;
    int opponent;
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

    printf("CHESSLITE subset=pawn,rook,knight size=%d moves=%d\n",
           SIZE, (int)(moves.size() / 4));
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
        rc = board.apply(fr, fc, tr, tc, &capture, &mover);
        if (rc != 0) {
            if (rc == E_RANGE)
                refuse(E_RANGE, "coordinate outside 0..7", step);
            else
                refuse(E_ILLEGAL, "illegal chesslite move", step);
            return 2;
        }
        printf("STEP %d side=%s piece=%s from=(%d,%d) to=(%d,%d) capture=%d\n",
               step, side_name(board.side()), piece_name(mover),
               fr, fc, tr, tc, capture);
        board.render();

        opponent = board.side() == PLAYER_WHITE ? PLAYER_BLACK : PLAYER_WHITE;
        if (opponent == PLAYER_WHITE) {
            if (board.whites() == 0) {
                result_name = "win";
                result_detail = "black:white-pieces-exhausted";
                break;
            }
            if (!board.has_move(PLAYER_WHITE)) {
                result_name = "win";
                result_detail = "black:white-has-no-move";
                break;
            }
        } else {
            if (board.blacks() == 0) {
                result_name = "win";
                result_detail = "white:black-pieces-exhausted";
                break;
            }
            if (!board.has_move(PLAYER_BLACK)) {
                result_name = "win";
                result_detail = "white:black-has-no-move";
                break;
            }
        }
        board.next_turn();
    }

    printf("RESULT %s detail=%s step=%d\n", result_name, result_detail, step);
    printf("FINAL result=%s plies=%d moves=%d side=%s white=%d black=%d captures=%d\n",
           result_name, board.plies(), (int)(moves.size() / 4),
           side_name(board.side()), board.whites(), board.blacks(),
           board.taken());
    return 0;
}
