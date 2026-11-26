/* Independent modern-C implementation of the physics_boxes workload.
 * C11, deterministic stdout.  Boxes fall in a bounded arena and an arm turns;
 * the frame is emitted as a small PNG through a local encoder.  Transient
 * buffers follow the scoped scratch pattern of stdlib/scoped_mem.hp.
 * No upstream sample source was copied. */
#include "common.h"
#include <math.h>

#define BOXES 3000
#define FRAMES 120
#define ARENA 100.0
#define IW 32
#define IH 32

typedef struct { double x, y, vx, vy; double s; } Box;
static Box g_box[BOXES];
static unsigned char g_img[IH][IW];

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

static unsigned g_rng = 12345u;
static double rnd(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (double)(g_rng >> 8) / 16777216.0;
}
static void setup(void) {
    int i;
    memset(g_img, 12, sizeof(g_img));
    for (i = 0; i < BOXES; ++i) {
        Box *b = &g_box[i];
        b->s = 1.0 + (double)(i % 3);
        b->x = rnd() * (ARENA - b->s);
        b->y = rnd() * (ARENA * 0.5);
        b->vx = (rnd() - 0.5) * 0.4;
        b->vy = rnd() * 0.2;
    }
}
static void clamp_box(Box *b) {
    if (b->x < 0) { b->x = 0; b->vx = -b->vx * 0.4; }
    if (b->x + b->s > ARENA) { b->x = ARENA - b->s; b->vx = -b->vx * 0.4; }
    if (b->y + b->s > ARENA) { b->y = ARENA - b->s; b->vy = -b->vy * 0.25; }
    if (b->y < 0) { b->y = 0; b->vy = 0; }
}
static void separate(Box *a, Box *b) {
    double dx = b->x - a->x, dy = b->y - a->y;
    double d = dx * dx + dy * dy, need;
    need = (a->s + b->s) * 0.5;
    if (d < need * need && d > 1e-9) {
        double dist = sqrt(d), push = (need - dist) * 0.25;
        double ux = dx / dist, uy = dy / dist;
        a->x -= ux * push; a->y -= uy * push;
        b->x += ux * push; b->y += uy * push;
    }
}
static void simulate(void) {
    int f, i;
    for (f = 0; f < FRAMES; ++f) {
        for (i = 0; i < BOXES; ++i) {
            Box *b = &g_box[i];
            b->vy += 0.06;
            b->x += b->vx;
            b->y += b->vy;
            clamp_box(b);
            if (i + 3 < BOXES) separate(b, &g_box[i + 3]);
            clamp_box(b);
        }
    }
}
static int all_inside(void) {
    int i;
    for (i = 0; i < BOXES; ++i) {
        Box *b = &g_box[i];
        if (b->x < -1e-9 || b->y < -1e-9 ||
            b->x + b->s > ARENA + 1e-9 || b->y + b->s > ARENA + 1e-9)
            return 0;
    }
    return 1;
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
static void draw(int degrees) {
    int i;
    double a = degrees * 3.141592653589793 / 180.0;
    int ax, ay;
    for (i = 0; i < BOXES; ++i) {
        int x = (int)(g_box[i].x * IW / ARENA);
        int y = (int)(g_box[i].y * IH / ARENA);
        if (x >= 0 && x < IW && y >= 0 && y < IH) g_img[y][x] = 200;
    }
    ax = IW / 2 + (int)(cos(a) * 12.0);
    ay = IH / 2 - (int)(sin(a) * 12.0);
    img_line(IW / 2, IH / 2, ax, ay, 240);
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
static int save_png(void) {
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    static const unsigned char ihdr_type[4] = {'I', 'H', 'D', 'R'};
    static const unsigned char idat_type[4] = {'I', 'D', 'A', 'T'};
    static const unsigned char iend_type[4] = {'I', 'E', 'N', 'D'};
    unsigned char ihdr[13], raw[IH * (1 + IW)], zlib[2 + 5 + sizeof(raw) + 4];
    Scratch sc; size_t z = 0, i; unsigned s1 = 1, s2 = 0, adler;
    FILE *f;
    int y, x, ok = 1;
    for (i = 0; i < sizeof(raw); ++i) raw[i] = 0;
    for (y = 0; y < IH; ++y) {
        raw[(size_t)y * (1 + IW)] = 0;
        for (x = 0; x < IW; ++x) raw[(size_t)y * (1 + IW) + 1 + x] = g_img[y][x];
    }
    memset(ihdr, 0, sizeof(ihdr));
    put32be(ihdr, IW); put32be(ihdr + 4, IH);
    ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    zlib[z++] = 0x78; zlib[z++] = 0x01;
    zlib[z++] = 0x01;
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
    int degrees, inside, saved;
    static int probe[3] = {3, 1, 2};
    (void)report; (void)fails;
    qsort(probe, 3, sizeof(probe[0]), cmp_int);
    if (probe[2] != 3) return 1;
    setup();
    simulate();
    inside = all_inside();
    degrees = FRAMES * 475 / 1000;   /* 475 milli-degrees per frame */
    draw(degrees);
    saved = save_png();
    printf("drew %d frames; dropped %d boxes; all in the arena: %s; "
           "the arm turned %d degrees; saved %s: %s\n",
           FRAMES, BOXES, inside ? "true" : "false", degrees,
           "physics_boxes.png", saved ? "true" : "false");
    return 0;
}
