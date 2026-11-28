/* nim.cp -- deterministic Nim logic core with an argv CLI.
 *
 * NOT an interactive game.  The three heaps are fixed static data.  Two
 * deterministic players alternate turns: player A plays the optimal
 * nim-sum move, player B plays a fixed take-away rule (remove one token from
 * the first non-empty heap).  Every move goes through one shared `take`
 * routine that refuses amount < 1 and amount > heap; the optimal strategy can
 * therefore never remove more than a heap without being caught.
 *
 * Teaching model with explicit bounds checks, NOT a formal proof of memory
 * safety.  Refusals are observed run-time errors (error record + nonzero
 * exit), not the output of a proof engine.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/games_3/nim.cp \
 *       --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games_3/bin/nim
 *
 * Run:
 *   bin/nim                       # full built-in A(optimal) vs B(take-away) game
 *   bin/nim O T 0 1 O T 1 1 S     # explicit alternating moves + status
 *
 * Command grammar (argv tokens, left to right; turns alternate A then B):
 *   O         optimal move computed from the current nim-sum
 *   T i a     take away a tokens from heap i (requires 1 <= a <= heaps[i])
 *   S         print status (heaps, nim-sum, moves)
 *
 * Exit 0 for an accepted program (including a finished game), 2 on the first
 * refused move.  A finished game treats later O/T commands as a refusal.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define NH 3
#define MAX_MOVES 256

#define CMD_OPTIMAL -100
#define CMD_TAKE -101
#define CMD_STATUS -102

#define E_OK 0
#define E_PARSE 1
#define E_FORMAT 2
#define E_BOUNDS 3
#define E_AMOUNT 4
#define E_INTERNAL 5

static const int INITIAL_HEAPS[NH] = {3, 4, 5};

static const char *code_name(int code) {
    if (code == E_OK) return "E_OK";
    if (code == E_PARSE) return "E_PARSE";
    if (code == E_FORMAT) return "E_FORMAT";
    if (code == E_BOUNDS) return "E_BOUNDS";
    if (code == E_AMOUNT) return "E_AMOUNT";
    return "E_INTERNAL";
}

static cplus_exit_code category_for(int code) {
    if (code == E_BOUNDS)
        return CPLUS_EXIT_BOUNDS;
    return CPLUS_EXIT_UNSUPPORTED;
}

/* Strict, overflow-aware integer parse: 0 ok, -1 malformed, -2 oversized. */
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

static void render_heaps(const int *heaps) {
    int i;
    printf("[");
    for (i = 0; i < NH; i++) {
        if (i != 0)
            printf(",");
        printf("%d", heaps[i]);
    }
    printf("]");
}

static int nim_sum(const int *heaps) {
    int x = 0;
    int i;
    for (i = 0; i < NH; i++)
        x = x ^ heaps[i];
    return x;
}

class NimGame {
    int heaps[NH];
    int moves;
    int finished;
    int winner;

    NimGame(void) {
        int i;
        this->moves = 0;
        this->finished = 0;
        this->winner = 0;
        for (i = 0; i < NH; i++)
            this->heaps[i] = INITIAL_HEAPS[i];
    }

    int heap(int i) {
        if (i < 0 || i >= NH)
            return -1;
        return this->heaps[i];
    }

    int over(void) {
        return this->finished;
    }

    int empty(void) {
        int i;
        for (i = 0; i < NH; i++) {
            if (this->heaps[i] != 0)
                return 0;
        }
        return 1;
    }

    /* THE single guarded mutation: both strategies call this.  It refuses
     * out-of-range heap indices and any amount outside [1, heaps[i]], so a
     * computed optimal move can never remove more than the heap holds. */
    int take(int i, int amount, int *out_code) {
        if (i < 0 || i >= NH) {
            *out_code = E_BOUNDS;
            return -1;
        }
        if (amount < 1 || amount > this->heaps[i]) {
            *out_code = E_AMOUNT;
            return -1;
        }
        this->heaps[i] = this->heaps[i] - amount;
        this->moves++;
        if (this->heaps[i] < 0) {
            /* Defensive invariant: the guard above must make this impossible. */
            *out_code = E_INTERNAL;
            return -1;
        }
        *out_code = E_OK;
        return 0;
    }

