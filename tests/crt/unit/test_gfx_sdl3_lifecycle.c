/*
 * Headless lifecycle regression tests for stdlib/gfx_sdl3.hp.
 *
 * SDL3's include guard is defined so the real header is skipped; the small
 * stubs below record every SDL call and keep handle liveness, which makes
 * window leaks, double-destroys, NULL-handle calls and premature SDL_Quit()
 * deterministic failures without a display or a real SDL build.
 */
#define SDL_h_
#define CPLUS_GFX_SDL3_SDL_DECLARED 1
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_FRect {
    float x;
    float y;
    float w;
    float h;
} SDL_FRect;

#define SDL_INIT_VIDEO 0x00000020u

int SDL_Init(uint32_t flags);
void SDL_Quit(void);
SDL_Window *SDL_CreateWindow(const char *title, int w, int h, uint32_t flags);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name);
void SDL_DestroyWindow(SDL_Window *window);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
int SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t red, uint8_t green,
                           uint8_t blue, uint8_t alpha);
int SDL_RenderClear(SDL_Renderer *r);
int SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rect);
void SDL_RenderDebugText(SDL_Renderer *r, float x, float y, const char *text);
void SDL_RenderPresent(SDL_Renderer *r);

#include "stdlib/gfx_sdl3.hp"

static int g_fail = 0;
#define CHECK(cond, label)                                                    \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s\n", label);                                       \
            ++g_fail;                                                         \
        } else {                                                              \
            printf("PASS %s\n", label);                                       \
        }                                                                     \
    } while (0)

#define MAXH 16
static int g_init_calls, g_quit_calls, g_initialized;
static int g_init_fail, g_window_fail, g_renderer_fail;
static int g_create_win, g_create_ren, g_destroy_win, g_destroy_ren;
static int g_bad_destroy, g_null_draw, g_draw_calls, g_present_calls, g_text_calls;
static unsigned char g_win_alive[MAXH], g_ren_alive[MAXH];

static void reset(void) {
    g_init_calls = g_quit_calls = g_initialized = 0;
    g_init_fail = g_window_fail = g_renderer_fail = 0;
    g_create_win = g_create_ren = g_destroy_win = g_destroy_ren = 0;
    g_bad_destroy = g_null_draw = g_draw_calls = g_present_calls = g_text_calls = 0;
    memset(g_win_alive, 0, sizeof(g_win_alive));
    memset(g_ren_alive, 0, sizeof(g_ren_alive));
}

static int slot(unsigned char *alive) {
    int i;
    for (i = 0; i < MAXH; ++i)
        if (!alive[i]) {
            alive[i] = 1;
            return i;
        }
    return -1;
}

static int live(const void *p, unsigned char *alive) {
    uintptr_t v = (uintptr_t)p;
    if (v == 0 || v > (uintptr_t)MAXH)
        return 0;
    return alive[v - 1] != 0;
}

static void note_draw(SDL_Renderer *r) {
    if (!live(r, g_ren_alive)) {
        ++g_null_draw;
        return;
    }
    ++g_draw_calls;
}

int SDL_Init(uint32_t flags) {
    (void)flags;
    ++g_init_calls;
    if (g_init_fail)
        return 0;
    g_initialized = 1;
    return 1;
}

void SDL_Quit(void) {
    ++g_quit_calls;
    g_initialized = 0;
}

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, uint32_t flags) {
    int i;
    (void)title; (void)w; (void)h; (void)flags;
    ++g_create_win;
    if (g_window_fail)
        return (SDL_Window *)0;
    i = slot(g_win_alive);
    return i < 0 ? (SDL_Window *)0 : (SDL_Window *)(uintptr_t)(i + 1);
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name) {
    int i;
    (void)name;
    ++g_create_ren;
    if (g_renderer_fail || !live(window, g_win_alive))
        return (SDL_Renderer *)0;
    i = slot(g_ren_alive);
    return i < 0 ? (SDL_Renderer *)0 : (SDL_Renderer *)(uintptr_t)(i + 1);
}

