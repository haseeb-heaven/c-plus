/* Real windowed GUI app: SDL3 renderer + C+ GFX facade.
 * Draws textbox, dropdown, list and button widgets, saves the actual framebuffer. */
#include "cplus/cplus.h"
#include "stdlib/gfx_sdl3.hp"

int main(void) {
    cplus_gfx_window g;
    const char *items[4] = {"Arena bump", "Slot pool", "Scratch scope", "Stable ref"};
    SDL_Surface *shot;
    SDL_IOStream *io;
    SDL_Event ev;
    int selected = 1;
    int frames = 0;

    if (cplus_gfx_open(&g, "C+ GFX - Modern GUI", 640, 380) == 0) {
        print("GFX_APP FATAL open");
        return 1;
    }
    while (frames < 3) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) frames = 3;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) frames = 3;
        }
        cplus_gfx_clear(&g, 13, 17, 23);
        cplus_gfx_text(&g, 20, 12, "C+ STANDARD LIBRARY - SDL3 GUI");
        cplus_gfx_textbox(&g, 20, 48, 560, "Name:", "modern_allocators");
        cplus_gfx_dropdown(&g, 20, 92, 560, "Allocator:", items[selected]);
        cplus_gfx_list(&g, 20, 140, 560, 4, items, selected);
        cplus_gfx_button(&g, 20, 270, 180, "Run frame");
        cplus_gfx_button(&g, 220, 270, 180, "Drop scope");
        cplus_gfx_text(&g, 20, 330, "ABI preserved | arena-first | one-store release");
        cplus_gfx_present(&g);
        frames++;
    }
    shot = SDL_RenderReadPixels(g.renderer, (SDL_Rect *)0);
    if (shot != (SDL_Surface *)0) {
        io = SDL_IOFromFile("proof/games_gui_nuklear/screenshots/gui_widgets.bmp", "wb");
        if (io != (SDL_IOStream *)0) {
            if (SDL_SaveBMP_IO(shot, io, 1)) print("GFX_APP screenshot saved");
            else print("GFX_APP screenshot save_failed");
        }
        SDL_DestroySurface(shot);
    }
    cplus_gfx_close(&g);
    print("GFX_APP_OK widgets=textbox,dropdown,list,button");
    return 0;
}
