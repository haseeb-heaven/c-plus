/* minesweeper.cp -- deterministic 9x9 Minesweeper logic core with argv CLI.
 *
 * NOT an interactive game: no graphics, no keyboard, no clock, no randomness.
 * The mine layout is fixed static data, moves come from argv, and every run
 * with the same arguments produces byte-identical stdout.
 *
 * This is a teaching model with explicit bounds checks, NOT a formal proof of
 * memory safety.  Each refusal is an observed run-time error (error record +
 * nonzero exit); no proof engine certifies the grid accesses.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/games_3/minesweeper.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/games_3/bin/minesweeper
 *
 * Run:
 *   bin/minesweeper                 # built-in fixed clear-the-board sequence
 *   bin/minesweeper R 4 4 F 0 0 V   # reveal, flag, status
 *
 * Command grammar (argv tokens, left to right):
 *   R r c   reveal cell (r,c), flooding connected zero-adjacency cells
 *   F r c   toggle a flag on an unrevealed cell (r,c)
 *   V       print status (state, revealed count, flags)
 *
 * Exit 0 for an accepted program (including a win or a loss), 2 on the first
 * refused move.  Revealing an already-revealed cell is a benign no-op.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define N 9
#define CELLS 81
#define NMINES 11

#define CMD_REVEAL -100
#define CMD_FLAG -101
#define CMD_STATUS -102

#define STATE_PLAY 0
#define STATE_WIN 1
#define STATE_LOSE 2

#define E_OK 0
#define E_PARSE 1
#define E_FORMAT 2
#define E_BOUNDS 3
#define E_FLAGGED 4
#define E_REVEALED 5

/* Fixed mine layout, as linear row-major indices (r*9+c). */
static const int MINES[NMINES] = {
    0, 8, 13, 20, 33, 37, 43, 50, 57, 63, 80
};

static const char *state_name(int state) {
    if (state == STATE_WIN)
        return "win";
    if (state == STATE_LOSE)
        return "lose";
    return "playing";
}