void SDL_DestroyWindow(SDL_Window *window) {
    if (!live(window, g_win_alive)) {
        ++g_bad_destroy;
        return;
    }
    ++g_destroy_win;
    g_win_alive[(uintptr_t)window - 1] = 0;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer) {
    if (!live(renderer, g_ren_alive)) {
        ++g_bad_destroy;
        return;
    }
    ++g_destroy_ren;
    g_ren_alive[(uintptr_t)renderer - 1] = 0;
}

int SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t red, uint8_t green,
                           uint8_t blue, uint8_t alpha) {
    (void)red; (void)green; (void)blue; (void)alpha;
    note_draw(r);
    return 1;
}

int SDL_RenderClear(SDL_Renderer *r) {
    note_draw(r);
    return 1;
}

int SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rect) {
    (void)rect;
    note_draw(r);
    return 1;
}

void SDL_RenderDebugText(SDL_Renderer *r, float x, float y, const char *text) {
    (void)x; (void)y;
    if (!live(r, g_ren_alive) || text == (const char *)0) {
        ++g_null_draw;
        return;
    }
    ++g_text_calls;
    ++g_draw_calls;
}

void SDL_RenderPresent(SDL_Renderer *r) {
    if (!live(r, g_ren_alive)) {
        ++g_null_draw;
        return;
    }
    ++g_present_calls;
    ++g_draw_calls;
}

