/* invaders_ui.cp -- deterministic space-invaders teaching game with a
 * Nuklear HUD, rendered headlessly through SDL3 (SDL_VIDEODRIVER=dummy).
 *
 * This is NOT a playable, interactive game and NOT a safety proof.  There is
 * no keyboard/event loop, no audio, no wall clock, no network and no
 * randomness beyond a fixed seed that only picks the initial ship column.
 * The enemy formation is fixed, the bullet pool is bounded, and every later
 * state is a pure function of the fixed command sequence supplied on argv
 * (or stdin).  Identical input therefore yields a byte-identical transcript.
 *
 * The Nuklear layer draws a real HUD window with three fixed text lines
 * ("SCORE n", "LIVES n", "WAVE n") into an SDL3 software renderer.  A surface
 * snapshot is sampled at two fixed pixels so the headless render is observed,
 * not merely assumed.  The text transcript mirrors those same fixed lines.
 *
 * Build (from the repository root, after build/ext/libnuklear.a exists):
 *   ./build/c+ build examples/modern_gui/games/invaders_ui/invaders_ui.cp \
 *       $(pkg-config --cflags --libs sdl3) \
 *       -I. -Ithird_party/nuklear -Lbuild/ext -lnuklear \
 *       -o examples/modern_gui/games/invaders_ui/bin/invaders_ui
 *
 * Run (headless):
 *   SDL_VIDEODRIVER=dummy bin/invaders_ui L R F S F
 *   SDL_VIDEODRIVER=dummy bin/invaders_ui LRF SF        # compact token
 *   printf 'LRFSF\n' | SDL_VIDEODRIVER=dummy bin/invaders_ui -
 *
 * CLI:
 *   --bullets=N        bullet pool capacity, 1..16 (default 16)
 *   --null-surface     fault injection: guard a NULL surface before pixel read
 *   --null-context     fault injection: guard a NULL Nuklear context
 *   --                 end of options
 *   L / R / S(.) / F   commands: move left, move right, still, fire
 *   -                  read one command line from stdin
 *   (no commands)      built-in fixed default sequence
 *
 * Exit 0 on a completed run, 2 on every clean refusal.  Refusals print
 * "REFUSED reason=... code=<stdlib code> category=<stdlib category> ...".
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"

#define SEED 0x13579BDFu
#define W 16
#define H 10
#define ENEMY_ROWS 3
#define ENEMY_COLS 5
#define ENEMY_BASE 5
#define ENEMY_COUNT (ENEMY_ROWS * ENEMY_COLS)
#define MAX_BULLETS 16
#define MAX_TICKS 128
#define MAX_TOKEN_LEN 64
#define START_LIVES 3
#define MISSES_PER_LIFE 4
#define WIN_W 320
#define WIN_H 200
#define DEFAULT_CMDS "LFRFLFSRFLFLSFRFLFFFF"

enum { CMD_STILL = 0, CMD_LEFT = 1, CMD_RIGHT = 2, CMD_FIRE = 3 };

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

/* --- clean, coded refusals ------------------------------------------------ */

static int refuse_with(const char *reason, int category, cplus_exit_code code,
                       const char *message) {
    error e = error();
    error_fail_cat(&e, category, code, message);
    fprintf(stderr, "REFUSED reason=%s code=%d category=%d message=%s\n",
            reason, e.code(), e.category(), e.what());
    return 2;
}

/* Parse a pool request.  0 on success, -1 malformed, -2 out of range. */
static int parse_pool(const char *text, int *out) {
    int v = 0;
    size_t i;
    if (text == NULL || text[0] == '\0')
        return -1;
    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9')
            return -1;
        v = v * 10 + (text[i] - '0');
        if (v > MAX_BULLETS + 1)
            return -2; /* also stops integer overflow early */
    }
    if (v < 1 || v > MAX_BULLETS)
        return -2;
    *out = v;
    return 0;
}

