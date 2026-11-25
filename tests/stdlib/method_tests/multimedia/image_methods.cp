/* Method-by-method coverage for stdlib/image.hp (stb image codec).
 * Fully headless: builds pixels, writes PNG/BMP, decodes them back.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/image.hp"

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
    unsigned char src[36];
    int x;
    int y;

    for (y = 0; y < 3; y++) {
        for (x = 0; x < 4; x++) {
            int at = (y * 4 + x) * 3;
            src[at] = (unsigned char)(x * 40);
            src[at + 1] = (unsigned char)(y * 60);
            src[at + 2] = 128;
        }
    }

    image built = image_from_pixels(src, 4, 3, 3, &err);
    int made = err.ok() && built.valid();
    ck("image", "image_from_pixels", made);
    if (!made)
        return 1;

    int wrote_png = built.write_png("/tmp/cplus_mt_image.png", &err) == STDLIB_OK;
    ck("image", "image::write_png", wrote_png);

    int wrote_bmp = built.write_bmp("/tmp/cplus_mt_image.bmp", &err) == STDLIB_OK;
    ck("image", "image::write_bmp", wrote_bmp);

    image pic = image("/tmp/cplus_mt_image.png", 0, &err);
    ck("image", "image::ctor", err.ok());
    ck("image", "image::valid", pic.valid() == 1);
    ck("image", "image::width", pic.width() == 4);
    ck("image", "image::height", pic.height() == 3);
    ck("image", "image::channels", pic.channels() == 3);
    ck("image", "image::size_bytes", pic.size_bytes() == 36);

    const unsigned char *px = pic.pixel(3, 2);
    ck("image", "image::pixel",
       px != NULL && px[0] == 120 && px[1] == 120 && px[2] == 128);

    unsigned char mem[4096];
    size_t n = 0;
    FILE *fp = fopen("/tmp/cplus_mt_image.png", "rb");
    if (fp != NULL) {
        n = fread(mem, 1, sizeof(mem), fp);
        fclose(fp);
    }
    image decoded = image_from_memory(mem, (int)n, 3, &err);
    ck("image", "image_from_memory", err.ok() && decoded.valid());

    pic.drop();
    ck("image", "image::drop", pic.valid() == 0 && pic.width() == 0);

    decoded.drop();
    built.drop();
    return g_fail;
}
