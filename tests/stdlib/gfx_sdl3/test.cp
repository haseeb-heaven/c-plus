/* Issue #79: SDL window/renderer lifecycle, NULL safety and shared SDL
 * init/quit refcount.  Uses a recording SDL3 stub (no real window server). */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/gfx_sdl3.hp"

int stub_init_ok = 1;
int stub_init_calls = 0;
int stub_quit_calls = 0;
int stub_win_new = 0;
int stub_win_destroy = 0;
int stub_ren_new = 0;
int stub_ren_destroy = 0;
int stub_fail_renderer = 0;
int stub_draw_calls = 0;
int stub_present_calls = 0;

int main(void) {
    cplus_gfx_window g;
    cplus_gfx_window a;
    cplus_gfx_window b;
    const char *items[2];
    int fails = 0;

    /* NULL and already-closed handles must be harmless. */
    cplus_gfx_close((cplus_gfx_window *)0);
    g.window = (SDL_Window *)0;
    g.renderer = (SDL_Renderer *)0;
    g.sdl_ref = 0;
    cplus_gfx_close(&g);
    cplus_gfx_close(&g);
    if (stub_init_calls != 0 || stub_quit_calls != 0) fails++;

    /* Drawing helpers must refuse NULL / uninitialised windows. */
    cplus_gfx_clear((cplus_gfx_window *)0, 1, 2, 3);
    cplus_gfx_rect((cplus_gfx_window *)0, 0, 0, 4, 4, 1, 2, 3);
    cplus_gfx_text((cplus_gfx_window *)0, 0, 0, "x");
    cplus_gfx_present((cplus_gfx_window *)0);
    cplus_gfx_clear(&g, 1, 2, 3);
    cplus_gfx_list(&g, 0, 0, 10, 2, (const char *const *)0, 0);
    if (stub_draw_calls != 0 || stub_present_calls != 0) fails++;

    /* Renderer failure destroys the window and undoes the SDL retain. */
    stub_fail_renderer = 1;
    if (cplus_gfx_open(&g, "test", 64, 48) != 0) fails++;
    if (g.window != (SDL_Window *)0 || g.renderer != (SDL_Renderer *)0 ||
        g.sdl_ref != 0) fails++;
    if (stub_win_new != 1 || stub_win_destroy != 1 || stub_quit_calls != 1)
        fails++;

    /* Two windows share one SDL init; closing one must not call SDL_Quit. */
    stub_fail_renderer = 0;
    stub_quit_calls = 0;
    a.window = (SDL_Window *)0; a.renderer = (SDL_Renderer *)0; a.sdl_ref = 0;
    b.window = (SDL_Window *)0; b.renderer = (SDL_Renderer *)0; b.sdl_ref = 0;
    if (cplus_gfx_open(&a, "a", 64, 48) == 0) fails++;
    if (cplus_gfx_open(&b, "b", 64, 48) == 0) fails++;
    if (cplus_gfx_sdl3_refcount != 2) fails++;
    cplus_gfx_close(&a);
    if (stub_quit_calls != 0) fails++;
    cplus_gfx_close(&a);            /* idempotent */
    if (stub_quit_calls != 0) fails++;
    cplus_gfx_list(&b, 0, 0, 40, -1, items, 0); /* negative rows */
    cplus_gfx_list(&b, 0, 0, 40, 2, (const char *const *)0, 0); /* NULL items */
    items[0] = (const char *)0;
    items[1] = "row";
    cplus_gfx_list(&b, 0, 0, 40, 2, (const char *const *)items, 1);
    cplus_gfx_close(&b);
    if (stub_quit_calls != 1) fails++;

    printf("GFX_SDL3 %d\n", fails);
    return fails ? 1 : 0;
}
