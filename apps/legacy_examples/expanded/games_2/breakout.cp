/* breakout.cp -- deterministic brick-breaker logic core with a CLI driver.
 *
 * This is NOT an interactive game.  There is no terminal graphics/curses UI,
 * no keyboard/event loop, no wall clock and no network.  A fixed seed supplies
 * only the initial ball direction; all other state is a pure function of the
 * fixed command sequence.  Identical input yields byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/games_2/breakout.cp \
 *       --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games_2/bin/breakout
 *
 * Run:
 *   bin/breakout R L S R F ...     # commands as separate argv tokens
 *   bin/breakout RLSR              # one compact argv token
 *   printf 'RLSR\n' | bin/breakout -  # commands from one stdin line
 *   bin/breakout                   # built-in fixed default sequence
 *
 * Command alphabet (one tick per character): L/R = move paddle, S/. = still.
 * Anything else is refused (exit 2).  Oversized tokens are refused too.
 *
 * Exit 0 on valid input, 2 on invalid/oversized input (clean refusal).
 */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define BREAKOUT_SEED 0x5EED1234u
#define W 16             /* columns 0..15 */
#define BRICK_ROWS 4
#define H 12             /* rows 0..11; paddle lives on row 11 */
#define PADDLE_W 3
#define START_LIVES 3
#define MAX_TOKEN_LEN 64
#define MAX_TICKS 128
#define DEFAULT_CMDS "RLSRRLSSLLRRRLSRS"

static unsigned int g_rng;

static void rng_reseed(unsigned int seed) {
    g_rng = seed;
}

