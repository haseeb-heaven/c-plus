/* olive.c example: built-in bitmap font text (no assets). */
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
    olivec_fill(oc, 0xFF0A1428u);
    olivec_text(oc, "OLIVE.C + C+", 24, 32, olivec_default_font, 3, 0xFF40E0FFu);
    olivec_text(oc, "2D SOFTWARE RENDERER", 24, 72, olivec_default_font, 2, 0xFFFFC040u);
    olivec_text(oc, "NO ASSETS - ALL PIXELS", 24, 108, olivec_default_font, 2, 0xFF80FF80u);
    for (int i = 0; i < 8; ++i)
        olivec_rect(oc, 24 + i * 34, 160, 24, 16, (uint32_t) (0xFF2020A0u + (unsigned) i * 0x00101000u));
    for (size_t i = 0; i < PPM_HEADER_LEN; ++i) ppm[i] = (unsigned char) PPM_HEADER[i];
    for (size_t i = 0; i < (size_t) WIDTH * HEIGHT; ++i) {
        size_t o = PPM_HEADER_LEN + 3u * i;
        ppm[o] = (unsigned char) pixels[i];
        ppm[o + 1] = (unsigned char) (pixels[i] >> 8);
        ppm[o + 2] = (unsigned char) (pixels[i] >> 16);
    }
    FILE *f = fopen("out/text.ppm", "wb");
    if (!f) return 1;
    int ok = fwrite(ppm, 1, PPM_BYTES, f) == PPM_BYTES && fclose(f) == 0;
    if (!ok) return 1;
    printf("OLIVE_TEXT bytes=%u fnv1a64=%016llx\n", (unsigned) PPM_BYTES,
           (unsigned long long) fnv1a64(ppm, PPM_BYTES));
    return 0;
}
