/* Real-world C+ GUI app: interactive memory lab on SDL3 + scoped memory.
 * Widgets: editable textbox, dropdown, list, buttons. State lives in fixed
 * slots; transient strings come from the VM data stack (one-store scope). */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/gfx_sdl3.hp"
#include "stdlib/scoped_mem.hp"

typedef struct {
    char name[64];
    int name_len;
    int selected;
    int dropdown_open;
    int frames;
    int clicks;
} app_state;

static const char *const ALLOCATORS[4] = {
    "VM data stack", "Dynamic arena", "Slot pool", "Checked heap (fallback)"
};

static void hit(int mx, int my, int x, int y, int w, int h, int *inside) {
    *inside = (mx >= x && mx < x + w && my >= y && my < y + h) ? 1 : 0;
}

int main(int argc, char **argv) {
    cplus_gfx_window g;
    app_state st;
    error e = error();
    const int demo = (argc > 1);
    int running = 1;
    int frame = 0;
    int mx = 0, my = 0, inside = 0;
    SDL_Event ev;

    stdlib_set_error_callback((stdlib_error_callback)0);
    memset(&st, 0, sizeof(st));
    strcpy(st.name, "cplus_memory_lab");
    st.name_len = (int)strlen(st.name);
    if (cplus_gfx_open(&g, "C+ Memory Lab - SDL3", 720, 420) == 0) return 1;
    SDL_StartTextInput(g.window);

    while (running && frame < (demo ? 4 : 100000)) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running = 0;
            else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) running = 0;
            else if (ev.type == SDL_EVENT_TEXT_INPUT && st.name_len < 60) {
                size_t k;
                for (k = 0; ev.text.text[k] && st.name_len < 60; ++k)
                    st.name[st.name_len++] = ev.text.text[k];
                st.name[st.name_len] = 0;
            }
            else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                mx = (int)ev.button.x; my = (int)ev.button.y;
                hit(mx, my, 20, 270, 180, 30, &inside);
                if (inside) st.clicks++;
                hit(mx, my, 20, 92, 560, 28, &inside);
                if (inside) st.dropdown_open = st.dropdown_open ? 0 : 1;
                hit(mx, my, 20, 140 + st.selected * 28, 560, 26, &inside);
                if (inside && st.selected < 3) st.selected++;
            }
        }
        if (demo && frame == 2) { st.selected++; st.clicks++; }

        cplus_gfx_clear(&g, 13, 17, 23);
        cplus_gfx_text(&g, 20, 12, "C+ MEMORY LAB - Goose-style scoped allocation");
        cplus_gfx_textbox(&g, 20, 48, 560, "Name:", st.name);
        cplus_gfx_dropdown(&g, 20, 92, 560, "Allocator:", ALLOCATORS[st.selected]);
        if (st.dropdown_open)
            cplus_gfx_list(&g, 20, 140, 560, 4, ALLOCATORS, st.selected);
        else
            cplus_gfx_list(&g, 20, 140, 560, 4, ALLOCATORS, st.selected);
        cplus_gfx_button(&g, 20, 270, 180, "Run frame");
        cplus_gfx_button(&g, 220, 270, 180, "Drop scope");

        /* Transient per-frame data on the VM data stack: no heap, one-store
         * release at scope exit. */
        {
            stdlib_scoped_scope scope = stdlib_scoped_scope(stdlib_scoped_stack_current(&e));
            char line[128];
            snprintf(line, sizeof(line), "frames=%d clicks=%d slot=%d",
                     st.frames, st.clicks, st.selected);
            cplus_gfx_text(&g, 20, 320, line);
            scope.drop();
        }
        cplus_gfx_present(&g);
        if (frame == 0) {
            SDL_Surface *shot = SDL_RenderReadPixels(g.renderer, (SDL_Rect *)0);
            if (shot != (SDL_Surface *)0) {
                SDL_IOStream *io = SDL_IOFromFile(
                    "proof/games_gui_nuklear/screenshots/memory_lab.bmp", "wb");
                if (io != (SDL_IOStream *)0) { (void)SDL_SaveBMP_IO(shot, io, 1); }
                SDL_DestroySurface(shot);
            }
        }
        st.frames++;
        frame++;
    }
    SDL_StopTextInput(g.window);
    cplus_gfx_close(&g);
    printf("MEMORY_LAB frames=%d clicks=%d selected=%d\n", st.frames, st.clicks, st.selected);
    return 0;
}
