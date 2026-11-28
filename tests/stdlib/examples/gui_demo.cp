/* Demo: immediate-mode GUI with Nuklear on SDL3 (headless dummy driver). */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"

int main(void) {
    error err = error();
    sdl core = sdl(SDL_INIT_VIDEO, &err);
    if (!core.valid())
        return 1;
    window win = window("gui demo", 320, 200, SDL_WINDOW_HIDDEN, &err);
    if (!win.valid())
        return 2;
    renderer draw = renderer(win.native(), "software", &err);
    if (!draw.valid())
        return 3;
    gui ui = gui(win.native(), draw.native(), &err);
    if (!ui.valid()) {
        print("gui failed", err.what());
        return 4;
    }

    int volume = 42;
    int clicks = 0;
    int frame;
    for (frame = 0; frame < 3; frame++) {
        ui.input_begin();
        if (frame == 1) {
            ui.input_motion(40, 60);
            ui.input_button(1);
        }
        if (frame == 2)
            ui.input_button(0);
        ui.input_end();
        if (ui.window_begin("controls", 10.0f, 10.0f, 280.0f, 160.0f, 0)) {
            ui.layout_row_dynamic(24.0f, 1);
            ui.label("volume");
            ui.layout_row_dynamic(30.0f, 1);
            ui.slider(&volume, 0, 100);
            ui.layout_row_dynamic(30.0f, 1);
            if (ui.button("increase"))
                clicks++;
        }
        ui.window_end();
        draw.set_color(24, 24, 24, 255, &err);
        draw.clear(&err);
        ui.render(1);
        draw.present(&err);
    }
    print("gui", ui.valid(), volume, clicks);
    return 0;
}
