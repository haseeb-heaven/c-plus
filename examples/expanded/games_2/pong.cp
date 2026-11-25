/* pong.cp -- deterministic 1D paddle + ball logic core with a CLI driver.
 *
 * This is NOT an interactive game.  There is no terminal graphics/curses UI,
 * no keyboard/event loop, no wall clock and no network.  A fixed 32-bit seed
 * supplies only the initial setup (ball direction and paddle offset); every
 * subsequent state is a pure function of the fixed command sequence.  The same
 * input always produces byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/games_2/pong.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/games_2/bin/pong
 *
 * Run:
 *   bin/pong R L S R R          # commands as separate argv tokens
 *   bin/pong RLSRR              # one compact argv token
 *   printf 'RLSRR\n' | bin/pong -   # commands from one stdin line
 *   bin/pong                    # built-in fixed default sequence
 *
 * Command alphabet (one tick per character): L = paddle left, R = paddle
 * right, S/. = stand still.  Anything else is refused (exit 2).
 *
 * Exit 0 on valid input, 2 on invalid/oversized input.  Adverse inputs are
 * refused cleanly (never crash).
 */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define PONG_SEED 0x1A2B3C4Du
#define GRID_W 21          /* columns 0..20; col 0 = goal, col 20 = right wall */
#define PADDLE_W 3
#define MAX_TOKEN_LEN 64
#define MAX_TICKS 128
#define DEFAULT_CMDS "SLLLRRRSSRRLL"

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

/* Parse a compact token such as "RLS" into command codes.  Whitespace is
 * skipped; any other character is an out-of-range command (-1). */
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

class Pong {
    int ball;
    int vel;
    int paddle;       /* leftmost column of the paddle */
    int score;        /* saves */
    int opp;          /* misses */
    int saves;
    int misses;
    int wall_hits;
    int ticks;

    Pong(void) {
        unsigned int first = rnext();
        unsigned int second = rnext();
        this->ball = GRID_W / 2;                 /* constant divisor: 21 / 2 */
        this->vel = (first & 1u) ? 1 : -1;
        this->paddle = (int)(second & 3u);       /* 0..3, may or may not guard goal */
        this->score = 0;
        this->opp = 0;
        this->saves = 0;
        this->misses = 0;
        this->wall_hits = 0;
        this->ticks = 0;
    }

    /* Apply one command.  Writes a stable event name into *event. */
    void step(int cmd, const char **event) {
        int b;
        this->ticks++;
        if (cmd == CMD_LEFT) {
            if (this->paddle > 0)
                this->paddle--;
        } else if (cmd == CMD_RIGHT) {
            if (this->paddle < GRID_W - PADDLE_W)
                this->paddle++;
        }

        b = this->ball + this->vel;
        *event = "none";
        if (b >= GRID_W - 1) {           /* right wall at col 20 */
            b = GRID_W - 2;              /* reflect to col 19 */
            this->vel = -1;
            this->wall_hits++;
            *event = "wall";
        }
        if (b <= 0) {                    /* left goal line at col 0 */
            if (this->paddle == 0) {     /* paddle covers the goal column */
                b = 1;
                this->vel = 1;
                this->score++;
                this->saves++;
                *event = "save";
            } else {
                b = GRID_W / 2;          /* reset to centre, constant divisor */
                this->vel = 1;
                this->opp++;
                this->misses++;
                *event = "miss";
            }
        }
        this->ball = b;
    }

    void render(void) {
        char line[GRID_W + 1];
        int i;
        for (i = 0; i < GRID_W; i++)
            line[i] = '.';
        line[GRID_W - 1] = '|';
        for (i = this->paddle; i < this->paddle + PADDLE_W; i++)
            line[i] = '=';
        line[this->ball] = 'o';
        line[GRID_W] = '\0';
        printf("  %s\n", line);
    }

    int ball_col(void) { return this->ball; }
    int velocity(void) { return this->vel; }
    int paddle_left(void) { return this->paddle; }
    int score_value(void) { return this->score; }
    int opp_value(void) { return this->opp; }
    int save_count(void) { return this->saves; }
    int miss_count(void) { return this->misses; }
    int wall_count(void) { return this->wall_hits; }
    int tick_count(void) { return this->ticks; }
};

static int refuse(const char *message) {
    error err = error();
    err.set(CPLUS_EXIT_UNSUPPORTED, message);
    fprintf(stderr, "pong: refused: %s\n", err.what());
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
        return -2; /* line filled the buffer: oversized */
    while (got > 0 && (buf[got - 1] == '\n' || buf[got - 1] == '\r')) {
        got--;
        buf[got] = '\0';
    }
    if (got > MAX_TOKEN_LEN)
        return -2;
    return parse_cmds(buf, out);
}

int main(int argc, char **argv) {
    vector cmds = vector();
    Pong game = Pong();
    int i;
    int a;
    int rc;
    const char *event = "none";

    rng_reseed(PONG_SEED);

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

    printf("PONG seed=0x%08X grid=%d paddle_w=%d max_ticks=%d cmds=%d\n",
           (unsigned)PONG_SEED, GRID_W, PADDLE_W, MAX_TICKS, (int)cmds.size());
    printf("INIT ball=%d v=%+d paddle=[%d,%d] score=0 opp=0\n",
           game.ball_col(), game.velocity(), game.paddle_left(),
           game.paddle_left() + PADDLE_W - 1);
    game.render();

    for (i = 0; i < (int)cmds.size(); i++) {
        int code = cmds.at((size_t)i);
        game.step(code, &event);
        printf("TICK %d cmd=%s ball=%d v=%+d paddle=[%d,%d] event=%s score=%d opp=%d\n",
               i + 1, cmd_name(code), game.ball_col(), game.velocity(),
               game.paddle_left(), game.paddle_left() + PADDLE_W - 1,
               event, game.score_value(), game.opp_value());
        game.render();
    }

    printf("FINAL ticks=%d saves=%d misses=%d wall_hits=%d score=%d opp=%d\n",
           game.tick_count(), game.save_count(), game.miss_count(),
           game.wall_count(), game.score_value(), game.opp_value());
    return 0;
}
