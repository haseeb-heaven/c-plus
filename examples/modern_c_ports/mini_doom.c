/* Independent modern-C port of the "mini_doom" sample workload.
 * C11, no upstream source was copied. Transient storage uses a one-store scope
 * arena modeled on stdlib/scoped_mem.hp; rays are traced in a headless grid. */
#include "common.h"
#include <stdint.h>
#include <math.h>

#define SCRATCH_CAP (32u * 1024u)
static unsigned char g_scratch[SCRATCH_CAP];

typedef struct { unsigned char *base; size_t cap, top; } Scope;
static void scope_init(Scope *s) { s->base = g_scratch; s->cap = SCRATCH_CAP; s->top = 0; }
static size_t scope_mark(const Scope *s) { return s->top; }
static void scope_release(Scope *s, size_t mark) { s->top = mark; }
static void *scope_take(Scope *s, size_t bytes) {
    size_t pad = (16u - (s->top & 15u)) & 15u;
    if (s->top > s->cap || pad > s->cap - s->top) return NULL;
    s->top += pad;
    if (bytes > s->cap - s->top) { s->top -= pad; return NULL; }
    { void *p = s->base + s->top; s->top += bytes; return p; }
}

static uint32_t crc32(const unsigned char *p, size_t n) {
    uint32_t c = 0xffffffffu; size_t i; int k;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return c ^ 0xffffffffu;
}
static uint32_t adler32(const unsigned char *p, size_t n) {
    uint32_t a = 1, b = 0; size_t i;
    for (i = 0; i < n; ++i) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}
static unsigned char *chunk(unsigned char *o, const char *type,
                            const unsigned char *d, size_t n) {
    uint32_t L = (uint32_t)n, crc;
    o[0]=(unsigned char)(L>>24); o[1]=(unsigned char)(L>>16);
    o[2]=(unsigned char)(L>>8);  o[3]=(unsigned char)L;
    memcpy(o+4, type, 4);
    if (n) memcpy(o+8, d, n);
    crc = crc32(o+4, 4+n);
    o += 8+n;
    o[0]=(unsigned char)(crc>>24); o[1]=(unsigned char)(crc>>16);
    o[2]=(unsigned char)(crc>>8);  o[3]=(unsigned char)crc;
    return o+4;
}
static int png_save(unsigned char *out, size_t cap,
                    const unsigned char *fb, int w, int h) {
    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    unsigned char ihdr[13], raw[1024], z[1035];
    unsigned char *p, *zp;
    size_t raw_len, zlen, q = 0, i;
    uint32_t ad;
    if (w <= 0 || h <= 0) return 0;
    raw_len = (size_t)(w+1) * (size_t)h;
    if (raw_len > 1000u) return 0;
    zlen = raw_len + 11u;
    if (57u + zlen > cap) return 0;
    for (i = 0; i < (size_t)h; ++i) {
        raw[q++] = 0;
        memcpy(raw+q, fb + i*(size_t)w, (size_t)w); q += (size_t)w;
    }
    zp = z;
    zp[0]=0x78; zp[1]=0x01; zp[2]=0x01;
    zp[3]=(unsigned char)(raw_len & 255u); zp[4]=(unsigned char)((raw_len>>8)&255u);
    zp[5]=(unsigned char)((~raw_len)&255u); zp[6]=(unsigned char)((~raw_len>>8)&255u);
    memcpy(zp+7, raw, raw_len);
    ad = adler32(raw, raw_len);
    zp += 7 + raw_len;
    zp[0]=(unsigned char)(ad>>24); zp[1]=(unsigned char)(ad>>16);
    zp[2]=(unsigned char)(ad>>8);  zp[3]=(unsigned char)ad;
    ihdr[0]=(unsigned char)(w>>24); ihdr[1]=(unsigned char)(w>>16);
    ihdr[2]=(unsigned char)(w>>8);  ihdr[3]=(unsigned char)w;
    ihdr[4]=(unsigned char)(h>>24); ihdr[5]=(unsigned char)(h>>16);
    ihdr[6]=(unsigned char)(h>>8);  ihdr[7]=(unsigned char)h;
    ihdr[8]=8; ihdr[9]=0; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
    p = out;
    memcpy(p, sig, 8); p += 8;
    p = chunk(p, "IHDR", ihdr, 13);
    p = chunk(p, "IDAT", z, zlen);
    p = chunk(p, "IEND", NULL, 0);
    return (int)(p - out);
}

