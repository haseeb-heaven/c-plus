/* Method-by-method coverage for stdlib/graphics.hp.
 * SDL3 software renderer on the dummy video driver (no display needed).
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"

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
    window win = window("mt-gfx", 64, 48, (unsigned)SDL_WINDOW_HIDDEN, &err);
    if (!win.valid())
        return 91;

    renderer draw = renderer(win.native(), "software", &err);
    ck("graphics", "renderer::ctor", err.ok());
    ck("graphics", "renderer::valid", draw.valid() == 1);
    ck("graphics", "renderer::native", draw.native() != NULL);

    ck("graphics", "renderer::set_color",
       draw.set_color(0, 0, 0, 255, &err) == STDLIB_OK);
    ck("graphics", "renderer::clear", draw.clear(&err) == STDLIB_OK);
    draw.set_color(255, 0, 0, 255, &err);
    ck("graphics", "renderer::fill_rect",
       draw.fill_rect(8.0f, 8.0f, 16.0f, 16.0f, &err) == STDLIB_OK);
    ck("graphics", "renderer::line",
       draw.line(0.0f, 0.0f, 63.0f, 47.0f, &err) == STDLIB_OK);
    ck("graphics", "renderer::set_vsync", draw.set_vsync(0, &err) == STDLIB_OK);
    ck("graphics", "renderer::present", draw.present(&err) == STDLIB_OK);

    surface shot = draw.read(&err);
    ck("graphics", "renderer::read", err.ok() && shot.valid());

    surface empty = surface(NULL);
    ck("graphics", "surface::ctor", empty.valid() == 0);

    ck("graphics", "surface::valid", shot.valid() == 1);
    ck("graphics", "surface::width", shot.width() == 64);
    ck("graphics", "surface::height", shot.height() == 48);

    unsigned char inside[4];
    int got = shot.pixel(12, 12, inside, &err);
    /* SDL3's software target is BGRA in memory: red lands at byte 2 and the
     * alpha byte may be 0 (XRGB target), so only the colour bytes are checked. */
    ck("graphics", "surface::pixel",
       got == STDLIB_OK && inside[2] > 200 && inside[0] < 50 && inside[1] < 50);

    shot.drop();
    ck("graphics", "surface::drop", shot.valid() == 0 && shot.width() == 0);

    draw.drop();
    ck("graphics", "renderer::drop", draw.valid() == 0);

    win.drop();
    core.drop();
    return g_fail;
}