static int append_cmds(const char *text, int *cmds, int *count) {
    size_t i;
    size_t n = strlen(text);
    if (n > MAX_TOKEN_LEN)
        return -2; /* oversized token */
    for (i = 0; i < n; i++) {
        char ch = text[i];
        int code;
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t')
            continue;
        code = cmd_code(ch);
        if (code < 0)
            return -1; /* out-of-range command */
        if (*count >= MAX_TICKS)
            return -3; /* too many ticks */
        cmds[*count] = code;
        (*count)++;
    }
    return 0;
}

/* --- guarded stdlib wrappers: these are the null-handle safety checks ----- */

/* Refuses a NULL/invalid surface before any pixel access. */
static int guarded_pixel(surface *shot, int x, int y, unsigned char *out,
                         error *err) {
    if (shot == NULL || out == NULL || !shot->valid()) {
        error_fail(err, CPLUS_EXIT_UNSUPPORTED, "guarded_pixel: null surface");
        return STDLIB_FAIL;
    }
    return shot->pixel(x, y, out, err);
}

/* Refuses a NULL/invalid Nuklear context before building a HUD frame. */
static int guarded_hud(gui *ui, const char *score_line, const char *lives_line,
                       const char *wave_line, error *err) {
    if (ui == NULL || !ui->valid() || ui->native() == NULL) {
        error_fail(err, CPLUS_EXIT_UNSUPPORTED, "guarded_hud: null context");
        return STDLIB_FAIL;
    }
    if (ui->window_begin("INVADERS HUD", 10.0f, 10.0f, 280.0f, 150.0f, 0)) {
        ui->layout_row_dynamic(26.0f, 1);
        ui->label(score_line);
        ui->layout_row_dynamic(26.0f, 1);
        ui->label(lives_line);
        ui->layout_row_dynamic(26.0f, 1);
        ui->label(wave_line);
    }
    ui->window_end();
    return STDLIB_OK;
}

/* --- deterministic game core --------------------------------------------- */

class Invaders {
    int enemies[ENEMY_ROWS][ENEMY_COLS];
    int bullets[MAX_BULLETS];
    int bullet_used[MAX_BULLETS];
    int pool_capacity;
    int ship;
    int score;
    int lives;
    int wave;
    int remaining;
    int fired;
    int hits;
    int shots_lost;
    int ticks;
    int won;

    Invaders(int pool) {
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
        this->pool_capacity = pool;
        this->ship = (int)(rnext() & 15u); /* fixed seed -> deterministic */
        this->score = 0;
        this->lives = START_LIVES;
        this->wave = 1;
        this->remaining = ENEMY_COUNT;
        this->fired = 0;
        this->hits = 0;
        this->shots_lost = 0;
        this->ticks = 0;
        this->won = 0;
    }

    int pool(void) { return this->pool_capacity; }
    int ship_col(void) { return this->ship; }
    int score_value(void) { return this->score; }
    int lives_value(void) { return this->lives; }
    int wave_value(void) { return this->wave; }
    int enemies_left(void) { return this->remaining; }
    int fired_count(void) { return this->fired; }
    int hit_count(void) { return this->hits; }
    int lost_count(void) { return this->shots_lost; }
    int tick_count(void) { return this->ticks; }
    int is_won(void) { return this->won; }

    int live_bullets(void) {
        int i;
        int n = 0;
        for (i = 0; i < this->pool_capacity; i++)
            if (this->bullet_used[i])
                n++;
        return n;
    }

    void spawn_bullet(void) {
        int i;
        for (i = 0; i < this->pool_capacity; i++) {
            if (!this->bullet_used[i]) {
                int by = H - 2;
                this->bullets[i] = this->ship | (by << 6);
                this->bullet_used[i] = 1;
                this->fired++;
                return;
            }
        }
        /* Pool full: the shot is ignored (bounded, deterministic event). */
    }

    void next_wave(void) {
        int r;
        int c;
        int i;
        for (r = 0; r < ENEMY_ROWS; r++)
            for (c = 0; c < ENEMY_COLS; c++)
                this->enemies[r][c] = 1;
        for (i = 0; i < this->pool_capacity; i++)
            this->bullet_used[i] = 0;
        this->remaining = ENEMY_COUNT;
        this->wave++;
    }

