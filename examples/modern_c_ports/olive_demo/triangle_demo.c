/* Deterministic still-frame adaptation of tsoding/olive.c demos/triangle.c.
 * Upstream: https://github.com/tsoding/olive.c (MIT -- see LICENSE).
 * Renders one frame into a uint32_t framebuffer, writes a binary PPM, and
 * prints the byte count plus FNV-1a64 of the exact bytes written. */
#define OLIVEC_IMPLEMENTATION
#include "olive.c"

#include <stdio.h>

#define WIDTH 320
#define HEIGHT 240
#define BACKGROUND_COLOR 0xFF181818u
#define CIRCLE_COLOR 0x99AA2020u

#define PPM_HEADER "P6\n320 240\n255\n"
#define PPM_HEADER_LEN (sizeof(PPM_HEADER) - 1u)
#define PPM_BYTES (PPM_HEADER_LEN + 3u * WIDTH * HEIGHT)

static uint64_t fnv1a64(const unsigned char *data, size_t size)
{
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t) data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int main(void)
{
    static uint32_t pixels[WIDTH * HEIGHT];
    static unsigned char ppm[PPM_BYTES];
    Olivec_Canvas oc = olivec_canvas(pixels, WIDTH, HEIGHT, WIDTH);

    /* Same shapes as upstream demos/triangle.c, with a fixed pose so that
     * every run (and both compilers) produce identical bytes. */
    olivec_fill(oc, BACKGROUND_COLOR);
    olivec_triangle3c(oc, WIDTH / 2, HEIGHT / 8, WIDTH / 8, HEIGHT / 2,
                      WIDTH * 7 / 8, HEIGHT * 7 / 8,
                      0xFF2020FFu, 0xFF20FF20u, 0xFFFF2020u);
    olivec_circle(oc, WIDTH / 2, HEIGHT / 2, 36, CIRCLE_COLOR);

    for (size_t i = 0; i < PPM_HEADER_LEN; ++i) {
        ppm[i] = (unsigned char) PPM_HEADER[i];
    }
    for (size_t i = 0; i < (size_t) WIDTH * HEIGHT; ++i) {
        size_t o = PPM_HEADER_LEN + 3u * i;
        ppm[o + 0u] = (unsigned char) (pixels[i] & 0xFFu);
        ppm[o + 1u] = (unsigned char) ((pixels[i] >> 8) & 0xFFu);
        ppm[o + 2u] = (unsigned char) ((pixels[i] >> 16) & 0xFFu);
    }

    FILE *f = fopen("out/frame.ppm", "wb");
    if (f == NULL) {
        fprintf(stderr, "olive_demo: cannot open out/frame.ppm\n");
        return 1;
    }
    int ok = fwrite(ppm, 1u, PPM_BYTES, f) == PPM_BYTES;
    if (fclose(f) != 0) ok = 0;
    if (!ok) {
        fprintf(stderr, "olive_demo: failed to write out/frame.ppm\n");
        return 1;
    }

    printf("OLIVE_DEMO bytes=%u fnv1a64=%016llx ppm=out/frame.ppm\n",
           (unsigned) PPM_BYTES,
           (unsigned long long) fnv1a64(ppm, PPM_BYTES));
    return 0;
}