#define MS 16
static unsigned char map[MS][MS];
static void map_init(void) {
    int x, y;
    for (y = 0; y < MS; ++y) for (x = 0; x < MS; ++x)
        map[y][x] = (unsigned char)(x == 0 || y == 0 || x == MS-1 || y == MS-1 ||
                                    (x == 8 && y >= 4 && y <= 11));
}
static int solid(int x, int y) {
    if (x < 0 || y < 0 || x >= MS || y >= MS) return 1;
    return map[y][x] != 0;
}
static double cast_ray(double px, double py, double dx, double dy) {
    double d = 0.0; int s;
    for (s = 0; s < 128; ++s) {
        d += 0.05;
        if (solid((int)(px+dx*d), (int)(py+dy*d))) return d;
    }
    return 6.4;
}

int main(void) {
    Scope sc; scope_init(&sc);
    unsigned char *fb, *png, *depth;
    size_t mark, png_len, i, f, c, r;
    unsigned char hash = 0;
    double px = 2.5, py = 2.5, ang = 0.25;
    const int fw = 32, fh = 16;
    int rays_ok = 1, move_ok = 1, saved;
    unsigned char *dep;

    map_init();
    mark = scope_mark(&sc);
    fb = scope_take(&sc, (size_t)fw*fh);
    depth = scope_take(&sc, (size_t)fw);
    if (!fb || !depth) return 1;

    for (f = 0; f < 30; ++f) {
        /* bounded walk: turn a little, step only into open cells */
        ang += 0.013;
        {
            double nx = px + cos(ang)*0.04, ny = py + sin(ang)*0.04;
            if (!solid((int)nx, (int)ny) && nx > 1.0 && ny > 1.0 &&
                nx < MS-1 && ny < MS-1) { px = nx; py = ny; }
        }
        if (solid((int)px, (int)py)) move_ok = 0;

        memset(fb, ' ', (size_t)fw*fh);
        dep = depth;
        for (c = 0; c < (size_t)fw; ++c) {
            double cam = 2.0*(double)(c+0.5)/(double)fw - 1.0;
            double a = ang + cam*0.65;
            double dist = cast_ray(px, py, cos(a), sin(a));
            int h, top, bot;
            if (dist > 6.4 || dist <= 0.0) rays_ok = 0;
            h = (int)(9.0/(dist + 0.25));
            if (h < 1) h = 1; if (h > fh-2) h = fh-2;
            top = (fh-h)/2; bot = top + h;
            dep[c] = (unsigned char)(dist*10.0);
            for (r = 0; r < (size_t)fh; ++r)
                fb[r*fw+c] = (r >= (size_t)top && r < (size_t)bot)
                    ? (unsigned char)('#' - (int)(dist > 3.0 ? 1 : 0)) : '.';
        }
        for (i = 0; i < (size_t)fw*fh; ++i) hash = (unsigned char)(hash*31u + fb[i]);
        (void)dep;
    }
    if (!rays_ok || !move_ok || hash == 0u) fails++;

    png = scope_take(&sc, 8192u);
    png_len = png ? (size_t)png_save(png, 8192u, fb, fw, fh) : 0u;
    saved = png_len > 57u && png[0] == 137;
    if (!saved) fails++;
    printf("drew 30 frames; saved mini_doom.png: %s\n", saved ? "true" : "false");
    scope_release(&sc, mark);
    return fails;
}
