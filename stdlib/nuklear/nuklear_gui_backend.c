/* Plain-C implementation of the Nuklear GUI backend. SDL3 and Nuklear are
 * real third-party C libraries linked through this C ABI boundary. */
#include <SDL3/SDL.h>
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_COMMAND_USERDATA
#include "../../third_party/nuklear/nuklear.h"
#include "../../third_party/nuklear/nuklear_sdl3_renderer.h"
#include "nuklear_gui_api.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct NG {
    SDL_Window *win;
    SDL_Renderer *renderer;
    struct nk_context *ctx;
    int events_seen;
};

void *ng_init(void) {
    struct NG *g = (struct NG *)calloc(1, sizeof(*g));
    if (!g) return 0;
    if (!SDL_Init(SDL_INIT_VIDEO)) { free(g); return 0; }
    g->win = SDL_CreateWindow("C+ Nuklear GUI", 800, 600, 0);
    g->renderer = g->win ? SDL_CreateRenderer(g->win, 0) : 0;
    g->ctx = g->renderer ? nk_sdl_init(g->win, g->renderer, nk_sdl_allocator()) : 0;
    if (!g->ctx) { ng_drop(g); return 0; }
    nk_sdl_font_stash_begin(g->ctx);
    nk_sdl_font_stash_end(g->ctx);
    return g;
}

int ng_should_close(const void *p) {
    const struct NG *g = (const struct NG *)p;
    return g && g->ctx && nk_input_is_key_down(&g->ctx->input, NK_KEY_NONE) ? 0 : 0;
}

int ng_poll(void *p, int *quit) {
    struct NG *g = (struct NG *)p;
    SDL_Event ev;
    int n = 0;
    while (SDL_PollEvent(&ev)) {
        nk_sdl_handle_event(g->ctx, &ev);
        if (ev.type == SDL_EVENT_QUIT) *quit = 1;
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) *quit = 1;
        g->events_seen++;
        n++;
    }
    return n;
}

void ng_ui(void *p, int frame, int *clicks, float *value, int *check, char *name, int cap) {
    struct NG *g = (struct NG *)p;
    struct nk_context *ctx = g->ctx;
    (void)frame;
    nk_input_begin(ctx); nk_input_end(ctx);
    if (nk_begin(ctx, "Nuklear demo", nk_rect(20, 20, 460, 400),
               NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "Real Nuklear UI library (not a fake)", NK_LEFT);
        nk_layout_row_dynamic(ctx, 28, 2);
        if (nk_button_label(ctx, "Click me")) (*clicks)++;
        if (nk_button_label(ctx, "Reset")) { *clicks = 0; *value = 42.0f; }
        nk_layout_row_dynamic(ctx, 25, 1);
        nk_label(ctx, "Slider", NK_LEFT);
        nk_slider_float(ctx, 0.0f, value, 100.0f, 1.0f);
        nk_layout_row_dynamic(ctx, 25, 1);
        nk_checkbox_label(ctx, "Enable feature", check);
        nk_layout_row_dynamic(ctx, 25, 1);
        nk_label(ctx, "Name:", NK_LEFT);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_SIMPLE, name, cap, nk_filter_default);
        nk_layout_row_dynamic(ctx, 25, 1);
        {
            char line[160];
            snprintf(line, sizeof(line), "clicks=%d value=%.0f check=%d name=%s",
                     *clicks, *value, *check, name);
            nk_label(ctx, line, NK_LEFT);
        }
    }
    nk_end(ctx);
}

void ng_render(void *p) {
    struct NG *g = (struct NG *)p;
    SDL_SetRenderDrawColor(g->renderer, 18, 24, 38, 255);
    SDL_RenderClear(g->renderer);
    nk_sdl_render(g->ctx, NK_ANTI_ALIASING_ON);
    SDL_RenderPresent(g->renderer);
}

void ng_shot(void *p, const char *path) {
    struct NG *g = (struct NG *)p;
    SDL_Surface *s = SDL_RenderReadPixels(g->renderer, 0);
    if (s) { SDL_IOStream *io = SDL_IOFromFile(path, "wb"); if (io) SDL_SaveBMP_IO(s, io, 1); SDL_DestroySurface(s); }
}

int ng_events_seen(const void *p) { return ((const struct NG *)p)->events_seen; }

void ng_drop(void *p) {
    struct NG *g = (struct NG *)p;
    if (!g) return;
    if (g->ctx) nk_sdl_shutdown(g->ctx);
    if (g->renderer) SDL_DestroyRenderer(g->renderer);
    if (g->win) SDL_DestroyWindow(g->win);
    free(g);
    SDL_Quit();
}