    /* Compute an optimal move: target = heaps[i] xor nim_sum must be smaller
     * than heaps[i], so amount = heaps[i] - target is in [1, heaps[i]].  On a
     * P-position (nim_sum == 0) fall back to removing one from the first
     * non-empty heap, which is always legal. */
    int optimal(int *out_i, int *out_amount, int *out_code) {
        int x = nim_sum(this->heaps);
        int i;
        if (this->empty()) {
            *out_code = E_INTERNAL;
            return -1;
        }
        if (x != 0) {
            for (i = 0; i < NH; i++) {
                int target = this->heaps[i] ^ x;
                if (target < this->heaps[i]) {
                    *out_i = i;
                    *out_amount = this->heaps[i] - target;
                    *out_code = E_OK;
                    return 0;
                }
            }
        }
        for (i = 0; i < NH; i++) {
            if (this->heaps[i] > 0) {
                *out_i = i;
                *out_amount = 1;
                *out_code = E_OK;
                return 0;
            }
        }
        *out_code = E_INTERNAL;
        return -1;
    }

    /* Deterministic take-away player: one token from the first non-empty heap. */
    int takeaway(int *out_i, int *out_amount, int *out_code) {
        int i;
        for (i = 0; i < NH; i++) {
            if (this->heaps[i] > 0) {
                *out_i = i;
                *out_amount = 1;
                *out_code = E_OK;
                return 0;
            }
        }
        *out_code = E_INTERNAL;
        return -1;
    }

    int play(int player, int strategy, int manual_i, int manual_a,
             int *out_i, int *out_amount, int *out_code) {
        int rc;
        if (this->finished) {
            *out_code = E_INTERNAL;
            return -1;
        }
        if (this->moves >= MAX_MOVES) {
            *out_code = E_INTERNAL;
            return -1;
        }
        if (strategy == CMD_OPTIMAL) {
            rc = this->optimal(out_i, out_amount, out_code);
            if (rc != 0)
                return -1;
        } else if (strategy == CMD_TAKE) {
            if (manual_i < 0 || manual_i >= NH) {
                *out_code = E_BOUNDS;
                return -1;
            }
            *out_i = manual_i;
            *out_amount = manual_a;
        } else {
            *out_code = E_FORMAT;
            return -1;
        }
        /* Both strategies, and every manual move, pass through this guard. */
        rc = this->take(*out_i, *out_amount, out_code);
        if (rc != 0)
            return -1;
        (void)player;
        if (this->empty()) {
            this->finished = 1;
            this->winner = player;
        }
        return 0;
    }

