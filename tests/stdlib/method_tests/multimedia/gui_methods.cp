/* Method-by-method coverage for stdlib/gui.hp (Nuklear + SDL3 backend).
 * Real frames are built and rendered on the SDL dummy video driver.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"

static int g_fail = 0;

static void ck(const char *header, const char *method, int ok) {
    if (ok) {
        print(header, method, "PASS");
    } else {
        print(header, method, "FAIL");
        g_fail++;
    }
}

int main(void) {
    error err = error();
    sdl core = sdl((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS), &err);
    if (!core.valid())
        return 90;
    window win = window("mt-gui", 240, 160, (unsigned)SDL_WINDOW_HIDDEN, &err);
    if (!win.valid())
        return 91;
    renderer draw = renderer(win.native(), "software", &err);
    if (!draw.valid())
        return 92;

    gui ui = gui(win.native(), draw.native(), &err);
    ck("gui", "gui::ctor", err.ok());
    ck("gui", "gui::valid", ui.valid() == 1);
    ck("gui", "gui::native", ui.native() != NULL);
    if (!ui.valid())
        return 93;

    struct nk_context *ctx = ui.native();

    ck("gui", "gui::handle_event", ui.handle_event(NULL) == 0);
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    (void)ui.handle_event(&ev);

    /* input_begin: a fresh frame can still be opened afterwards. */
    ui.input_begin();
    ui.input_end();
    int probe = ui.window_begin("probe", 4.0f, 4.0f, 100.0f, 40.0f, 0);
    ui.window_end();
    ck("gui", "gui::input_begin", probe == 1);

    /* input_motion updates Nuklear's cursor position. */
    ui.input_motion(60, 40);
    ck("gui", "gui::input_motion",
       ctx->input.mouse.pos.x == 60.0f && ctx->input.mouse.pos.y == 40.0f);

    /* input_button drives the left-button state at the last motion point. */
    ui.input_begin();
    ui.input_button(1);
    ui.input_end();
    ck("gui", "gui::input_button",
       ctx->input.mouse.buttons[NK_BUTTON_LEFT].down == 1);

    /* input_end releases any mouse grab accumulated during the frame. */
    ctx->input.mouse.grab = 1;
    ctx->input.mouse.ungrab = 1;
    ctx->input.mouse.grabbed = 1;
    ui.input_end();
    ck("gui", "gui::input_end",
       ctx->input.mouse.grab == 0 && ctx->input.mouse.ungrab == 0 &&
           ctx->input.mouse.grabbed == 0);

    /* Release the button so the widget frame below sees no click. */
    ui.input_begin();
    ui.input_motion(0, 0);
    ui.input_button(0);
    ui.input_end();

    /* One full widget frame. */
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("demo", 8.0f, 8.0f, 200.0f, 120.0f, 0);
    ck("gui", "gui::window_begin", shown == 1);

    int dyn_w = 0;
    int dyn_h = 0;
    if (shown) {
        ui.layout_row_dynamic(30.0f, 1);
        ui.label("dynamic");
        int got = ui.widget_bounds(NULL, NULL, &dyn_w, &dyn_h);
        ck("gui", "gui::layout_row_dynamic", got == 1 && dyn_h > 0);

        int lbl_w = 0;
        int lbl_h = 0;
        ui.label("label");
        int got2 = ui.widget_bounds(NULL, NULL, &lbl_w, &lbl_h);
        ck("gui", "gui::label", got2 == 1 && lbl_w > 0);

        ui.layout_row_static(24.0f, 80, 1);
        ui.label("static");
        int stat_w = 0;
        int stat_h = 0;
        int got3 = ui.widget_bounds(NULL, NULL, &stat_w, &stat_h);
        ck("gui", "gui::layout_row_static", got3 == 1 && stat_w > 0 && stat_w <= 80);

        int pressed = ui.button("OK");
        ck("gui", "gui::button", pressed == 0 || pressed == 1);

        int value = 5;
        int sv = ui.slider(&value, 0, 10);
        ck("gui", "gui::slider",
           sv >= 0 && sv <= 10 && value >= 0 && value <= 10);

        int bx = 0;
        int by = 0;
        int bw = 0;
        int bh = 0;
        int bounds = ui.widget_bounds(&bx, &by, &bw, &bh);
        ck("gui", "gui::widget_bounds", bounds == 1 && bw > 0 && bh > 0);
    }

    ui.window_end();
    int after_end = ui.widget_bounds(NULL, NULL, NULL, NULL);
    ck("gui", "gui::window_end", after_end == 0);

    /* render: clear to black, draw the frame, read pixels back. */
    draw.set_color(0, 0, 0, 255, &err);
    draw.clear(&err);
    ui.render(1);
    draw.present(&err);
    surface shot = draw.read(&err);
    int drew = 0;
    if (shot.valid()) {
        unsigned char p[4];
        int py;
        int px;
        for (py = 0; py < shot.height() && !drew; py += 4) {
            for (px = 0; px < shot.width() && !drew; px += 4) {
                shot.pixel(px, py, p, &err);
                if (p[0] + p[1] + p[2] > 30)
                    drew = 1;
            }
        }
    }
    ck("gui", "gui::render", drew == 1);

    ck("gui", "gui::style_text_color", ui.style_text_color(200, 200, 200) == 1);

    ui.drop();
    ck("gui", "gui::drop", ui.valid() == 0);

    shot.drop();
    draw.drop();
    win.drop();
    core.drop();
    return g_fail;
}
