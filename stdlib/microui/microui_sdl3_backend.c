/* Real microui GUI backend: vendored microui + SDL3 renderer, plain C ABI.
 * Renders mu_Command lists with SDL3 primitives and a built-in 6x6 font. */
#include <SDL3/SDL.h>
#include "microui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MU {
    SDL_Window *win;
    SDL_Renderer *renderer;
    mu_Context ctx;
    int events;
    int frames;
};

#include "mu_font.h"

static int mu_text_width(mu_Font f, const char *s, int len) {
    (void)f; int n = len < 0 ? (int)strlen(s) : len; return n * 7;
}
static int mu_text_height(mu_Font f) { (void)f; return 8; }

static void draw_glyph(SDL_Renderer *r, int x, int y,
                       const unsigned char bits[6][6], mu_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 6; ++col)
            if (bits[row][col])
                SDL_RenderFillRect(r, &(SDL_FRect){(float)(x + col), (float)(y + row), 1.0f, 1.0f});
}

static void draw_text(SDL_Renderer *r, int x, int y, const char *s,
                      mu_Color c) {
    for (; *s; ++s, x += 7) {
        unsigned char ch = (unsigned char)*s;
        draw_glyph(r, x, y, MU_FONT[ch < 128 ? ch : 0], c);
    }
}

static void render(mu_Context *ctx, SDL_Renderer *r) {
    mu_Command *cmd = NULL;
    while (mu_next_command(ctx, &cmd)) {
        switch (cmd->type) {
        case MU_COMMAND_RECT: {
            mu_Rect b = cmd->rect.rect; mu_Color c = cmd->rect.color;
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            SDL_FRect s = {(float)b.x, (float)b.y, (float)b.w, (float)b.h};
            SDL_RenderFillRect(r, &s);
            break;
        }
        case MU_COMMAND_TEXT: {
            draw_text(r, cmd->text.pos.x, cmd->text.pos.y, cmd->text.str,
                      cmd->text.color);
            break;
        }
        default: break;
        }
    }
}

/* ---------- Calculator state and panel ---------- */
typedef struct {
    char display[64];
    double acc;
    int op;           /* 0 none, 1 +, 2 -, 3 *, 4 / */
    int waiting;
} Calc;
static Calc calc = {{0}, 0, 0, 1};

static void calc_init(void) {
    strcpy(calc.display, "0"); calc.acc = 0; calc.op = 0; calc.waiting = 1;
}
static void calc_digit(int d) {
    if (calc.waiting) { snprintf(calc.display, sizeof(calc.display), "%d", d); calc.waiting = 0; }
    else { size_t n = strlen(calc.display); if (n < 60) { calc.display[n] = (char)('0' + d); calc.display[n+1] = 0; } }
}
static void calc_op(int op) {
    double v = atof(calc.display);
    if (calc.op && !calc.waiting) {
        if (calc.op == 1) calc.acc += v; else if (calc.op == 2) calc.acc -= v;
        else if (calc.op == 3) calc.acc *= v; else if (calc.op == 4 && v != 0) calc.acc /= v;
    } else calc.acc = v;
    calc.op = op; calc.waiting = 1;
    snprintf(calc.display, sizeof(calc.display), "%.6g", calc.acc);
}
static void calc_equals(void) {
    if (calc.op) calc_op(0);
    calc.op = 0; calc.waiting = 1;
}

