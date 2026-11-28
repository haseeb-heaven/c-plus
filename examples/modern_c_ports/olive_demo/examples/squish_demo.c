/* olive.c example: nested gradient triangles and circles (squish-like). */
#define OLIVEC_IMPLEMENTATION
#include "olive.c"
#include <stdio.h>

#define WIDTH 320
#define HEIGHT 240
#define PPM_HEADER "P6\n320 240\n255\n"
#define PPM_HEADER_LEN (sizeof(PPM_HEADER) - 1u)
#define PPM_BYTES (PPM_HEADER_LEN + 3u * WIDTH * HEIGHT)

static uint64_t fnv1a64(const unsigned char *d, size_t n)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; ++i) { h ^= (uint64_t) d[i]; h *= 1099511628211ULL; }
    return h;
}
int main(void)
{
    static uint32_t pixels[WIDTH * HEIGHT];
    static unsigned char ppm[PPM_BYTES];
    Olivec_Canvas oc = olivec_canvas(pixels, WIDTH, HEIGHT, WIDTH);
    olivec_fill(oc, 0xFF181810u);
    for (int i = 0; i < 6; ++i) {
        int inset = i * 18;
        uint32_t c1 = (uint32_t) (0xFF2020FFu + i * 0x00101000u);
        uint32_t c2 = (uint32_t) (0xFF20FF20u - i * 0x00001000u);
        uint32_t c3 = (uint32_t) (0xFFFF2020u + i * 0x00001000u);
        olivec_triangle3c(oc, WIDTH / 2, 16 + inset,
                          32 + inset, HEIGHT - 16 - inset,
                          WIDTH - 32 - inset, HEIGHT - 16 - inset,
                          c1, c2, c3);
        olivec_circle(oc, WIDTH / 2, HEIGHT / 2, 60 - i * 8,
                      (uint32_t) (0x66204080u + i * 0x00202000u));
    }
    for (size_t i = 0; i < PPM_HEADER_LEN; ++i) ppm[i] = (unsigned char) PPM_HEADER[i];
    for (size_t i = 0; i < (size_t) WIDTH * HEIGHT; ++i) {
        size_t o = PPM_HEADER_LEN + 3u * i;
        ppm[o] = (unsigned char) pixels[i];
        ppm[o + 1] = (unsigned char) (pixels[i] >> 8);
        ppm[o + 2] = (unsigned char) (pixels[i] >> 16);
    }
    FILE *f = fopen("out/squish.ppm", "wb");
    if (!f) return 1;
    int ok = fwrite(ppm, 1, PPM_BYTES, f) == PPM_BYTES && fclose(f) == 0;
    if (!ok) return 1;
    printf("OLIVE_SQUISH bytes=%u fnv1a64=%016llx\n", (unsigned) PPM_BYTES,
           (unsigned long long) fnv1a64(ppm, PPM_BYTES));
    return 0;
}
