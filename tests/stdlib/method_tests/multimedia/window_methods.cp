/* Method-by-method coverage for stdlib/window.hp.
 * Build/run recipe: see RUN_NOTES.md in this directory.
 * SDL runs on the dummy video driver so this is fully headless on macOS.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"

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
    unsigned flags = (unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    int width = 0;
    int height = 0;

    /* sdl constructor: err must be clear on success. */
    sdl core = sdl(flags, &err);
    ck("window", "sdl::ctor", err.ok());

    ck("window", "sdl::valid", core.valid() == 1);
    ck("window", "sdl::subsystem_flags", core.subsystem_flags() == flags);
    ck("window", "sdl_backend_error", sdl_backend_error() != NULL);

    /* window constructor */
    window win = window("mt-window", 64, 48, (unsigned)SDL_WINDOW_HIDDEN, &err);
    ck("window", "window::ctor", err.ok());
    ck("window", "window::valid", win.valid() == 1);
    ck("window", "window::native", win.native() != NULL);

    int sized = win.size(&width, &height, &err);
    ck("window", "window::size", sized == STDLIB_OK && width == 64 && height == 48);

    int titled = win.set_title("mt-renamed", &err);
    ck("window", "window::set_title", titled == STDLIB_OK);

    win.drop();
    ck("window", "window::drop", win.valid() == 0 && win.native() == NULL);

    core.drop();
    ck("window", "sdl::drop", core.valid() == 0);

    return g_fail;
}