static unsigned int rnext(void) {
    unsigned int x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

enum { CMD_STILL = 0, CMD_LEFT = 1, CMD_RIGHT = 2 };

static int cmd_code(char ch) {
    if (ch == 'L' || ch == 'l')
        return CMD_LEFT;
    if (ch == 'R' || ch == 'r')
        return CMD_RIGHT;
    if (ch == 'S' || ch == 's' || ch == '.')
        return CMD_STILL;
    return -1;
}

static const char *cmd_name(int code) {
    if (code == CMD_LEFT)
        return "L";
    if (code == CMD_RIGHT)
        return "R";
    return "S";
}

static int parse_cmds(const char *text, vector *out) {
    string s = string(text);
    size_t i;
    size_t n = s.size();
    if (n > MAX_TOKEN_LEN)
        return -2; /* oversized */
    for (i = 0; i < n; i++) {
        char ch = s.at(i);
        int code;
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t')
            continue;
        code = cmd_code(ch);
        if (code < 0)
            return -1;
        if (out->push_back(code) != 0)
            return -1;
    }
    return 0;
}

class Breakout {
    int bricks[BRICK_ROWS][W];
    int bx;
    int by;
    int vx;
    int vy;
    int paddle;
    int lives;
    int score;
    int remaining;
    int paddle_hits;
    int wall_hits;
    int brick_hits;
    int ticks;
    int won;
    int over;

    Breakout(void) {
        int r;
        int c;
        unsigned int seed_bit = rnext() & 1u;
        for (r = 0; r < BRICK_ROWS; r++)
            for (c = 0; c < W; c++)
                this->bricks[r][c] = 1;
        this->remaining = BRICK_ROWS * W;
        this->bx = W / 2;          /* constant divisor: 16 / 2 */
        this->by = H - 3;
        this->vx = seed_bit ? 1 : -1;
        this->vy = -1;
        this->paddle = 0;
        this->lives = START_LIVES;
        this->score = 0;
        this->paddle_hits = 0;
        this->wall_hits = 0;
        this->brick_hits = 0;
        this->ticks = 0;
        this->won = 0;
        this->over = 0;
    }

    void reset_ball(void) {
        unsigned int seed_bit = rnext() & 1u;
        this->bx = W / 2;
        this->by = H - 3;
        this->vx = seed_bit ? 1 : -1;
        this->vy = -1;
    }

    /* Apply one command and one physics tick.  Writes a stable event name. */
    void step(int cmd, const char **event) {
        this->ticks++;
        if (cmd == CMD_LEFT) {
            if (this->paddle > 0)
                this->paddle--;
        } else if (cmd == CMD_RIGHT) {
            if (this->paddle < W - PADDLE_W)
                this->paddle++;
        }

        this->bx += this->vx;
        this->by += this->vy;
        *event = "none";

        /* Side walls. */
        if (this->bx < 0) {
            this->bx = 0;
            this->vx = 1;
            this->wall_hits++;
            *event = "wall";
        } else if (this->bx >= W) {
            this->bx = W - 1;
            this->vx = -1;
            this->wall_hits++;
            *event = "wall";
        }

        /* Top wall. */
        if (this->by < 0) {
            this->by = 0;
            this->vy = 1;
            this->wall_hits++;
            *event = "top";
        }

        /* Brick collision: only inside the brick band. */
        if (this->by < BRICK_ROWS && this->by >= 0 &&
            this->bx >= 0 && this->bx < W &&
            this->bricks[this->by][this->bx]) {
            this->bricks[this->by][this->bx] = 0;
            this->remaining--;
            this->score += 10;
            this->brick_hits++;
            this->vy = -this->vy;
            *event = "brick";
            if (this->remaining == 0)
                this->won = 1;
        }

        /* Paddle line. */
        if (this->by >= H - 1) {
            if (this->bx >= this->paddle &&
                this->bx < this->paddle + PADDLE_W) {
                this->by = H - 2;
                this->vy = -1;
                this->paddle_hits++;
                *event = "paddle";
            } else {
                this->lives--;
                *event = "lose";
                if (this->lives <= 0) {
                    this->over = 1;
                } else {
                    this->reset_ball();
                }
            }
        }
    }

    void render(void) {
        int r;
        int c;
        for (r = 0; r < H; r++) {
            printf("  ");
            for (c = 0; c < W; c++) {
                char ch = '.';
                if (r < BRICK_ROWS && this->bricks[r][c])
                    ch = '#';
                if (r == this->by && c == this->bx)
                    ch = 'o';
                if (r == H - 1 && c >= this->paddle &&
                    c < this->paddle + PADDLE_W)
                    ch = '=';
                putchar(ch);
            }
            printf("\n");
        }
    }

    int ball_x(void) { return this->bx; }
    int ball_y(void) { return this->by; }
    int vel_x(void) { return this->vx; }
    int vel_y(void) { return this->vy; }
    int paddle_left(void) { return this->paddle; }
    int lives_left(void) { return this->lives; }
    int score_value(void) { return this->score; }
    int bricks_left(void) { return this->remaining; }
    int paddle_count(void) { return this->paddle_hits; }
    int wall_count(void) { return this->wall_hits; }
    int brick_count(void) { return this->brick_hits; }
    int tick_count(void) { return this->ticks; }
    int is_won(void) { return this->won; }
    int is_over(void) { return this->over; }
};

static int refuse(const char *message) {
    error err = error();
    err.set(CPLUS_EXIT_UNSUPPORTED, message);
    fprintf(stderr, "breakout: refused: %s\n", err.what());
    return 2;
}

static int read_stdin(vector *out) {
    char buf[MAX_TOKEN_LEN + 4];
    size_t got;
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
        return -1;
    got = 0;
    while (buf[got] != '\0' && got < sizeof(buf))
        got++;
    if (got >= sizeof(buf) - 1)
        return -2;
    while (got > 0 && (buf[got - 1] == '\n' || buf[got - 1] == '\r')) {
        got--;
        buf[got] = '\0';
    }
    if (got > MAX_TOKEN_LEN)
        return -2;
    return parse_cmds(buf, out);
}

int main(int argc, char **argv) {
    rng_reseed(BREAKOUT_SEED);
    vector cmds = vector();
    Breakout game = Breakout();
    int i;
    int a;
    int rc;
    const char *event = "none";
    const char *outcome = "incomplete";

    if (argc > 1) {
        const char *first = NULL;
        __builtin_memcpy(&first, argv + 1, sizeof(first));
        string first_str = string(first);
        if (first_str.equals("-")) {
            if (argc != 2)
                return refuse("'-' must be the only argument when reading stdin");
            rc = read_stdin(&cmds);
            if (rc == -2)
                return refuse("stdin line is oversized");
            if (rc != 0)
                return refuse("invalid command in stdin (expected L/R/S)");
        } else {
            for (a = 1; a < argc; a++) {
                const char *arg = NULL;
                __builtin_memcpy(&arg, argv + a, sizeof(arg));
                if (arg == NULL)
                    return refuse("null command token");
                rc = parse_cmds(arg, &cmds);
                if (rc == -2)
                    return refuse("token is oversized (max 64 characters)");
                if (rc != 0)
                    return refuse("invalid command token (expected L/R/S)");
            }
        }
    } else {
        rc = parse_cmds(DEFAULT_CMDS, &cmds);
        if (rc != 0)
            return refuse("internal default command sequence is invalid");
    }
    if (cmds.size() == 0)
        return refuse("empty command sequence");
    if (cmds.size() > MAX_TICKS)
        return refuse("too many commands (max 128 ticks)");

    printf("BREAKOUT seed=0x%08X grid=%dx%d bricks=%d paddle_w=%d lives=%d cmds=%d\n",
           (unsigned)BREAKOUT_SEED, W, H, BRICK_ROWS * W, PADDLE_W,
           START_LIVES, (int)cmds.size());
    printf("INIT ball=(%d,%d) v=(%+d,%+d) paddle=[%d,%d] score=0 bricks=%d\n",
           game.ball_x(), game.ball_y(), game.vel_x(), game.vel_y(),
           game.paddle_left(), game.paddle_left() + PADDLE_W - 1,
           game.bricks_left());
    game.render();

    for (i = 0; i < (int)cmds.size(); i++) {
        int code = cmds.at((size_t)i);
        game.step(code, &event);
        printf("TICK %d cmd=%s ball=(%d,%d) v=(%+d,%+d) paddle=[%d,%d] event=%s "
               "score=%d bricks=%d lives=%d\n",
               i + 1, cmd_name(code), game.ball_x(), game.ball_y(),
               game.vel_x(), game.vel_y(), game.paddle_left(),
               game.paddle_left() + PADDLE_W - 1, event,
               game.score_value(), game.bricks_left(), game.lives_left());
        game.render();
        if (game.is_won() || game.is_over())
            break;
    }

    if (game.is_won())
        outcome = "win";
    else if (game.is_over())
        outcome = "game-over";
    printf("FINAL outcome=%s ticks=%d score=%d bricks=%d paddle_hits=%d "
           "wall_hits=%d brick_hits=%d lives=%d\n",
           outcome, game.tick_count(), game.score_value(), game.bricks_left(),
           game.paddle_count(), game.wall_count(), game.brick_count(),
           game.lives_left());
    return 0;
}
