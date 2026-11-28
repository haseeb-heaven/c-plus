/* Linkable stub implementations for the SDL3 PE compile-only check.
 * These return failure/zeroed values so the GUI demo's refusal paths are
 * exercised at link time; no Windows execution is implied. */
#include "SDL3/SDL.h"

int SDL_Init(uint32_t flags) { (void)flags; return 0; }
void SDL_Quit(void) {}
SDL_Window *SDL_CreateWindow(const char *title, int w, int h, uint32_t flags) {
    (void)title; (void)w; (void)h; (void)flags;
    return (SDL_Window *)0;
}
void SDL_DestroyWindow(SDL_Window *window) { (void)window; }
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name) {
    (void)window; (void)name;
    return (SDL_Renderer *)0;
}
void SDL_DestroyRenderer(SDL_Renderer *renderer) { (void)renderer; }
int SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t a, uint8_t b, uint8_t c,
                           uint8_t d) {
    (void)r; (void)a; (void)b; (void)c; (void)d; return 0;
}
int SDL_RenderClear(SDL_Renderer *r) { (void)r; return 0; }
int SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rect) {
    (void)r; (void)rect; return 0;
}
void SDL_RenderPresent(SDL_Renderer *r) { (void)r; }
const char *SDL_GetCurrentVideoDriver(void) { return "pe-stub"; }
