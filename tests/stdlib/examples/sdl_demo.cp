/* Demo: SDL3 window + renderer + input, headless on the dummy driver. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/input.hp"

int main(void) {
    error err = error();
    sdl core = sdl(SDL_INIT_VIDEO | SDL_INIT_EVENTS, &err);
    if (!core.valid()) {
        print("sdl failed", err.what());
        return 1;
    }

    window win = window("stdlib demo", 128, 96, SDL_WINDOW_HIDDEN, &err);
    if (!win.valid())
        return 2;

    renderer draw = renderer(win.native(), "software", &err);
    if (!draw.valid())
        return 3;

    draw.set_color(20, 20, 40, 255, &err);
    draw.clear(&err);
    draw.set_color(0, 200, 0, 255, &err);
    draw.fill_rect(16.0f, 16.0f, 64.0f, 48.0f, &err);
    draw.set_color(255, 255, 0, 255, &err);
    draw.line(0.0f, 0.0f, 127.0f, 95.0f, &err);
    draw.present(&err);

    surface shot = draw.read(&err);
    if (shot.valid()) {
        unsigned char inside[4];
        unsigned char corner[4];
        shot.pixel(32, 32, inside, &err);
        shot.pixel(120, 90, corner, &err);
        print("shot", shot.width(), shot.height(), inside[1] > corner[1]);
    }

    SDL_Event event;
    int pumped = poll_event(&event);
    const bool *keys = keyboard_state();
    float mouse_x = 0;
    float mouse_y = 0;
    unsigned buttons = mouse_state(&mouse_x, &mouse_y);
    print("input", pumped, key_pressed(keys, SDL_SCANCODE_ESCAPE),
          mouse_button_left(buttons), event_scancode(&event));
    return 0;
}