    int moves_now(void) { return this->moves; }
    int winner_now(void) { return this->winner; }
    int finished_now(void) { return this->finished; }
};

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
                if (ch == 'O' || ch == 'o') {
                    prog->push_back(CMD_OPTIMAL);
                    continue;
                }
                if (ch == 'T' || ch == 't') {
                    prog->push_back(CMD_TAKE);
                    continue;
                }
                if (ch == 'S' || ch == 's') {
                    prog->push_back(CMD_STATUS);
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

/* Run the built-in A(optimal) vs B(take-away) game to completion.  Returns 0
 * on a normal finish, -1 with a refusal code on an internal bound violation. */
static int simulate(NimGame *game, error *e, int *out_code) {
    int guard;
    for (guard = 0; guard < MAX_MOVES; guard++) {
        int turn = game->moves_now();
        int player = (turn & 1) == 0 ? 1 : 2;
        int strategy = player == 1 ? CMD_OPTIMAL : CMD_TAKE;
        int i = 0;
        int amount = 0;
        int x_before;
        int code = E_OK;
        int rc;
        if (strategy == CMD_TAKE) {
            rc = game->takeaway(&i, &amount, &code);
            if (rc != 0) {
                *out_code = code;
                return -1;
            }
        }
        /* The xor is read through the checked heap accessor; no unchecked
         * direct array read is needed from outside the class. */
        {
            int copy[NH];
            int k;
            for (k = 0; k < NH; k++)
                copy[k] = game->heap(k);
            x_before = nim_sum(copy);
        }
        rc = game->play(player, strategy, i, amount, &i, &amount, &code);
        if (rc != 0) {
            char detail[120];
            snprintf(detail, sizeof(detail), "internal move player=%d", player);
            refuse_line(e, game->moves_now() + 1, detail, code,
                        code == E_BOUNDS ? "internal move exceeded heap bounds"
                        : code == E_AMOUNT ? "internal move removed more than the heap holds"
                        : "internal move invariant violation");
            *out_code = code;
            return -1;
        }
        {
            int copy[NH];
            int k;
            for (k = 0; k < NH; k++)
                copy[k] = game->heap(k);
            printf("MOVE %d player=%s strategy=%s heap=%d amount=%d "
                   "bound=%s xor_before=%d xor_after=%d heaps=",
                   game->moves_now(), player == 1 ? "A" : "B",
                   strategy == CMD_OPTIMAL ? "optimal" : "takeaway",
                   i, amount, "ok",
                   x_before, nim_sum(copy));
            render_heaps(copy);
            printf("\n");
        }
        if (game->finished_now())
            break;
    }
    if (!game->finished_now()) {
        refuse_line(e, game->moves_now(), "simulation=incomplete", E_INTERNAL,
                    "game did not finish within the move cap");
        *out_code = E_INTERNAL;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    NimGame game = NimGame();
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
    int internal_code = E_OK;

    if (argc > 1) {
        parse_code = parse_program(argc, argv, &prog, token, sizeof(token), &reason);
        if (parse_code != E_OK) {
            refuse_arg(&e, token, parse_code, reason);
            printf("NIM_DONE status=REFUSED code=%s\n", code_name(parse_code));
            return 2;
        }
    }
    if (prog.size() == 0 && argc == 1) {
        printf("NIM heaps=");
        render_heaps(INITIAL_HEAPS);
        printf(" heap_count=%d mode=builtin\n", NH);
        printf("INITIAL heaps=");
        {
            int copy[NH];
            for (i = 0; i < NH; i++)
                copy[i] = game.heap(i);
            render_heaps(copy);
            printf(" xor=%d\n", nim_sum(copy));
        }
        if (simulate(&game, &e, &internal_code) != 0) {
            printf("NIM_DONE status=REFUSED code=%s\n",
                   code_name(internal_code));
            return 2;
        }
        printf("RESULT winner=%s moves=%d heaps=",
               game.winner_now() == 1 ? "A" : "B", game.moves_now());
        {
            int copy[NH];
            for (i = 0; i < NH; i++)
                copy[i] = game.heap(i);
            render_heaps(copy);
            printf("\n");
        }
        printf("FINAL winner=%s moves=%d\n",
               game.winner_now() == 1 ? "A" : "B", game.moves_now());
        printf("NIM_DONE status=OK winner=%s\n",
               game.winner_now() == 1 ? "A" : "B");
        return 0;
    }
    if (prog.size() == 0) {
        refuse_line(&e, 0, "program=empty", E_FORMAT, "empty move program");
        printf("NIM_DONE status=REFUSED code=%s\n", code_name(E_FORMAT));
        return 2;
    }

    printf("NIM heaps=");
    render_heaps(INITIAL_HEAPS);
    printf(" heap_count=%d program=%zu\n", NH, prog.size());
    printf("INITIAL heaps=");
    {
        int copy[NH];
        for (i = 0; i < NH; i++)
            copy[i] = game.heap(i);
        render_heaps(copy);
        printf(" xor=%d\n", nim_sum(copy));
    }

    while (pc < (int)prog.size()) {
        int cmd = 0;
        char detail[200];
        int code = E_OK;
        int player;
        int i_take = 0;
        int amount = 0;
        int x_before;
        int bound_ok;
        int rc;
        if (prog.at_checked((size_t)pc, &cmd) != 0) {
            refuse_line(&e, step, "fetch=command", E_FORMAT,
                        "explicit bounds check failed on command fetch");
            refused = 1;
            final_code = E_FORMAT;
            break;
        }
        pc++;

        if (cmd == CMD_STATUS) {
            int copy[NH];
            for (i = 0; i < NH; i++)
                copy[i] = game.heap(i);
            step++;
            printf("STATUS step=%d code=E_OK heaps=", step);
            render_heaps(copy);
            printf(" xor=%d moves=%d finished=%d\n",
                   nim_sum(copy), game.moves_now(), game.finished_now());
            continue;
        }

        if (cmd != CMD_OPTIMAL && cmd != CMD_TAKE) {
            snprintf(detail, sizeof(detail), "cmd=%d unknown", cmd);
            refuse_line(&e, step + 1, detail, E_FORMAT,
                        "unknown command token (expected O/T/S)");
            refused = 1;
            final_code = E_FORMAT;
            break;
        }
        if (cmd == CMD_TAKE) {
            if (pc + 2 > (int)prog.size()) {
                refuse_line(&e, step + 1, "cmd=T operands=missing", E_FORMAT,
                            "take needs heap index and amount");
                refused = 1;
                final_code = E_FORMAT;
                break;
            }
            if (prog.at_checked((size_t)pc, &i_take) != 0 ||
                prog.at_checked((size_t)(pc + 1), &amount) != 0) {
                refuse_line(&e, step + 1, "cmd=T operands=oob", E_FORMAT,
                            "explicit bounds check failed on operands");
                refused = 1;
                final_code = E_FORMAT;
                break;
            }
            pc += 2;
        }
        player = (game.moves_now() & 1) == 0 ? 1 : 2;
        step++;
        {
            int copy[NH];
            for (i = 0; i < NH; i++)
                copy[i] = game.heap(i);
            x_before = nim_sum(copy);
        }
        bound_ok = (cmd != CMD_TAKE) ||
                   (i_take >= 0 && i_take < NH && amount >= 1 && amount <= game.heap(i_take));
        rc = game.play(player, cmd, i_take, amount, &i_take, &amount, &code);
        if (rc != 0) {
            if (cmd == CMD_TAKE)
                snprintf(detail, sizeof(detail), "cmd=T i=%d a=%d", i_take, amount);
            else
                snprintf(detail, sizeof(detail), "cmd=O player=%d", player);
            refuse_line(&e, step, detail, code,
                        code == E_BOUNDS ? "heap index out of range (0..2)"
                        : code == E_AMOUNT ? "amount must be in [1, heap size]"
                        : "no legal move (game over or internal invariant)");
            refused = 1;
            final_code = code;
            break;
        }
        if (!bound_ok) {
            snprintf(detail, sizeof(detail), "cmd=%d bound=VIOLATION", cmd);
            refuse_line(&e, step, detail, E_INTERNAL,
                        "move failed the explicit cannot-remove-more-than-heap check");
            refused = 1;
            final_code = E_INTERNAL;
            break;
        }
        {
            int copy[NH];
            for (i = 0; i < NH; i++)
                copy[i] = game.heap(i);
            printf("MOVE %d player=%s strategy=%s heap=%d amount=%d "
                   "bound=ok xor_before=%d xor_after=%d heaps=",
                   step, player == 1 ? "A" : "B",
                   cmd == CMD_OPTIMAL ? "optimal" : "takeaway",
                   i_take, amount, x_before, nim_sum(copy));
            render_heaps(copy);
            printf("\n");
        }
        if (game.finished_now()) {
            printf("RESULT winner=%s step=%d moves=%d\n",
                   game.winner_now() == 1 ? "A" : "B", step, game.moves_now());
        }
    }

    if (refused) {
        printf("NIM_DONE status=REFUSED code=%s\n", code_name(final_code));
        return 2;
    }

    printf("FINAL winner=%s moves=%d finished=%d steps=%d\n",
           game.winner_now() == 1 ? "A" : "B", game.moves_now(),
           game.finished_now(), step);
    printf("NIM_DONE status=OK winner=%s\n",
           game.finished_now() ? (game.winner_now() == 1 ? "A" : "B") : "none");
    return 0;
}
