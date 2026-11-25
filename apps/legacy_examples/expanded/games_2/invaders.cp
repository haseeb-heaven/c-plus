/* invaders.cp -- deterministic fixed-formation space-invaders logic core.
 *
 * This is NOT an interactive game.  There is no terminal graphics/curses UI,
 * no keyboard/event loop, no wall clock and no network.  The enemy formation is
 * fixed, a fixed seed only sets the initial ship column, and every later state
 * is a pure function of the fixed command sequence.  Identical input yields
 * byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/games_2/invaders.cp \
 *       --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games_2/bin/invaders
 *
 * Run:
 *   bin/invaders L F R F S F       # commands as separate argv tokens
 *   bin/invaders LFRSF             # one compact argv token
 *   printf 'LFRSF\n' | bin/invaders -  # commands from one stdin line
 *   bin/invaders                   # built-in fixed default sequence
 *
 * Command alphabet (one tick per character): L/R = move ship, S/. = still,
 * F = fire.  Anything else is refused (exit 2).  Oversized input is refused.
 *
 * Exit 0 on valid input, 2 on invalid/oversized input (clean refusal).
 */
#include <stdio.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define INVADERS_SEED 0x13579BDFu
#define W 16
#define H 10
#define ENEMY_ROWS 3
#define ENEMY_COLS 5
#define ENEMY_BASE 5          /* enemies occupy columns 5..9 */
#define ENEMY_COUNT (ENEMY_ROWS * ENEMY_COLS)
#define MAX_BULLETS 16
#define MAX_TOKEN_LEN 64
#define MAX_TICKS 128
#define DEFAULT_CMDS "LFRFLFSRFLFLSFRFL"

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

enum { CMD_STILL = 0, CMD_LEFT = 1, CMD_RIGHT = 2, CMD_FIRE = 3 };

static int cmd_code(char ch) {
    if (ch == 'L' || ch == 'l')
        return CMD_LEFT;
    if (ch == 'R' || ch == 'r')
        return CMD_RIGHT;
    if (ch == 'F' || ch == 'f')
        return CMD_FIRE;
    if (ch == 'S' || ch == 's' || ch == '.')
        return CMD_STILL;
    return -1;
}

static const char *cmd_name(int code) {
    if (code == CMD_LEFT)
        return "L";
    if (code == CMD_RIGHT)
        return "R";
    if (code == CMD_FIRE)
        return "F";
    return "S";
}

