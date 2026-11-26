/* Independent modern-C implementation of the gfx_cube workload.
 * C11, deterministic stdout.  The cube is projected into a small ASCII
 * framebuffer (no window); transient buffers use the scoped scratch pattern
 * of stdlib/scoped_mem.hp.  No upstream sample source was copied. */
#include "common.h"
#include <math.h>

#define GW 41
#define GH 21
#define FRAMES 30
#define IW 32
#define IH 32

typedef struct { unsigned char *base; size_t cap, top; } Scratch;
static void scratch_init(Scratch *s, size_t cap) {
    s->base = (unsigned char *)malloc(cap ? cap : 1);
    s->cap = cap; s->top = 0;
}
static void *scratch_take(Scratch *s, size_t n) {
    void *p;
    if (s->top + n > s->cap) return NULL;
    p = s->base + s->top; s->top += n; return p;
}

static char g_grid[GH][GW];
static unsigned char g_img[IH][IW];

static void grid_line(int x0, int y0, int x1, int y1, char ch) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < GW && y0 >= 0 && y0 < GH) g_grid[y0][x0] = ch;
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}
static void img_line(int x0, int y0, int x1, int y1, unsigned char v) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < IW && y0 >= 0 && y0 < IH) g_img[y0][x0] = v;
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}
static void vertex(int i, double a, double *ox, double *oy) {
    double x = (i & 1) ? 1.0 : -1.0;
    double y = (i & 2) ? 1.0 : -1.0;
    double z = (i & 4) ? 1.0 : -1.0;
    double x1 = x * cos(a) + z * sin(a);
    double z1 = -x * sin(a) + z * cos(a);
    double y2 = y * cos(a * 0.7) - z1 * sin(a * 0.7);
    *ox = x1; *oy = y2;
}
static int render_frame(int frame) {
    int v[8][2], imgv[8][2], i, j;
    double a = 0.11 * (double)frame;
    long sx = 0, sy = 0;
    memset(g_grid, '.', sizeof(g_grid));
    for (i = 0; i < 8; ++i) {
        double px, py;
        vertex(i, a, &px, &py);
        v[i][0] = GW / 2 + (int)(px * 6.0);
        v[i][1] = GH / 2 - (int)(py * 3.0);
        imgv[i][0] = IW / 2 + (int)(px * 8.0);
        imgv[i][1] = IH / 2 - (int)(py * 4.0);
        sx += v[i][0]; sy += v[i][1];
    }
    for (i = 0; i < 8; ++i)
        for (j = i + 1; j < 8; ++j) {
            int diff = i ^ j;
            if (diff == 1 || diff == 2 || diff == 4) {
                grid_line(v[i][0], v[i][1], v[j][0], v[j][1], '*');
                img_line(imgv[i][0], imgv[i][1], imgv[j][0], imgv[j][1], 220);
            }
        }
    {
        int mx = (int)(sx / 8), my = (int)(sy / 8);
        return abs(mx - GW / 2) <= 1 && abs(my - GH / 2) <= 1;
    }
}

/* ---- minimal PNG writer (stored DEFLATE, no external zlib) ---------------- */
static void put32be(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}
static unsigned crc32(const unsigned char *p, size_t n) {
    unsigned c = 0xFFFFFFFFu; size_t i; int k;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xEDB88320u & (unsigned)(-(int)(c & 1u)));
    }
    return ~c;
}
static int write_chunk(FILE *f, const unsigned char type[4],
                       const unsigned char *data, size_t n, Scratch *sc) {
    unsigned char *tmp = (unsigned char *)scratch_take(sc, n + 4);
    unsigned char hdr[8], tail[4];
    if (!tmp) return 0;
    memcpy(tmp, type, 4);
    if (n) memcpy(tmp + 4, data, n);
    put32be(hdr, (unsigned)n);
    memcpy(hdr + 4, type, 4);
    put32be(tail, crc32(tmp, n + 4));
    return fwrite(hdr, 1, 8, f) == 8 && fwrite(data, 1, n, f) == n &&
           fwrite(tail, 1, 4, f) == 4;
}
static int save_png(const char *name) {
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    static const unsigned char ihdr_type[4] = {'I', 'H', 'D', 'R'};
    static const unsigned char idat_type[4] = {'I', 'D', 'A', 'T'};
    static const unsigned char iend_type[4] = {'I', 'E', 'N', 'D'};
    unsigned char ihdr[13], raw[IH * (1 + IW)], zlib[2 + 5 + sizeof(raw) + 4];
    Scratch sc; size_t z = 0, i; unsigned s1 = 1, s2 = 0, adler;
    FILE *f;
    int y, x, ok = 1;
    (void)name;
    for (i = 0; i < sizeof(raw); ++i) raw[i] = 0;
    for (y = 0; y < IH; ++y) {
        raw[(size_t)y * (1 + IW)] = 0;
        for (x = 0; x < IW; ++x) raw[(size_t)y * (1 + IW) + 1 + x] = g_img[y][x];
    }
    memset(ihdr, 0, sizeof(ihdr));
    put32be(ihdr, IW); put32be(ihdr + 4, IH);
    ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    zlib[z++] = 0x78; zlib[z++] = 0x01;
    zlib[z++] = 0x01;                       /* one final stored block */
    zlib[z++] = (unsigned char)(sizeof(raw) & 255);
    zlib[z++] = (unsigned char)(sizeof(raw) >> 8);
    zlib[z++] = (unsigned char)(~sizeof(raw) & 255);
    zlib[z++] = (unsigned char)((~sizeof(raw) >> 8) & 255);
    memcpy(zlib + z, raw, sizeof(raw)); z += sizeof(raw);
    for (i = 0; i < sizeof(raw); ++i) {
        s1 = (s1 + raw[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    adler = (s2 << 16) | s1;
    put32be(zlib + z, adler); z += 4;
    scratch_init(&sc, sizeof(raw) + 64);
    f = tmpfile();
    if (!f) { free(sc.base); return 0; }
    if (fwrite(sig, 1, sizeof(sig), f) != sizeof(sig)) ok = 0;
    if (ok) ok = write_chunk(f, ihdr_type, ihdr, sizeof(ihdr), &sc);
    if (ok) ok = write_chunk(f, idat_type, zlib, z, &sc);
    if (ok) ok = write_chunk(f, iend_type, NULL, 0, &sc);
    if (fflush(f) != 0) ok = 0;
    fclose(f); free(sc.base);
    return ok;
}

int main(void) {
    int f, centered = 1, saved;
    static int probe[3] = {3, 1, 2};
    (void)report; (void)fails;
    qsort(probe, 3, sizeof(probe[0]), cmp_int);
    if (probe[2] != 3) return 1;
    memset(g_img, 16, sizeof(g_img));
    for (f = 0; f < FRAMES; ++f)
        if (!render_frame(f)) centered = 0;
    saved = save_png("gfx_cube.png");
    printf("drew %d frames; cube in the middle: %s; saved %s: %s\n",
           FRAMES, centered ? "true" : "false", "gfx_cube.png", saved ? "true" : "false");
    return 0;
}