static const char *code_name(int code) {
    if (code == E_OK) return "E_OK";
    if (code == E_PARSE) return "E_PARSE";
    if (code == E_FORMAT) return "E_FORMAT";
    if (code == E_BOUNDS) return "E_BOUNDS";
    if (code == E_FLAGGED) return "E_FLAGGED";
    return "E_REVEALED";
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

class Minefield {
    int mine[CELLS];
    int adj[CELLS];
    int revealed[CELLS];
    int flagged[CELLS];
    int safe_total;
    int revealed_count;
    int flag_count;
    int state;
    int lose_idx;

    Minefield(void) {
        int i;
        int r;
        int c;
        int idx;
        this->safe_total = CELLS - NMINES;
        this->revealed_count = 0;
        this->flag_count = 0;
        this->state = STATE_PLAY;
        this->lose_idx = -1;
        for (i = 0; i < CELLS; i++) {
            this->mine[i] = 0;
            this->adj[i] = 0;
            this->revealed[i] = 0;
            this->flagged[i] = 0;
        }
        for (i = 0; i < NMINES; i++) {
            idx = MINES[i];
            if (idx >= 0 && idx < CELLS)
                this->mine[idx] = 1;
        }
        /* Adjacency counts use only in-range neighbour indices. */
        for (idx = 0; idx < CELLS; idx++) {
            int count = 0;
            int dr;
            int dc;
            r = idx / N;
            c = idx - (idx / N) * N;
            for (dr = -1; dr <= 1; dr++) {
                for (dc = -1; dc <= 1; dc++) {
                    int nr;
                    int nc;
                    int nidx;
                    if (dr == 0 && dc == 0)
                        continue;
                    nr = r + dr;
                    nc = c + dc;
                    if (nr < 0 || nr >= N || nc < 0 || nc >= N)
                        continue;
                    nidx = nr * N + nc;
                    if (this->mine[nidx])
                        count++;
                }
            }
            this->adj[idx] = count;
        }
    }

    int index_of(int r, int c) {
        if (r < 0 || r >= N || c < 0 || c >= N)
            return -1;
        return r * N + c;
    }

    /* Moves after the game is over are benign no-ops (no refusal). */
    int over(void) {
        return this->state != STATE_PLAY;
    }

    /* 0 accepted, -1 refused (reason code written to out_code). */
    int reveal(int r, int c, int *out_code, int *out_effect) {
        int idx = this->index_of(r, c);
        int stack[CELLS];
        int queued[CELLS];
        int sp = 0;
        int i;
        if (idx < 0) {
            *out_code = E_BOUNDS;
            return -1;
        }
        if (this->over()) {
            *out_effect = 0;
            return 0;
        }
        if (this->flagged[idx]) {
            *out_code = E_FLAGGED;
            return -1;
        }
        if (this->revealed[idx]) {
            *out_effect = 0; /* already revealed: benign no-op */
            return 0;
        }
        for (i = 0; i < CELLS; i++)
            queued[i] = 0;
        if (this->mine[idx]) {
            this->revealed[idx] = 1;
            this->state = STATE_LOSE;
            this->lose_idx = idx;
            *out_effect = 1;
            *out_code = E_OK;
            return 0;
        }
        /* Bounded flood fill: a cell is queued at most once, so sp <= CELLS. */
        stack[sp] = idx;
        sp++;
        queued[idx] = 1;
        while (sp > 0) {
            int cur;
            int cr;
            int cc;
            int dr;
            int dc;
            sp--;
            cur = stack[sp];
            if (this->revealed[cur] || this->flagged[cur])
                continue;
            this->revealed[cur] = 1;
            this->revealed_count++;
            if (this->adj[cur] != 0)
                continue;
            cr = cur / N;
            cc = cur - (cur / N) * N;
            for (dr = -1; dr <= 1; dr++) {
                for (dc = -1; dc <= 1; dc++) {
                    int nr;
                    int nc;
                    int nidx;
                    if (dr == 0 && dc == 0)
                        continue;
                    nr = cr + dr;
                    nc = cc + dc;
                    if (nr < 0 || nr >= N || nc < 0 || nc >= N)
                        continue;
                    nidx = nr * N + nc;
                    if (this->mine[nidx] || this->revealed[nidx] ||
                        this->flagged[nidx] || queued[nidx])
                        continue;
                    if (sp >= CELLS)
                        continue; /* explicit stack-capacity guard */
                    stack[sp] = nidx;
                    sp++;
                    queued[nidx] = 1;
                }
            }
        }
        *out_effect = 1;
        if (this->revealed_count >= this->safe_total)
            this->state = STATE_WIN;
        *out_code = E_OK;
        return 0;
    }

    int flag(int r, int c, int *out_code, int *out_effect) {
        int idx = this->index_of(r, c);
        if (idx < 0) {
            *out_code = E_BOUNDS;
            return -1;
        }
        if (this->over()) {
            *out_effect = 0;
            return 0;
        }
        if (this->revealed[idx]) {
            *out_code = E_REVEALED;
            return -1;
        }
        if (this->flagged[idx]) {
            this->flagged[idx] = 0;
            this->flag_count--;
            *out_effect = 2; /* unflagged */
        } else {
            this->flagged[idx] = 1;
            this->flag_count++;
            *out_effect = 1; /* flagged */
        }
        *out_code = E_OK;
        return 0;
    }

    void render(void) {
        int i;
        for (i = 0; i < CELLS; i++) {
            int r = i / N;
            int c = i - (i / N) * N;
            if (c != 0)
                putchar(' ');
            if (this->flagged[i])
                putchar('F');
            else if (this->mine[i] && this->state == STATE_LOSE)
                putchar('*');
            else if (!this->revealed[i])
                putchar('#');
            else if (this->adj[i] == 0)
                putchar('.');
            else
                printf("%d", this->adj[i]);
            if (c == N - 1)
                printf(" r%d\n", r);
        }
    }

    int state_now(void) { return this->state; }
    int revealed_now(void) { return this->revealed_count; }
    int flags_now(void) { return this->flag_count; }
    int safe_total_now(void) { return this->safe_total; }
    int lose_index(void) { return this->lose_idx; }
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
                if (ch == 'R' || ch == 'r') {
                    prog->push_back(CMD_REVEAL);
                    continue;
                }
                if (ch == 'F' || ch == 'f') {
                    prog->push_back(CMD_FLAG);
                    continue;
                }
                if (ch == 'V' || ch == 'v') {
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

int main(int argc, char **argv) {
    Minefield field = Minefield();
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
    int win_step = 0;
    int lose_step = 0;

    if (argc > 1) {
        parse_code = parse_program(argc, argv, &prog, token, sizeof(token), &reason);
        if (parse_code != E_OK) {
            refuse_arg(&e, token, parse_code, reason);
            printf("MINESWEEPER_DONE status=REFUSED code=%s\n",
                   code_name(parse_code));
            return 2;
        }
    } else {
        /* Built-in clear-the-board program: reveal every non-mine cell in
         * row-major order, then print status.  Flooded cells become no-ops. */
        for (i = 0; i < CELLS; i++) {
            int is_mine = 0;
            int m;
            for (m = 0; m < NMINES; m++) {
                if (MINES[m] == i) {
                    is_mine = 1;
                    break;
                }
            }
            if (!is_mine) {
                int r = i / N;
                int c = i - (i / N) * N;
                prog.push_back(CMD_REVEAL);
                prog.push_back(r);
                prog.push_back(c);
            }
        }
        prog.push_back(CMD_STATUS);
    }
    if (prog.size() == 0) {
        refuse_line(&e, 0, "program=empty", E_FORMAT, "empty move program");
        printf("MINESWEEPER_DONE status=REFUSED code=%s\n", code_name(E_FORMAT));
        return 2;
    }

    printf("MINESWEEPER grid=9x9 mines=%d safe=%d program=%zu\n",
           NMINES, field.safe_total_now(), prog.size());
    print("INITIAL");
    field.render();

    while (pc < (int)prog.size()) {
        int cmd = 0;
        char detail[200];
        int code = E_OK;
        int effect = 0;
        int rc;
        if (prog.at_checked((size_t)pc, &cmd) != 0) {
            refuse_line(&e, step, "fetch=command", E_FORMAT,
                        "explicit bounds check failed on command fetch");
            refused = 1;
            final_code = E_FORMAT;
            break;
        }
        pc++;

        if (cmd == CMD_REVEAL || cmd == CMD_FLAG) {
            int r = 0;
            int c = 0;
            if (pc + 2 > (int)prog.size()) {
                refuse_line(&e, step + 1,
                            cmd == CMD_REVEAL ? "cmd=R operands=missing"
                                              : "cmd=F operands=missing",
                            E_FORMAT, "cell move needs r c");
                refused = 1;
                final_code = E_FORMAT;
                break;
            }
            if (prog.at_checked((size_t)pc, &r) != 0 ||
                prog.at_checked((size_t)(pc + 1), &c) != 0) {
                refuse_line(&e, step + 1, "operands=oob", E_FORMAT,
                            "explicit bounds check failed on operands");
                refused = 1;
                final_code = E_FORMAT;
                break;
            }
            pc += 2;
            step++;
            if (cmd == CMD_REVEAL)
                snprintf(detail, sizeof(detail), "cmd=R r=%d c=%d", r, c);
            else
                snprintf(detail, sizeof(detail), "cmd=F r=%d c=%d", r, c);
            if (cmd == CMD_REVEAL)
                rc = field.reveal(r, c, &code, &effect);
            else
                rc = field.flag(r, c, &code, &effect);
            if (rc != 0) {
                refuse_line(&e, step, detail, code,
                            code == E_BOUNDS ? "cell coordinate out of range (0..8)"
                            : code == E_FLAGGED ? "cell is flagged; unflag it first"
                            : "cell is already revealed");
                refused = 1;
                final_code = code;
                break;
            }
            if (code == E_BOUNDS) {
                refuse_line(&e, step, detail, code,
                            "cell coordinate out of range (0..8)");
                refused = 1;
                final_code = code;
                break;
            }
            {
                const char *note = effect == 1
                    ? (cmd == CMD_REVEAL ? "revealed" : "flagged")
                    : effect == 2 ? "unflagged" : "noop";
                printf("STEP %d %s code=E_OK effect=%s state=%s revealed=%d flags=%d\n",
                       step, detail, note, state_name(field.state_now()),
                       field.revealed_now(), field.flags_now());
            }
            if (field.state_now() == STATE_WIN && win_step == 0) {
                win_step = step;
                printf("RESULT win step=%d revealed=%d\n",
                       win_step, field.revealed_now());
            }
            if (field.state_now() == STATE_LOSE && lose_step == 0) {
                int li = field.lose_index();
                int lr = li / N;
                int lc = li - (li / N) * N;
                lose_step = step;
                printf("RESULT lose step=%d mine=(%d,%d)\n", lose_step, lr, lc);
            }
        } else if (cmd == CMD_STATUS) {
            step++;
            printf("STATUS step=%d code=E_OK state=%s revealed=%d/%d flags=%d\n",
                   step, state_name(field.state_now()), field.revealed_now(),
                   field.safe_total_now(), field.flags_now());
        } else {
            char bad[80];
            snprintf(bad, sizeof(bad), "cmd=%d unknown", cmd);
            refuse_line(&e, step + 1, bad, E_FORMAT,
                        "unknown command token (expected R/F/V)");
            refused = 1;
            final_code = E_FORMAT;
            break;
        }
    }

    if (refused) {
        printf("MINESWEEPER_DONE status=REFUSED code=%s\n", code_name(final_code));
        return 2;
    }

    printf("FINAL state=%s revealed=%d/%d flags=%d steps=%d\n",
           state_name(field.state_now()), field.revealed_now(),
           field.safe_total_now(), field.flags_now(), step);
    print("FINAL_BOARD");
    field.render();
    printf("MINESWEEPER_DONE status=OK state=%s\n", state_name(field.state_now()));
    return 0;
}
