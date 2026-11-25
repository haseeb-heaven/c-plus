/* breakout_ui.cp -- deterministic Breakout teaching game with a headless
 * SDL3 software-rendered playfield and a Nuklear HUD overlay.
 *
 * This is NOT a full interactive game.  There is no keyboard/event loop, no
 * wall clock, no network and no randomness beyond a fixed seed used only for
 * the initial ball direction.  Under SDL_VIDEODRIVER=dummy every run renders
 * real frames (SDL3 software renderer + Nuklear SDL3 backend) and writes a
 * byte-identical transcript of score / remaining bricks / paddle state.  The
 * HUD text lines are fed to Nuklear with gui::label and echoed to stdout as
 * fixed `HUD ...` lines, so the overlay is real but deterministic.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/modern_gui/games/breakout_ui/breakout_ui.cp \
 *     --strict --sanitize=none -I. \
 *     $(pkg-config --cflags --libs sdl3) \
 *     -Ithird_party/nuklear build/ext/libnuklear.a \
 *     -o apps/legacy_examples/modern_gui/games/breakout_ui/bin/breakout_ui
 *
 * Run (headless):
 *   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
 *     apps/legacy_examples/modern_gui/games/breakout_ui/bin/breakout_ui [OPTIONS] [CMDS]
 *
 * Options (before commands):
 *   --grid WxH     brick grid, 4<=W<=32, 1<=H<=8, W*H<=256 (default 16x4)
 *   --probe-null   exercise null-surface / null-context guards (refuses)
 *   --help         print usage
 * Commands: L/R/S (or '.') as separate argv tokens or one compact token;
 * a single '-' reads one command line from stdin.  No arguments uses the
 * built-in fixed sequence.  Exit 0 on success, 2 on any clean refusal.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define GAME_SEED 0xC0FFEE11u
#define MAX_COLS 32
#define MAX_ROWS 8
#define MAX_BRICKS 256
#define FIELD_H 12
#define PADDLE_W 3
#define START_LIVES 3
#define MAX_TOKEN_LEN 64
#define MAX_TICKS 128
#define WIN_W 320
#define WIN_H 240
#define CELL_W 10
#define ROW_H 20
#define DEFAULT_CMDS "RLSRRLSSLLRRRLSRSSLLRRRLLSRRRLS"

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

/* 0 = ok, -1 = out-of-range command, -2 = oversized token. */
static int parse_cmds(const char *text, vector *out) {
    string txt = string(text);
    size_t i;
    size_t n = txt.size();
    if (n > MAX_TOKEN_LEN)
        return -2;
    for (i = 0; i < n; i++) {
        char ch = txt.at(i);
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

/* Parse WxH.  -1 covers malformed and oversized dimensions alike; the caller
 * reports an "oversized brick dimensions" refusal for hostile grids. */
static int parse_grid(const char *text, int *cols, int *rows) {
    string txt = string(text);
    size_t i;
    size_t n = txt.size();
    int w = 0;
    int h = 0;
    int phase = 0;
    for (i = 0; i < n; i++) {
        char ch = txt.at(i);
        int digit;
        if (ch == 'x' && phase == 0) {
            phase = 1;
            continue;
        }
        if (ch < '0' || ch > '9')
            return -1;
        digit = ch - '0';
        if (phase == 0) {
            w = w * 10 + digit;
            if (w > MAX_COLS)
                return -1;
        } else {
            h = h * 10 + digit;
            if (h > MAX_ROWS)
                return -1;
        }
    }
    if (phase != 1 || w < 4 || w > MAX_COLS || h < 1 || h > MAX_ROWS)
        return -1;
    if (w * h > MAX_BRICKS)
        return -1;
    *cols = w;
    *rows = h;
    return 0;
}

/* ---- deterministic game core ------------------------------------------- */

class BreakoutUI {
    int bricks[MAX_ROWS][MAX_COLS];
    int cols;
    int rows;
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

    BreakoutUI(int c, int r) {
        int row;
        int col;
        unsigned int seed_bit;
        this->cols = c;
        this->rows = r;
        for (row = 0; row < MAX_ROWS; row++) {
            for (col = 0; col < MAX_COLS; col++)
                this->bricks[row][col] = 0;
        }
        for (row = 0; row < r; row++) {
            for (col = 0; col < c; col++)
                this->bricks[row][col] = 1;
        }
        this->remaining = r * c;
        seed_bit = rnext() & 1u;
        this->bx = (int)((unsigned)c >> 1u); /* no variable division */
        this->by = FIELD_H - 3;
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
        this->bx = (int)((unsigned)this->cols >> 1u);
        this->by = FIELD_H - 3;
        this->vx = seed_bit ? 1 : -1;
        this->vy = -1;
    }

    /* One command plus one physics tick; writes a stable event name. */
    void step(int cmd, const char **event) {
        this->ticks++;
        if (cmd == CMD_LEFT) {
            if (this->paddle > 0)
                this->paddle--;
        } else if (cmd == CMD_RIGHT) {
            if (this->paddle < this->cols - PADDLE_W)
                this->paddle++;
        }

        this->bx += this->vx;
        this->by += this->vy;
        *event = "none";

        if (this->bx < 0) {
            this->bx = 0;
            this->vx = 1;
            this->wall_hits++;
            *event = "wall";
        } else if (this->bx >= this->cols) {
            this->bx = this->cols - 1;
            this->vx = -1;
            this->wall_hits++;
            *event = "wall";
        }

        if (this->by < 0) {
            this->by = 0;
            this->vy = 1;
            this->wall_hits++;
            *event = "top";
        }

        if (this->by >= 0 && this->by < this->rows &&
            this->bx >= 0 && this->bx < this->cols &&
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

        if (this->by >= FIELD_H - 1) {
            if (this->bx >= this->paddle &&
                this->bx < this->paddle + PADDLE_W) {
                this->by = FIELD_H - 2;
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

    int brick_at(int row, int col) {
        if (row < 0 || row >= this->rows || col < 0 || col >= this->cols)
            return 0;
        return this->bricks[row][col];
    }

    int cols_count(void) { return this->cols; }
    int rows_count(void) { return this->rows; }
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

/* Fixed HUD strings: the same text is fed to Nuklear and echoed to stdout. */
static void build_hud(BreakoutUI *game, char lines[4][64]) {
    snprintf(lines[0], 64, "HUD SCORE %03d", game->score_value());
    snprintf(lines[1], 64, "HUD LIVES %d", game->lives_left());
    snprintf(lines[2], 64, "HUD BRICKS %03d", game->bricks_left());
    snprintf(lines[3], 64, "HUD PADDLE [%d,%d]", game->paddle_left(),
             game->paddle_left() + PADDLE_W - 1);
}

/* Render one real frame: clear, bricks, paddle, ball, Nuklear overlay,
 * present, readback and a deterministic painted-pixel probe. */
static int render_frame(int frame_no, renderer *draw, gui *ui,
                        BreakoutUI *game, error *err) {
    char lines[4][64];
    unsigned char px[4];
    int i;
    int row;
    int col;
    int draw_ok = 1;
    int hud_ok;
    int shot_ok = 0;
    int painted = 0;
    int shown;

    build_hud(game, lines);

    if (draw->set_color(12, 12, 20, 255, err) != STDLIB_OK)
        draw_ok = 0;
    if (draw->clear(err) != STDLIB_OK)
        draw_ok = 0;

    if (draw->set_color(220, 120, 60, 255, err) != STDLIB_OK)
        draw_ok = 0;
    for (row = 0; row < game->rows_count(); row++) {
        for (col = 0; col < game->cols_count(); col++) {
            if (game->brick_at(row, col) != 0) {
                float bx = (float)(col * CELL_W + 1);
                float by = (float)(row * ROW_H + 2);
                if (draw->fill_rect(bx, by, 8.0f, 16.0f, err) != STDLIB_OK)
                    draw_ok = 0;
            }
        }
    }

    if (draw->set_color(80, 200, 120, 255, err) != STDLIB_OK)
        draw_ok = 0;
    if (draw->fill_rect((float)(game->paddle_left() * CELL_W),
                        (float)((FIELD_H - 1) * ROW_H + 2),
                        (float)(PADDLE_W * CELL_W - 1), 12.0f, err) != STDLIB_OK)
        draw_ok = 0;

    if (draw->set_color(240, 240, 200, 255, err) != STDLIB_OK)
        draw_ok = 0;
    if (draw->fill_rect((float)(game->ball_x() * CELL_W + 1),
                        (float)(game->ball_y() * ROW_H + 1),
                        7.0f, 7.0f, err) != STDLIB_OK)
        draw_ok = 0;

    ui->input_begin();
    ui->input_end();
    shown = ui->window_begin("HUD", 4.0f, 4.0f, 190.0f, 92.0f, 0);
    hud_ok = shown;
    if (shown != 0) {
        ui->layout_row_dynamic(20.0f, 1);
        for (i = 0; i < 4; i++)
            ui->label(lines[i]);
    }
    ui->window_end();
    ui->render(1);
    if (draw->present(err) != STDLIB_OK)
        draw_ok = 0;

    {
        surface shot = draw->read(err);
        if (shot.valid()) {
            shot_ok = 1;
            px[0] = 0;
            px[1] = 0;
            px[2] = 0;
            px[3] = 0;
            if (shot.pixel(game->paddle_left() * CELL_W + 4,
                           (FIELD_H - 1) * ROW_H + 8, px, err) == STDLIB_OK) {
                if (px[0] + px[1] + px[2] > 120)
                    painted = 1;
            } else {
                draw_ok = 0;
            }
            shot.drop();
        } else {
            draw_ok = 0;
        }
    }

    printf("FRAME %03d draw=%s hud=%s shot=%s painted=%d\n", frame_no,
           draw_ok ? "OK" : "FAIL", hud_ok ? "OK" : "FAIL",
           shot_ok ? "OK" : "FAIL", painted);
    for (i = 0; i < 4; i++)
        printf("%s\n", lines[i]);
    return draw_ok && hud_ok && shot_ok && painted ? 1 : 0;
}

/* ---- safety guards (real error* API calls on null handles) -------------- */

static int guard_surface(surface *shot, error *err) {
    unsigned char px[4];
    px[0] = 0;
    px[1] = 0;
    px[2] = 0;
    px[3] = 0;
    if (!shot->valid()) {
        error_fail(err, CPLUS_EXIT_UNSUPPORTED, "null surface handle");
        fprintf(stderr, "REFUSED null-surface: %s\n", err->what());
        return STDLIB_FAIL;
    }
    if (shot->pixel(0, 0, px, err) != STDLIB_OK) {
        fprintf(stderr, "REFUSED surface-pixel: %s\n", err->what());
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

static int guard_context(gui *ui, error *err) {
    if (!ui->valid() || ui->native() == NULL) {
        error_fail(err, CPLUS_EXIT_UNSUPPORTED, "null nuklear context");
        fprintf(stderr, "REFUSED null-context: %s\n", err->what());
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

/* Hostile probe: deliberately hand null handles to both guards. */
static int probe_null(void) {
    error err = error();
    surface blank = surface((SDL_Surface *)0);
    gui probe_ui = gui((SDL_Window *)0, (SDL_Renderer *)0, &err);
    int a = guard_surface(&blank, &err);
    int b = guard_context(&probe_ui, &err);
    printf("GUARD surface=%s context=%s\n",
           a == STDLIB_FAIL ? "REFUSED" : "UNEXPECTED-OK",
           b == STDLIB_FAIL ? "REFUSED" : "UNEXPECTED-OK");
    printf("GUARD-END refused=%d\n",
           (a == STDLIB_FAIL && b == STDLIB_FAIL) ? 2 : 0);
    probe_ui.drop();
    blank.drop();
    if (a == STDLIB_FAIL && b == STDLIB_FAIL)
        return 2;
    return 3;
}

static int refuse(const char *tag, const char *message) {
    error err = error();
    error_fail(&err, CPLUS_EXIT_UNSUPPORTED, message);
    fprintf(stderr, "REFUSED %s: %s\n", tag, err.what());
    return 2;
}

static void print_usage(void) {
    printf("usage: breakout_ui [--grid WxH] [--probe-null] [LRS...|-]\n");
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
    error err = error();
    vector cmds = vector();
    int cols = 16;
    int rows = 4;
    int probe = 0;
    int a = 1;
    int i;
    int rc;
    int frames = 0;
    const char *event = "none";
    const char *outcome = "incomplete";

    rng_reseed(GAME_SEED);

    /* Options are consumed before commands; unknown '--' tokens are refused. */
    while (a < argc) {
        const char *tok = NULL;
        string ts;
        __builtin_memcpy(&tok, argv + a, sizeof(tok));
        if (tok == NULL)
            return refuse("argv", "null argument token");
        ts = string(tok);
        if (ts.equals("--help")) {
            print_usage();
            return 0;
        }
        if (ts.equals("--probe-null")) {
            probe = 1;
            a++;
            continue;
        }
        if (ts.equals("--grid")) {
            const char *gt = NULL;
            if (a + 1 >= argc)
                return refuse("grid", "missing grid dimensions");
            a++;
            __builtin_memcpy(&gt, argv + a, sizeof(gt));
            if (parse_grid(gt, &cols, &rows) != 0)
                return refuse("grid", "oversized brick dimensions");
            a++;
            continue;
        }
        if (ts.size() >= 2 && ts.at(0) == '-' && ts.at(1) == '-')
            return refuse("option", "unknown option");
        break;
    }

    if (probe != 0)
        return probe_null();

    if (a < argc) {
        const char *first = NULL;
        string first_str;
        __builtin_memcpy(&first, argv + a, sizeof(first));
        if (first == NULL)
            return refuse("argv", "null command token");
        first_str = string(first);
        if (first_str.equals("-")) {
            if (a + 1 != argc)
                return refuse("argv", "'-' must be the only command argument");
            rc = read_stdin(&cmds);
            if (rc == -2)
                return refuse("commands", "stdin line is oversized");
            if (rc != 0)
                return refuse("commands", "out-of-range paddle command (expected L/R/S)");
        } else {
            while (a < argc) {
                const char *arg = NULL;
                __builtin_memcpy(&arg, argv + a, sizeof(arg));
                if (arg == NULL)
                    return refuse("argv", "null command token");
                rc = parse_cmds(arg, &cmds);
                if (rc == -2)
                    return refuse("commands", "token is oversized (max 64 characters)");
                if (rc != 0)
                    return refuse("commands", "out-of-range paddle command (expected L/R/S)");
                a++;
            }
        }
    } else {
        rc = parse_cmds(DEFAULT_CMDS, &cmds);
        if (rc != 0)
            return refuse("internal", "default command sequence is invalid");
    }
    if (cmds.size() == 0)
        return refuse("commands", "empty command sequence");
    if (cmds.size() > MAX_TICKS)
        return refuse("commands", "too many commands (max 128 ticks)");

    /* Headless SDL3 + Nuklear setup. */
    sdl core = sdl((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS), &err);
    if (!core.valid())
        return 3;
    window win = window("breakout_ui", WIN_W, WIN_H, SDL_WINDOW_HIDDEN, &err);
    if (!win.valid())
        return 3;
    renderer draw = renderer(win.native(), "software", &err);
    if (!draw.valid())
        return 3;
    gui ui = gui(win.native(), draw.native(), &err);
    if (!ui.valid())
        return 3;
    BreakoutUI game = BreakoutUI(cols, rows);

    printf("GFXBREAKOUT seed=0x%08X grid=%dx%d bricks=%d field=%dx%d "
           "paddle_w=%d lives=%d cmds=%d\n",
           (unsigned)GAME_SEED, cols, rows, cols * rows, cols, FIELD_H,
           PADDLE_W, START_LIVES, (int)cmds.size());
    printf("INIT ball=(%d,%d) v=(%+d,%+d) paddle=[%d,%d] score=0 bricks=%d\n",
           game.ball_x(), game.ball_y(), game.vel_x(), game.vel_y(),
           game.paddle_left(), game.paddle_left() + PADDLE_W - 1,
           game.bricks_left());
    if (render_frame(frames, &draw, &ui, &game, &err) == 0)
        return 3;
    frames++;

    for (i = 0; i < (int)cmds.size(); i++) {
        int code = cmds.at((size_t)i);
        game.step(code, &event);
        printf("TICK %d cmd=%s ball=(%d,%d) v=(%+d,%+d) paddle=[%d,%d] "
               "event=%s score=%d bricks=%d lives=%d\n",
               i + 1, cmd_name(code), game.ball_x(), game.ball_y(),
               game.vel_x(), game.vel_y(), game.paddle_left(),
               game.paddle_left() + PADDLE_W - 1, event,
               game.score_value(), game.bricks_left(), game.lives_left());
        if (render_frame(frames, &draw, &ui, &game, &err) == 0)
            return 3;
        frames++;
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
    printf("GFXBREAKOUT-END frames=%d hud_lines=%d\n", frames, frames * 4);

    ui.drop();
    draw.drop();
    win.drop();
    core.drop();
    cmds.drop();
    return 0;
}
