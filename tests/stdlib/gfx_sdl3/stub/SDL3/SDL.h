/* Minimal SDL3 stand-in used only by tests/stdlib/gfx_sdl3 (issue #79).
 * It records lifecycle calls so open/close/refcount behaviour is deterministic
 * on headless machines; no real SDL3 headers or window server are needed. */
#ifndef CPLUS_TEST_STUB_SDL3_H
#define CPLUS_TEST_STUB_SDL3_H

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct { float x, y, w, h; } SDL_FRect;

#define SDL_INIT_VIDEO 1u

extern int stub_init_ok;
extern int stub_init_calls;
extern int stub_quit_calls;
extern int stub_win_new;
extern int stub_win_destroy;
extern int stub_ren_new;
extern int stub_ren_destroy;
extern int stub_fail_renderer;
extern int stub_draw_calls;
extern int stub_present_calls;

static inline int SDL_Init(unsigned int flags) {
    (void)flags; stub_init_calls++; return stub_init_ok;
}
static inline void SDL_Quit(void) { stub_quit_calls++; }
static inline SDL_Window *SDL_CreateWindow(const char *title, int w, int h,
                                           unsigned int flags) {
    (void)title; (void)w; (void)h; (void)flags;
    stub_win_new++;
    return (SDL_Window *)0x1000;
}
static inline SDL_Renderer *SDL_CreateRenderer(SDL_Window *window,
                                               const char *driver) {
    (void)window; (void)driver;
    if (stub_fail_renderer != 0) return (SDL_Renderer *)0;
    stub_ren_new++;
    return (SDL_Renderer *)0x2000;
}
static inline void SDL_DestroyWindow(SDL_Window *window) {
    (void)window; stub_win_destroy++;
}
static inline void SDL_DestroyRenderer(SDL_Renderer *renderer) {
    (void)renderer; stub_ren_destroy++;
}
static inline void SDL_SetRenderDrawColor(SDL_Renderer *r, unsigned char a,
                                          unsigned char b, unsigned char c,
                                          unsigned char d) {
    (void)r; (void)a; (void)b; (void)c; (void)d; stub_draw_calls++;
}
static inline void SDL_RenderClear(SDL_Renderer *r) { (void)r; }
static inline void SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *f) {
    (void)r; (void)f;
}
static inline void SDL_RenderDebugText(SDL_Renderer *r, float x, float y,
                                       const char *text) {
    (void)r; (void)x; (void)y; (void)text;
}
static inline void SDL_RenderPresent(SDL_Renderer *r) {
    (void)r; stub_present_calls++;
}

#endif
