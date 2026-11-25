/* Minimal SDL3 API surface stub used ONLY for the Windows PE compile-only
 * verification of examples/gui_app/gui_app.cp.  MinGW-w64 on this host has no
 * SDL3 port; these declarations let the C+ driver's lowered C be type-checked
 * and linked into a genuine PE32+ executable.  No Windows runtime behaviour is
 * claimed -- this is compile-and-verify only. */
#ifndef MODCROSS_PE_SDL3_STUB_H
#define MODCROSS_PE_SDL3_STUB_H

#include <stdint.h>

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;

typedef struct SDL_FRect {
    float x;
    float y;
    float w;
    float h;
} SDL_FRect;

#define SDL_INIT_VIDEO      0x00000020u
#define SDL_WINDOW_HIDDEN   0x00000020u
#define SDL_VIDEODRIVER     "SDL_VIDEODRIVER"

#ifdef __cplusplus
extern "C" {
#endif

int SDL_Init(uint32_t flags);
void SDL_Quit(void);
SDL_Window *SDL_CreateWindow(const char *title, int w, int h, uint32_t flags);
void SDL_DestroyWindow(SDL_Window *window);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
int SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t red, uint8_t green,
                           uint8_t blue, uint8_t alpha);
int SDL_RenderClear(SDL_Renderer *r);
int SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rect);
void SDL_RenderPresent(SDL_Renderer *r);
const char *SDL_GetCurrentVideoDriver(void);

#ifdef __cplusplus
}
#endif

#endif /* MODCROSS_PE_SDL3_STUB_H */
