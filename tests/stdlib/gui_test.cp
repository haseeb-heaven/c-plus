/* GUI wrapper test: real Nuklear frames on the SDL3 dummy driver.
 * Two frames: the first lays the widgets out, the second clicks the button at
 * the recorded position, then the frame is rendered and read back. */
#include <stdio.h>
#include <string.h>
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
    window win = window("gui", 240, 160, SDL_WINDOW_HIDDEN, &err);
    if (!win.valid())
        return 2;
    renderer draw = renderer(win.native(), "software", &err);
    if (!draw.valid())
        return 3;
    gui ui = gui(win.native(), draw.native(), &err);
    if (!ui.valid()) {
        print("gui-failed", err.what());
        return 4;
    }
    print("context", ui.valid(), ui.native() != NULL);

    /* Frame 1: layout, no input. */
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("demo", 8.0f, 8.0f, 200.0f, 120.0f, 0);
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    if (shown) {
        ui.layout_row_dynamic(24.0f, 1);
        ui.label("hello nuklear");
        ui.layout_row_dynamic(30.0f, 1);
        ui.widget_bounds(&x, &y, &w, &h);
        ui.button("OK");
    }
    ui.window_end();
    ui.render(1);
    print("frame1", shown, w > 0, h > 0, x, y, w, h);

    /* Frame 2: move the mouse onto the button, press and release. */
    int cx = x + w / 2;
    int cy = y + h / 2;
    ui.input_begin();
    ui.input_motion(cx, cy);
    ui.input_button(1);
    ui.input_end();
    shown = ui.window_begin("demo", 8.0f, 8.0f, 200.0f, 120.0f, 0);
    int pressed = 0;
    if (shown) {
        ui.layout_row_dynamic(24.0f, 1);
        ui.label("hello nuklear");
        ui.layout_row_dynamic(30.0f, 1);
        pressed = ui.button("OK");
    }
    ui.window_end();
    ui.render(1);
    print("click", cx, cy, pressed);

    /* Frame 3: release, then draw and read the frame back. */
    ui.input_begin();
    ui.input_button(0);
    ui.input_end();
    shown = ui.window_begin("demo", 8.0f, 8.0f, 200.0f, 120.0f, 0);
    int released = 0;
    if (shown) {
        ui.layout_row_dynamic(24.0f, 1);
        ui.label("hello nuklear");
        ui.layout_row_dynamic(30.0f, 1);
        released = ui.button("OK");
    }
    ui.window_end();
    print("release", released);

    draw.set_color(0, 0, 0, 255, &err);
    draw.clear(&err);
    ui.render(1);
    draw.present(&err);

    surface shot = draw.read(&err);
    int drew = 0;
    if (shot.valid()) {
        unsigned char pixel[4];
        int px;
        int py;
        for (py = 0; py < shot.height() && !drew; py += 4) {
            for (px = 0; px < shot.width() && !drew; px += 4) {
                shot.pixel(px, py, pixel, &err);
                if (pixel[0] + pixel[1] + pixel[2] > 30)
                    drew = 1;
            }
        }
        print("rendered", shot.valid(), drew, shot.width(), shot.height());
    }

    int styled = ui.style_text_color(200, 200, 200);
    print("style", styled);
    return 0;
}