static int enemy_value(int row) {
    if (row == 0)
        return 30;
    if (row == 1)
        return 20;
    return 10;
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

/* Bullets are packed as (by << 6) | bx; bx < 64 and by < 64, so no division is
 * needed to decode them. */
class Invaders {
    int enemies[ENEMY_ROWS][ENEMY_COLS];
    int bullets[MAX_BULLETS];
    int bullet_used[MAX_BULLETS];
    int ship;
    int score;
    int remaining;
    int fired;
    int hits;
    int shots_lost;
    int ticks;
    int won;

    Invaders(void) {
        int r;
        int c;
        int i;
        for (r = 0; r < ENEMY_ROWS; r++)
            for (c = 0; c < ENEMY_COLS; c++)
                this->enemies[r][c] = 1;
        for (i = 0; i < MAX_BULLETS; i++) {
            this->bullets[i] = 0;
            this->bullet_used[i] = 0;
        }
        this->ship = (int)(rnext() & 15u); /* fixed seed -> deterministic */
        this->score = 0;
        this->remaining = ENEMY_COUNT;
        this->fired = 0;
        this->hits = 0;
        this->shots_lost = 0;
        this->ticks = 0;
        this->won = 0;
    }

    void spawn_bullet(void) {
        int i;
        for (i = 0; i < MAX_BULLETS; i++) {
            if (!this->bullet_used[i]) {
                int by = H - 2;
                this->bullets[i] = this->ship | (by << 6);
                this->bullet_used[i] = 1;
                this->fired++;
                return;
            }
        }
        /* Pool full: the shot is ignored (a bounded, deterministic event). */
    }

    /* Apply one command and advance every live bullet one row. */
    void step(int cmd, const char **event) {
        int i;
        this->ticks++;
        *event = "none";

        if (cmd == CMD_LEFT) {
            if (this->ship > 0)
                this->ship--;
        } else if (cmd == CMD_RIGHT) {
            if (this->ship < W - 1)
                this->ship++;
        } else if (cmd == CMD_FIRE) {
            this->spawn_bullet();
            *event = "fire";
        }

        /* Walk backwards so removal by index cannot skip a bullet. */
        for (i = MAX_BULLETS - 1; i >= 0; i--) {
            int bx;
            int by;
            if (!this->bullet_used[i])
                continue;
            bx = this->bullets[i] & 63;
            by = this->bullets[i] >> 6;
            by--;
            if (by < 0) {
                this->bullet_used[i] = 0;
                this->shots_lost++;
                *event = "off";
                continue;
            }
            if (by < ENEMY_ROWS) {
                int ec = bx - ENEMY_BASE;
                if (ec >= 0 && ec < ENEMY_COLS && this->enemies[by][ec]) {
                    this->enemies[by][ec] = 0;
                    this->remaining--;
                    this->score += enemy_value(by);
                    this->hits++;
                    this->bullet_used[i] = 0;
                    *event = "hit";
                    if (this->remaining == 0)
                        this->won = 1;
                    continue;
                }
            }
            this->bullets[i] = bx | (by << 6);
        }
    }

    int live_bullets(void) {
        int i;
        int n = 0;
        for (i = 0; i < MAX_BULLETS; i++)
            if (this->bullet_used[i])
                n++;
        return n;
    }

    void render(void) {
        int r;
        int c;
        for (r = 0; r < H; r++) {
            printf("  ");
            for (c = 0; c < W; c++) {
                char ch = '.';
                if (r < ENEMY_ROWS && c >= ENEMY_BASE &&
                    c < ENEMY_BASE + ENEMY_COLS &&
                    this->enemies[r][c - ENEMY_BASE])
                    ch = 'E';
                {
                    int i;
                    for (i = 0; i < MAX_BULLETS; i++) {
                        if (this->bullet_used[i]) {
                            int bx = this->bullets[i] & 63;
                            int by = this->bullets[i] >> 6;
                            if (by == r && bx == c)
                                ch = '|';
                        }
                    }
                }
                if (r == H - 1 && c == this->ship)
                    ch = 'A';
                putchar(ch);
            }
            printf("\n");
        }
    }

    int ship_col(void) { return this->ship; }
    int score_value(void) { return this->score; }
    int enemies_left(void) { return this->remaining; }
    int fired_count(void) { return this->fired; }
    int hit_count(void) { return this->hits; }
    int lost_count(void) { return this->shots_lost; }
    int tick_count(void) { return this->ticks; }
    int is_won(void) { return this->won; }
};

static int refuse(const char *message) {
    error err = error();
    err.set(CPLUS_EXIT_UNSUPPORTED, message);
    fprintf(stderr, "invaders: refused: %s\n", err.what());
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
    rng_reseed(INVADERS_SEED);
    vector cmds = vector();
    Invaders game = Invaders();
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
                return refuse("invalid command in stdin (expected L/R/S/F)");
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
                    return refuse("invalid command token (expected L/R/S/F)");
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

    printf("INVADERS seed=0x%08X grid=%dx%d formation=%dx%d enemies=%d max_ticks=%d cmds=%d\n",
           (unsigned)INVADERS_SEED, W, H, ENEMY_ROWS, ENEMY_COLS,
           ENEMY_COUNT, MAX_TICKS, (int)cmds.size());
    printf("INIT ship=%d score=0 enemies=%d bullets=%d\n",
           game.ship_col(), game.enemies_left(), game.live_bullets());
    game.render();

    for (i = 0; i < (int)cmds.size(); i++) {
        int code = cmds.at((size_t)i);
        game.step(code, &event);
        printf("TICK %d cmd=%s ship=%d bullets=%d event=%s score=%d enemies=%d\n",
               i + 1, cmd_name(code), game.ship_col(), game.live_bullets(),
               event, game.score_value(), game.enemies_left());
        game.render();
        if (game.is_won())
            break;
    }

    if (game.is_won())
        outcome = "win";
    printf("FINAL outcome=%s ticks=%d score=%d enemies=%d fired=%d hits=%d "
           "shots_lost=%d\n",
           outcome, game.tick_count(), game.score_value(), game.enemies_left(),
           game.fired_count(), game.hit_count(), game.lost_count());
    return 0;
}
