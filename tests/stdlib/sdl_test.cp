/* SDL3 window/renderer/input test on the dummy video driver (no display). */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/input.hp"

int main(void) {
    error err = error();

    sdl core = sdl(SDL_INIT_VIDEO | SDL_INIT_EVENTS, &err);
    if (!core.valid()) {
        print("sdl-failed", err.what());
        return 1;
    }

    window win = window("cplus", 64, 48, SDL_WINDOW_HIDDEN, &err);
    if (!win.valid())
        return 2;

    int width = 0;
    int height = 0;
    print("sdl-flags", core.subsystem_flags() != 0);
    int sized = win.size(&width, &height, &err);
    int titled = win.set_title("renamed", &err);
    print("window", sized, width, height, titled, win.native() != NULL);

    renderer draw = renderer(win.native(), "software", &err);
    if (!draw.valid())
        return 3;

    draw.set_color(0, 0, 0, 255, &err);
    draw.clear(&err);
    draw.set_color(255, 0, 0, 255, &err);
    draw.fill_rect(8.0f, 8.0f, 16.0f, 16.0f, &err);
    draw.line(0.0f, 0.0f, 63.0f, 47.0f, &err);
    int vsync = draw.set_vsync(0, &err);
    draw.present(&err);
    print("draw", vsync);

    surface shot = draw.read(&err);
    if (!shot.valid())
        return 4;

    unsigned char inside[4];
    unsigned char outside[4];
    if (shot.pixel(12, 12, inside, &err) != STDLIB_OK)
        return 5;
    if (shot.pixel(0, 40, outside, &err) != STDLIB_OK)
        return 6;
    print("surface", shot.width(), shot.height(),
          memcmp(inside, outside, 4) != 0,
          outside[0] + outside[1] + outside[2] == 0);

    int bounds = shot.pixel(999, 999, inside, &err) != STDLIB_OK;

    SDL_Event event;
    memset(&event, 0, sizeof(event));
    int pumped = poll_event(&event);
    const bool *keys = keyboard_state();
    float mouse_x = 0;
    float mouse_y = 0;
    unsigned buttons = mouse_state(&mouse_x, &mouse_y);
    print("input", pumped, event_is_quit(&event), event_is_key_down(&event),
          event_is_key_up(&event), event_scancode(&event) == SDL_SCANCODE_UNKNOWN,
          event_text(&event) == '\0', key_pressed(keys, SDL_SCANCODE_A),
          mouse_button_left(buttons), bounds, sdl_backend_error() != NULL);

    renderer invalid = renderer(NULL, "software", &err);
    print("invalid-renderer", invalid.valid(), err.failed_now(), win.valid());
    return 0;
}
