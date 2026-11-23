/* Temporary behavior probe 3 (removed before handoff). */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/image.hp"
#include "stdlib/audio.hp"

static void put32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

int main(void) {
    error e = error();

    /* Low-bpp surface -> surface::pixel must refuse (unsupported format). */
    SDL_Surface *s8 = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_INDEX8);
    printf("s8 ptr=%p\n", (void *)s8);
    if (s8 != NULL) {
        surface sh = surface(s8);
        unsigned char out[4];
        out[0] = 9; out[1] = 9; out[2] = 9; out[3] = 9;
        int rc = sh.pixel(1, 1, out, &e);
        printf("s8 pixel rc=%d err=%d sentinel=%d what=%s\n", rc,
               e.failed_now(),
               out[0] == 9 && out[1] == 9 && out[2] == 9 && out[3] == 9,
               e.what());
        sh.drop();
    }

    /* Crafted PNG with huge IHDR dims but truncated body. */
    unsigned char png[64];
    memset(png, 0, sizeof(png));
    png[0] = 137; png[1] = 80; png[2] = 78; png[3] = 71;
    png[4] = 13; png[5] = 10; png[6] = 26; png[7] = 10;
    put32(png + 8, 13);
    memcpy(png + 12, "IHDR", 4);
    put32(png + 16, 0xffffffffu);
    put32(png + 20, 0xffffffffu);
    png[24] = 8; png[25] = 2; png[26] = 0; png[27] = 0; png[28] = 0;
    image bi = image_from_memory(png, 29, 3, &e);
    printf("huge png valid=%d err=%d what=%s\n", bi.valid(), e.failed_now(), e.what());
    bi.drop();

    /* audio device with zero sample rate / zero channels. */
    audio_context ctx = audio_context(1, &e);
    printf("ctx valid=%d\n", ctx.valid());
    audio_device d0 = audio_device(ctx.native(), 0, 0, 2, NULL, NULL, &e);
    printf("dev rate0 valid=%d err=%d what=%s\n", d0.valid(), e.failed_now(), e.what());
    d0.drop();
    audio_device dc = audio_device(ctx.native(), 0, 48000, 0, NULL, NULL, &e);
    printf("dev ch0 valid=%d err=%d what=%s\n", dc.valid(), e.failed_now(), e.what());
    dc.drop();
    ctx.drop();

    printf("PROBE3 done\n");
    return 0;
}
