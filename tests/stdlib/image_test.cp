/* Image wrapper test: build -> PNG/BMP -> decode -> verify pixels, + failures. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/image.hp"

int main(void) {
    error err = error();
    unsigned char src[4 * 3 * 3];
    int x;
    int y;
    for (y = 0; y < 3; y++) {
        for (x = 0; x < 4; x++) {
            size_t at = ((size_t)y * 4 + (size_t)x) * 3;
            src[at] = (unsigned char)(x * 40);
            src[at + 1] = (unsigned char)(y * 60);
            src[at + 2] = 128;
        }
    }

    image built = image_from_pixels(src, 4, 3, 3, &err);
    if (!built.valid())
        return 1;
    print("built", built.width(), built.height(), built.channels(),
          built.size_bytes());

    if (built.write_png("/tmp/cplus_image.png", &err) != STDLIB_OK)
        return 2;
    if (built.write_bmp("/tmp/cplus_image.bmp", &err) != STDLIB_OK)
        return 3;

    image png = image("/tmp/cplus_image.png", 0, &err);
    if (!png.valid())
        return 4;
    const unsigned char *pixel = png.pixel(3, 2);
    print("png", png.width(), png.height(), png.channels(), pixel[0], pixel[1],
          pixel[2]);
    print("oob", png.pixel(9, 9) == NULL);

    image bmp = image("/tmp/cplus_image.bmp", 3, &err);
    print("bmp", bmp.valid(), bmp.channels());

    image missing = image("/nonexistent/cplus/image.png", 0, &err);
    print("missing", missing.valid(), err.failed_now());

    unsigned char junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    image broken = image_from_memory(junk, sizeof(junk), 0, &err);
    print("junk", broken.valid(), err.failed_now());

    if (missing.write_png("/tmp/should_not_exist.png", &err) != STDLIB_FAIL)
        return 5;
    print("write-fail", err.failed_now());
    return 0;
}
