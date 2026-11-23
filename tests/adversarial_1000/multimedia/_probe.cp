/* Temporary behavior probe (removed before handoff). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"
#include "stdlib/image.hp"
#include "stdlib/audio.hp"
#include "stdlib/video.hp"

int main(void) {
    error err = error();

    sdl core = sdl((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS), &err);
    printf("sdl valid=%d err=%d\n", core.valid(), err.failed_now());

    window tiny = window("p", 0, 0, (unsigned)SDL_WINDOW_HIDDEN, &err);
    printf("win0 valid=%d err=%d\n", tiny.valid(), err.failed_now());
    tiny.drop();

    window huge = window("p", 100000, 100000, (unsigned)SDL_WINDOW_HIDDEN, &err);
    printf("winhuge valid=%d err=%d\n", huge.valid(), err.failed_now());
    huge.drop();

    window w = window("p", 64, 48, (unsigned)SDL_WINDOW_HIDDEN, &err);
    renderer r = renderer(w.native(), "software", &err);
    printf("win valid=%d rvalid=%d err=%d\n", w.valid(), r.valid(), err.failed_now());

    gui ui = gui(w.native(), r.native(), &err);
    printf("gui valid=%d err=%d\n", ui.valid(), err.failed_now());
    if (ui.valid()) {
        ui.input_begin();
        int shown = ui.window_begin("x", NAN, NAN, 200.0f, 100.0f, 0);
        printf("gui nan begin=%d\n", shown);
        ui.window_end();
        ui.input_end();
        ui.input_begin();
        shown = ui.window_begin("y", 1.0e30f, -1.0e30f, 1.0e30f, 1.0e30f, 0);
        printf("gui huge begin=%d\n", shown);
        ui.window_end();
        ui.input_end();
        ui.drop();
    }

    surface s = r.read(&err);
    printf("surface valid=%d %dx%d\n", s.valid(), s.width(), s.height());
    unsigned char px[4];
    px[0] = 7; px[1] = 7; px[2] = 7; px[3] = 7;
    int got = s.pixel(-1, 0, px, &err);
    printf("pixel oob ret=%d sentinel=%d\n", got,
           px[0] == 7 && px[1] == 7 && px[2] == 7 && px[3] == 7);
    got = s.pixel(0, 0, px, &err);
    printf("pixel ok ret=%d px=%d\n", got, px[0]);
    s.drop();
    r.drop();
    w.drop();
    core.drop();

    unsigned char png[128];
    memset(png, 0, sizeof(png));
    image bad = image_from_memory(png, 8, 3, &err);
    printf("img truncated valid=%d err=%d\n", bad.valid(), err.failed_now());
    bad.drop();

    unsigned char wav[512];
    memset(wav, 0, sizeof(wav));
    audio_decoder ad = decoder_from_memory(wav, 16, &err);
    printf("audio truncated valid=%d err=%d\n", ad.valid(), err.failed_now());
    ad.drop();

    FILE *fp = fopen("results/_probe_bad.bin", "wb");
    if (fp != NULL) {
        (void)fwrite(png, 1, 8, fp);
        fclose(fp);
    }
    media m = media("results/_probe_bad.bin", &err);
    printf("media truncated valid=%d err=%d\n", m.valid(), err.failed_now());
    m.drop();

    printf("PROBE done\n");
    return 0;
}