    /* Apply one command, advance bullets, update lives/wave deterministically. */
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

        for (i = this->pool_capacity - 1; i >= 0; i--) {
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
                    if (this->remaining == 0) {
                        this->won = 1;
                        this->next_wave();
                        this->won = 0; /* continuous waves: never end early */
                        *event = "wave";
                    }
                    continue;
                }
            }
            this->bullets[i] = bx | (by << 6);
        }

        /* Lives are a pure function of misses: every 4 shots lost to space
         * costs one life.  This keeps the HUD LIVES line deterministic. */
        this->lives = START_LIVES - this->shots_lost / MISSES_PER_LIFE;
        if (this->lives < 0)
            this->lives = 0;
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
                    for (i = 0; i < this->pool_capacity; i++) {
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
};

/* --- main ----------------------------------------------------------------- */

int main(int argc, char **argv) {
    int cmds[MAX_TICKS];
    int count = 0;
    int pool = MAX_BULLETS;
    int inject_null_surface = 0;
    int inject_null_context = 0;
    int options_done = 0;
    int a;
    int i;
    int rc;

    rng_reseed(SEED);

    /* 1. Parse options and commands, refusing malformed input early. */
    for (a = 1; a < argc; a++) {
        const char *arg = NULL;
        __builtin_memcpy(&arg, argv + a, sizeof(arg));
        if (arg == NULL)
            return refuse_with("null-command", STDLIB_CATEGORY_INVALID,
                               CPLUS_EXIT_UNSUPPORTED, "null command token");

        if (!options_done && strcmp(arg, "--") == 0) {
            options_done = 1;
            continue;
        }
        if (!options_done && strcmp(arg, "--null-surface") == 0) {
            inject_null_surface = 1;
            continue;
        }
        if (!options_done && strcmp(arg, "--null-context") == 0) {
            inject_null_context = 1;
            continue;
        }
        if (!options_done && strncmp(arg, "--bullets=", 10) == 0) {
            rc = parse_pool(arg + 10, &pool);
            if (rc == -2)
                return refuse_with("pool-oversized", STDLIB_CATEGORY_BOUNDS,
                                   CPLUS_EXIT_BOUNDS,
                                   "bullet pool request out of range 1..16");
            if (rc != 0)
                return refuse_with("pool-malformed", STDLIB_CATEGORY_INVALID,
                                   CPLUS_EXIT_UNSUPPORTED,
                                   "bullet pool request is not a decimal number");
            continue;
        }
        if (!options_done && strcmp(arg, "-") == 0) {
            char buf[MAX_TOKEN_LEN + 4];
            size_t got = 0;
            if (argc != a + 1)
                return refuse_with("stdin-args", STDLIB_CATEGORY_INVALID,
                                   CPLUS_EXIT_UNSUPPORTED,
                                   "'-' must be the last argument");
            if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
                return refuse_with("stdin-empty", STDLIB_CATEGORY_INVALID,
                                   CPLUS_EXIT_UNSUPPORTED, "no stdin line");
            while (buf[got] != '\0' && got < sizeof(buf))
                got++;
            if (got >= sizeof(buf) - 1)
                return refuse_with("stdin-oversized", STDLIB_CATEGORY_BOUNDS,
                                   CPLUS_EXIT_BOUNDS, "stdin line is oversized");
            while (got > 0 && (buf[got - 1] == '\n' || buf[got - 1] == '\r')) {
                got--;
                buf[got] = '\0';
            }
            if (got > MAX_TOKEN_LEN)
                return refuse_with("token-oversized", STDLIB_CATEGORY_BOUNDS,
                                   CPLUS_EXIT_BOUNDS, "token exceeds 64 characters");
            rc = append_cmds(buf, cmds, &count);
            if (rc == -2)
                return refuse_with("token-oversized", STDLIB_CATEGORY_BOUNDS,
                                   CPLUS_EXIT_BOUNDS, "token exceeds 64 characters");
            if (rc == -3)
                return refuse_with("too-many-ticks", STDLIB_CATEGORY_BOUNDS,
                                   CPLUS_EXIT_BOUNDS, "too many commands (max 128)");
            if (rc != 0)
                return refuse_with("bad-command", STDLIB_CATEGORY_INVALID,
                                   CPLUS_EXIT_UNSUPPORTED,
                                   "command out of range (expected L/R/S/F)");
            continue;
        }

        rc = append_cmds(arg, cmds, &count);
        if (rc == -2)
            return refuse_with("token-oversized", STDLIB_CATEGORY_BOUNDS,
                               CPLUS_EXIT_BOUNDS, "token exceeds 64 characters");
        if (rc == -3)
            return refuse_with("too-many-ticks", STDLIB_CATEGORY_BOUNDS,
                               CPLUS_EXIT_BOUNDS, "too many commands (max 128)");
        if (rc != 0)
            return refuse_with("bad-command", STDLIB_CATEGORY_INVALID,
                               CPLUS_EXIT_UNSUPPORTED,
                               "command out of range (expected L/R/S/F)");
    }

    if (count == 0 && argc > 1) {
        /* Options alone are allowed (fault-injection runs); otherwise a token
         * that yielded no commands is a malformed command sequence. */
        int has_command_token = 0;
        for (a = 1; a < argc; a++) {
            const char *arg = NULL;
            __builtin_memcpy(&arg, argv + a, sizeof(arg));
            if (arg != NULL && arg[0] != '-')
                has_command_token = 1;
        }
        if (has_command_token)
            return refuse_with("empty-commands", STDLIB_CATEGORY_INVALID,
                               CPLUS_EXIT_UNSUPPORTED, "empty command sequence");
    }
    if (count == 0)
        rc = append_cmds(DEFAULT_CMDS, cmds, &count);
    if (count == 0 || count > MAX_TICKS)
        return refuse_with("too-many-ticks", STDLIB_CATEGORY_BOUNDS,
                           CPLUS_EXIT_BOUNDS, "command count out of range 1..128");

    {
        error err = error();
        sdl core = sdl(SDL_INIT_VIDEO, &err);
        window win = window("invaders_ui HUD", WIN_W, WIN_H,
                            SDL_WINDOW_HIDDEN, &err);
        renderer draw = renderer(win.native(), "software", &err);
        gui ui = gui(win.native(), draw.native(), &err);
        Invaders game = Invaders(pool);
        const char *driver = SDL_GetCurrentVideoDriver();
        int frames = 0;

        if (!core.valid()) {
            fprintf(stderr, "GFX_INVADERS_SETUP core failed: %s\n", err.what());
            return 3;
        }
        if (!win.valid() || !draw.valid() || !ui.valid()) {
            fprintf(stderr, "GFX_INVADERS_SETUP window/renderer/gui failed: %s\n",
                    err.what());
            ui.drop();
            draw.drop();
            win.drop();
            core.drop();
            return 3;
        }

        /* 2. Fault-injection refusals: the guards are exercised with genuinely
         *    null/invalid handles, and report the wrapped error's own codes. */
        if (inject_null_surface) {
            unsigned char px[4];
            error ge = error();
            surface nullshot = surface(NULL);
            if (guarded_pixel(&nullshot, 4, 4, px, &ge) == STDLIB_FAIL) {
                fprintf(stderr,
                        "REFUSED reason=null-surface code=%d category=%d message=%s\n",
                        ge.code(), ge.category(), ge.what());
                nullshot.drop();
                ui.drop();
                draw.drop();
                win.drop();
                core.drop();
                return 2;
            }
            nullshot.drop();
        }
        if (inject_null_context) {
            error ge = error();
            error ge2 = error();
            gui nullui = gui(NULL, NULL, &ge2);
            if (guarded_hud(&nullui, "SCORE 0", "LIVES 3", "WAVE 1", &ge) == STDLIB_FAIL) {
                fprintf(stderr,
                        "REFUSED reason=null-context code=%d category=%d message=%s\n",
                        ge.code(), ge.category(), ge.what());
                nullui.drop();
                ui.drop();
                draw.drop();
                win.drop();
                core.drop();
                return 2;
            }
            nullui.drop();
        }

        /* 3. Deterministic transcript. */
        printf("GFX_INVADERS seed=0x%08X grid=%dx%d formation=%dx%d enemies=%d "
               "pool=%d max_ticks=%d cmds=%d driver=%s\n",
               (unsigned)SEED, W, H, ENEMY_ROWS, ENEMY_COLS, ENEMY_COUNT,
               game.pool(), MAX_TICKS, count,
               driver != NULL ? driver : "unknown");
        printf("INIT ship=%d score=%d lives=%d wave=%d enemies=%d bullets=%d\n",
               game.ship_col(), game.score_value(), game.lives_value(),
               game.wave_value(), game.enemies_left(), game.live_bullets());
        game.render();

        for (i = 0; i < count; i++) {
            int code = cmds[i];
            const char *event = "none";
            char line_score[64];
            char line_lives[64];
            char line_wave[64];
            unsigned char inside[4];
            unsigned char outside[4];
            error ge = error();
            surface shot = surface(NULL);

            game.step(code, &event);
            snprintf(line_score, sizeof(line_score), "SCORE %d", game.score_value());
            snprintf(line_lives, sizeof(line_lives), "LIVES %d", game.lives_value());
            snprintf(line_wave, sizeof(line_wave), "WAVE %d", game.wave_value());

            ui.input_begin();
            ui.input_end();
            if (guarded_hud(&ui, line_score, line_lives, line_wave, &ge) != STDLIB_OK) {
                fprintf(stderr, "REFUSED reason=hud-render code=%d category=%d message=%s\n",
                        ge.code(), ge.category(), ge.what());
                ui.drop();
                draw.drop();
                win.drop();
                core.drop();
                return 2;
            }

            draw.set_color(12, 24, 48, 255, &err);
            draw.clear(&err);
            ui.render(0);
            shot = draw.read(&err);
            draw.present(&err);
            frames++;

            if (!shot.valid()) {
                fprintf(stderr, "REFUSED reason=surface-read code=%d category=%d message=%s\n",
                        err.code(), err.category(), err.what());
                shot.drop();
                ui.drop();
                draw.drop();
                win.drop();
                core.drop();
                return 2;
            }
            if (guarded_pixel(&shot, 20, 20, inside, &ge) != STDLIB_OK ||
                guarded_pixel(&shot, 2, 2, outside, &ge) != STDLIB_OK) {
                fprintf(stderr, "REFUSED reason=pixel-read code=%d category=%d message=%s\n",
                        ge.code(), ge.category(), ge.what());
                shot.drop();
                ui.drop();
                draw.drop();
                win.drop();
                core.drop();
                return 2;
            }

            printf("TICK %d cmd=%s ship=%d bullets=%d event=%s score=%d lives=%d "
                   "wave=%d enemies=%d\n",
                   i + 1, cmd_name(code), game.ship_col(), game.live_bullets(),
                   event, game.score_value(), game.lives_value(),
                   game.wave_value(), game.enemies_left());
            printf("HUD %s\n", line_score);
            printf("HUD %s\n", line_lives);
            printf("HUD %s\n", line_wave);
            printf("PIXEL hud_nonbg=%d\n",
                   (inside[0] != outside[0] || inside[1] != outside[1] ||
                    inside[2] != outside[2]) ? 1 : 0);
            game.render();
            shot.drop();
        }

        printf("FINAL outcome=%s ticks=%d score=%d lives=%d wave=%d enemies=%d "
               "fired=%d hits=%d lost=%d frames=%d driver=%s gui=1 surface=1\n",
               "complete", game.tick_count(), game.score_value(),
               game.lives_value(), game.wave_value(), game.enemies_left(),
               game.fired_count(), game.hit_count(), game.lost_count(), frames,
               driver != NULL ? driver : "unknown");

        ui.drop();
        draw.drop();
        win.drop();
        core.drop();
    }
    return 0;
}