static void calc_panel(mu_Context *ctx) {
    if (mu_begin_window(ctx, "Calculator", mu_rect(460, 40, 280, 300))) {
        mu_layout_row(ctx, 1, (int[]){-1}, 0);
        mu_text(ctx, calc.display);
        const char *labels[16] = {"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
        for (int i = 0; i < 16; i += 4) {
            mu_layout_row(ctx, 4, (int[]){-1,-1,-1,-1}, 0);
            for (int j = 0; j < 4; ++j) {
                const char *l = labels[i + j];
                if (!mu_button(ctx, l)) continue;
                if (l[0] >= '0' && l[0] <= '9') calc_digit(l[0] - '0');
                else if (l[0] == '+') calc_op(1); else if (l[0] == '-') calc_op(2);
                else if (l[0] == '*') calc_op(3); else if (l[0] == '/') calc_op(4);
                else if (l[0] == '=') calc_equals(); else if (l[0] == 'C') calc_init();
            }
        }
        mu_end_window(ctx);
    }
}

/* ---------- Notes state and panel ---------- */
static char notes_text[4096] = "Project IGI notes:\n- stdlib hardening done\n- real GUI demos\n";
static void notes_panel(mu_Context *ctx) {
    if (mu_begin_window(ctx, "Notes", mu_rect(40, 330, 390, 210))) {
        mu_layout_row(ctx, 1, (int[]){-1}, 120);
        mu_textbox(ctx, notes_text, (int)sizeof(notes_text));
        mu_layout_row(ctx, 1, (int[]){-1}, 0);
        mu_label(ctx, "microui + SDL3 - real immediate-mode UI");
        mu_end_window(ctx);
    }
}

/* ---------- C ABI ---------- */
void *mi_init(void) {
    struct MU *m = (struct MU *)calloc(1, sizeof(*m));
    if (!m) return 0;
    if (!SDL_Init(SDL_INIT_VIDEO)) { free(m); return 0; }
    m->win = SDL_CreateWindow("C+ microui", 800, 600, 0);
    m->renderer = m->win ? SDL_CreateRenderer(m->win, 0) : 0;
    if (!m->renderer) { free(m); return 0; }
    mu_init(&m->ctx);
    m->ctx.text_width = mu_text_width;
    m->ctx.text_height = mu_text_height;
    calc_init();
    return m;
}

int mi_poll(void *p, int *quit) {
    struct MU *m = (struct MU *)p;
    SDL_Event ev; int n = 0;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) *quit = 1;
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) *quit = 1;
        if (ev.type == SDL_EVENT_MOUSE_MOTION)
            mu_input_mousemove(&m->ctx, (int)ev.motion.x, (int)ev.motion.y);
        if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            mu_input_mousedown(&m->ctx, (int)ev.button.x, (int)ev.button.y, MU_MOUSE_LEFT);
        if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP)
            mu_input_mouseup(&m->ctx, (int)ev.button.x, (int)ev.button.y, MU_MOUSE_LEFT);
        if (ev.type == SDL_EVENT_TEXT_INPUT)
            mu_input_text(&m->ctx, ev.text.text);
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            if (ev.key.key == SDLK_BACKSPACE) mu_input_keydown(&m->ctx, MU_KEY_BACKSPACE); mu_input_keyup(&m->ctx, MU_KEY_BACKSPACE);
            if (ev.key.key == SDLK_RETURN) mu_input_keydown(&m->ctx, MU_KEY_RETURN); mu_input_keyup(&m->ctx, MU_KEY_RETURN);
        }
        m->events++; n++;
    }
    return n;
}

void mi_frame(void *p, int proof) {
    struct MU *m = (struct MU *)p;
    mu_begin(&m->ctx);
    if (mu_begin_window(&m->ctx, "microui demo", mu_rect(20, 20, 400, 280))) {
        mu_layout_row(&m->ctx, 1, (int[]){-1}, 0);
        mu_label(&m->ctx, "Real microui library (calculator + notes)");
        if (mu_button(&m->ctx, "Click me")) mu_label(&m->ctx, "clicked!");
        mu_end_window(&m->ctx);
    }
    calc_panel(&m->ctx);
    notes_panel(&m->ctx);
    mu_end(&m->ctx);
    SDL_SetRenderDrawColor(m->renderer, 16, 20, 32, 255);
    SDL_RenderClear(m->renderer);
    render(&m->ctx, m->renderer);
    SDL_RenderPresent(m->renderer);
    m->frames++;
    (void)proof;
}

void mi_shot(void *p, const char *path) {
    struct MU *m = (struct MU *)p;
    SDL_Surface *s = SDL_RenderReadPixels(m->renderer, 0);
    if (s) { SDL_IOStream *io = SDL_IOFromFile(path, "wb"); if (io) SDL_SaveBMP_IO(s, io, 1); SDL_DestroySurface(s); }
}
int mi_frames(const void *p) { return ((const struct MU *)p)->frames; }
int mi_events(const void *p) { return ((const struct MU *)p)->events; }
const char *mi_calc_display(void) { return calc.display; }

void mi_drop(void *p) {
    struct MU *m = (struct MU *)p;
    if (!m) return;
    if (m->renderer) SDL_DestroyRenderer(m->renderer);
    if (m->win) SDL_DestroyWindow(m->win);
    free(m);
    SDL_Quit();
}
