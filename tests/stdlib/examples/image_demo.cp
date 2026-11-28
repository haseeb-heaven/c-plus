/* Demo: RAII images - build pixels, write PNG, reload, sample. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/image.hp"

int main(void) {
    error err = error();
    unsigned char pixels[8 * 8 * 3];
    int x;
    int y;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            size_t at = ((size_t)y * 8 + (size_t)x) * 3;
            pixels[at] = (unsigned char)(x * 30);
            pixels[at + 1] = (unsigned char)(y * 30);
            pixels[at + 2] = 200;
        }
    }

    image source = image_from_pixels(pixels, 8, 8, 3, &err);
    if (!source.valid()) {
        print("build failed", err.what());
        return 1;
    }
    print("source", source.width(), source.height(), source.channels(),
          source.size_bytes());

    if (source.write_png("/tmp/cplus_demo.png", &err) != STDLIB_OK)
        return 2;

    image loaded = image("/tmp/cplus_demo.png", 0, &err);
    if (!loaded.valid())
        return 3;
    const unsigned char *pixel = loaded.pixel(7, 7);
    print("loaded", loaded.width(), loaded.height(), pixel[0], pixel[1],
          pixel[2]);

    image broken = image("/nonexistent/demo.png", 0, &err);
    print("missing", broken.valid(), err.failed_now());
    return 0;
}