int main(void) {
    cplus_gfx_window w, a, b;
    const char *items[2];
    int draws_before, quit_before, i;

    /* NULL handles never reach SDL. */
    reset();
    CHECK(cplus_gfx_sdl_refs == 0, "start_refs_zero");
    CHECK(cplus_gfx_open((cplus_gfx_window *)0, "x", 8, 8) == 0,
          "open_null_handle");
    cplus_gfx_close((cplus_gfx_window *)0);
    cplus_gfx_clear((cplus_gfx_window *)0, 1, 2, 3);
    cplus_gfx_rect((cplus_gfx_window *)0, 0, 0, 4, 4, 1, 2, 3);
    cplus_gfx_text((cplus_gfx_window *)0, 0, 0, "hi");
    cplus_gfx_present((cplus_gfx_window *)0);
    CHECK(g_init_calls == 0 && g_draw_calls == 0 && g_null_draw == 0,
          "null_handle_no_sdl_calls");
    CHECK(cplus_gfx_sdl_refs == 0, "null_handle_no_ref");

    /* SDL_Init failure: nothing created, nothing to quit. */
    reset();
    w.window = (SDL_Window *)0;
    w.renderer = (SDL_Renderer *)0;
    g_init_fail = 1;
    CHECK(cplus_gfx_open(&w, "x", 8, 8) == 0, "init_failure_reported");
    CHECK(w.window == (SDL_Window *)0 && w.renderer == (SDL_Renderer *)0,
          "init_failure_handles_null");
    CHECK(g_init_calls == 1 && g_quit_calls == 0 && g_create_win == 0,
          "init_failure_no_window_no_quit");

    /* Window creation failure: our own init must be undone. */
    reset();
    g_window_fail = 1;
    CHECK(cplus_gfx_open(&w, "x", 8, 8) == 0, "window_failure_reported");
    CHECK(w.window == (SDL_Window *)0 && w.renderer == (SDL_Renderer *)0,
          "window_failure_handles_null");
    CHECK(g_quit_calls == 1 && g_initialized == 0 && g_create_ren == 0,
          "window_failure_balanced_init");

    /* Renderer failure: the window must be destroyed, not leaked. */
    reset();
    g_renderer_fail = 1;
    CHECK(cplus_gfx_open(&w, "x", 8, 8) == 0, "renderer_failure_reported");
    CHECK(g_create_win == 1 && g_destroy_win == 1 && g_create_ren == 1,
          "renderer_failure_window_destroyed");
    CHECK(w.window == (SDL_Window *)0 && w.renderer == (SDL_Renderer *)0,
          "renderer_failure_handles_null");
    CHECK(g_quit_calls == 1 && g_bad_destroy == 0 && cplus_gfx_sdl_refs == 0,
          "renderer_failure_balanced_refs");

    /* Successful open, all draw helpers, list edge cases, idempotent close. */
    reset();
    CHECK(cplus_gfx_open(&w, "x", 64, 48) == 1, "open_success");
    CHECK(w.window != (SDL_Window *)0 && w.renderer != (SDL_Renderer *)0 &&
              g_initialized == 1,
          "open_handles_and_init");
    cplus_gfx_clear(&w, 1, 2, 3);
    cplus_gfx_rect(&w, 0, 0, 4, 4, 1, 2, 3);
    cplus_gfx_text(&w, 0, 0, "hi");
    cplus_gfx_text(&w, 0, 0, (const char *)0);
    cplus_gfx_textbox(&w, 0, 0, 40, "label", "value");
    cplus_gfx_dropdown(&w, 0, 0, 40, "label", "sel");
    cplus_gfx_button(&w, 0, 0, 40, "button");
    items[0] = "one";
    items[1] = (const char *)0;
    cplus_gfx_list(&w, 0, 0, 40, 2, items, 0);
    cplus_gfx_list(&w, 0, 0, 40, 2, (const char *const *)0, 0);
    cplus_gfx_list(&w, 0, 0, 40, -3, items, 0);
    cplus_gfx_present(&w);
    CHECK(g_draw_calls > 0 && g_text_calls >= 5 && g_null_draw == 0 &&
              g_present_calls == 1,
          "draw_helpers_guarded");

    draws_before = g_draw_calls;
    cplus_gfx_close(&w);
    CHECK(g_destroy_ren == 1 && g_destroy_win == 1 && g_quit_calls == 1,
          "close_once_destroys_each_handle");
    CHECK(w.window == (SDL_Window *)0 && w.renderer == (SDL_Renderer *)0,
          "close_nulls_handles");
    cplus_gfx_close(&w);
    CHECK(g_destroy_ren == 1 && g_destroy_win == 1 && g_quit_calls == 1 &&
              g_bad_destroy == 0,
          "double_close_is_noop");
    cplus_gfx_clear(&w, 1, 2, 3);
    cplus_gfx_rect(&w, 0, 0, 4, 4, 1, 2, 3);
    cplus_gfx_text(&w, 0, 0, "hi");
    cplus_gfx_present(&w);
    CHECK(g_draw_calls == draws_before && g_null_draw == 0,
          "draw_after_close_no_sdl_calls");

    /* Reopening a closed handle contributes a fresh reference. */
    reset();
    CHECK(cplus_gfx_open(&w, "x", 8, 8) == 1, "reopen_after_close");
    cplus_gfx_close(&w);
    CHECK(g_quit_calls == 1 && cplus_gfx_sdl_refs == 0, "reopen_balanced");

    /* Two live windows: closing one must not call global SDL_Quit(). */
    reset();
    a.window = (SDL_Window *)0; a.renderer = (SDL_Renderer *)0;
    b.window = (SDL_Window *)0; b.renderer = (SDL_Renderer *)0;
    CHECK(cplus_gfx_open(&a, "a", 32, 32) == 1 &&
              cplus_gfx_open(&b, "b", 32, 32) == 1,
          "two_windows_open");
    CHECK(g_init_calls == 1 && g_quit_calls == 0 && cplus_gfx_sdl_refs == 2,
          "two_windows_one_init");
    quit_before = g_quit_calls;
    cplus_gfx_close(&a);
    CHECK(g_destroy_win == 1 && g_quit_calls == quit_before &&
              cplus_gfx_sdl_refs == 1,
          "first_close_keeps_sdl_alive");
    g_present_calls = 0;
    cplus_gfx_present(&b);
    CHECK(g_present_calls == 1, "second_window_still_renderable");
    cplus_gfx_close(&b);
    CHECK(g_quit_calls == quit_before + 1 && cplus_gfx_sdl_refs == 0 &&
              g_bad_destroy == 0,
          "last_close_quits_once");

    /* Opening an already-open handle is refused (no second window/ref). */
    reset();
    CHECK(cplus_gfx_open(&w, "x", 8, 8) == 1, "guard_open_first");
    i = g_create_win;
    CHECK(cplus_gfx_open(&w, "x", 8, 8) == 0 && g_create_win == i &&
              cplus_gfx_sdl_refs == 1,
          "guard_open_twice");
    cplus_gfx_close(&w);

    printf("gfx_sdl3 lifecycle: %d failure(s)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
