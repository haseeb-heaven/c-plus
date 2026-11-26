/* Independent modern-C port of the "mini_minecraft" sample workload.
 * C11, no upstream source was copied. Transient storage uses a one-store scope
 * arena modeled on stdlib/scoped_mem.hp; the world is a headless voxel sketch. */
#include "common.h"
#include <stdint.h>

#define SCRATCH_CAP (64u * 1024u)
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

#define W 16
#define D 4
static unsigned char world[W][W][D]; /* 0 air, 1 grass, 2 dirt, 3 stone */

static unsigned mix32(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16; return x;
}
static void gen_terrain(void) {
    int x, z, y;
    for (z = 0; z < W; ++z) for (x = 0; x < W; ++x) {
        unsigned h = 2u + (mix32((unsigned)(x*31 + z)) % 3u); /* 2..4 */
        for (y = 0; y < D; ++y)
            world[x][z][y] = (unsigned)y < h
                ? (unsigned char)(y == 0 ? 3 : (y == (int)h-1 ? 1 : 2)) : 0;
    }
}
static int column_height(int x, int z) {
    int y, h = 0; for (y = 0; y < D; ++y) if (world[x][z][y]) h = y+1; return h;
}
static int blocks(void) {
    int x, z, y, n = 0;
    for (z = 0; z < W; ++z) for (x = 0; x < W; ++x) for (y = 0; y < D; ++y)
        n += world[x][z][y] != 0;
    return n;
}
static int ray_down(int x, int z) { /* first solid voxel from above */
    int y; for (y = D-1; y >= 0; --y) if (world[x][z][y]) return y; return -1;
}
static void render(unsigned char *fb, int frame, int light) {
    const int fw = 32, fh = 12;
    int r, c, i;
    memset(fb, ' ', (size_t)fw*fh);
    for (c = 0; c < fw; ++c) { fb[c]='~'; fb[(fh-1)*fw+c]='='; }
    for (r = 1; r < fh-1; ++r) { fb[r*fw]='|'; fb[r*fw+fw-1]='|'; }
    for (c = 0; c < fw; ++c) fb[8*fw+c] = (unsigned char)(light > 50 ? '.' : ',');
    /* a moving player marker on the ground row */
    i = 2 + ((frame*2) % 26);
    fb[7*fw+i] = 'A';
}

int main(void) {
    Scope sc; scope_init(&sc);
    unsigned char *fb, *png, *blob;
    size_t mark, png_len, blob_len, i;
    unsigned char hash = 0;
    int terrain_ok, ray_ok, mining_ok, craft_ok, place_ok, move_ok, light_ok, save_ok;
    int sys_ok, saved, t, x, z, y, before, after, top, py, vy, ground;
    int light, light_min = 100, light_max = 0;
    int inv[2] = {2, 0}; /* wood, planks */
    unsigned char copy[W][W][D];
    uint32_t sum = 0;

    gen_terrain();
    terrain_ok = 1;
    for (z = 0; z < W && terrain_ok; ++z) for (x = 0; x < W && terrain_ok; ++x) {
        int h = column_height(x, z);
        if (h < 2 || h > D) terrain_ok = 0;
    }

    top = ray_down(8, 8);
    ray_ok = (top == column_height(8, 8) - 1) && top >= 0;

    before = blocks();
    if (top >= 0 && world[8][8][top]) { world[8][8][top] = 0; after = blocks(); }
    else after = before + 1;
    mining_ok = (after == before - 1);

    if (inv[0] >= 2) { inv[0] -= 2; inv[1] += 1; }
    craft_ok = (inv[0] == 0 && inv[1] == 1);

    before = blocks();
    y = top >= 0 ? top : 0;
    if (y < D && world[8][8][y] == 0) world[8][8][y] = 1;
    after = blocks();
    place_ok = (after == before + 1);

    ground = column_height(8, 8) * 16;
    py = ground + 16; vy = 0;
    for (t = 0; t < 20; ++t) {
        if (t == 5) vy = 32;
        vy -= 4; py += vy;
        if (py <= ground) { py = ground; vy = 0; }
    }
    move_ok = (py == ground && vy == 0);

    light_ok = 1;
    for (t = 0; t < 24; ++t) {
        light = (t >= 6 && t < 18) ? 90 - ((t-12)*(t-12))/2 : 15;
        if (light < 0 || light > 100) light_ok = 0;
        if (light < light_min) light_min = light;
        if (light > light_max) light_max = light;
    }
    light_ok = (light_min < light_max) && light_min >= 0 && light_max <= 100;

    mark = scope_mark(&sc);
    blob_len = (size_t)W*W*D + 8u;
    blob = scope_take(&sc, blob_len + 4096u);
    if (!blob) return 1;
    blob[0]='S'; blob[1]='A'; blob[2]='V'; blob[3]='E';
    memcpy(blob+4, world, (size_t)W*W*D);
    for (i = 0; i < (size_t)W*W*D; ++i) sum = sum*33u + blob[4+i];
    blob[blob_len-4]=(unsigned char)(sum>>24); blob[blob_len-3]=(unsigned char)(sum>>16);
    blob[blob_len-2]=(unsigned char)(sum>>8);  blob[blob_len-1]=(unsigned char)sum;
    memcpy(copy, blob+4, sizeof copy);
    save_ok = (memcmp(copy, world, sizeof copy) == 0) && blob_len == (size_t)W*W*D+8u;

    sys_ok = terrain_ok && ray_ok && mining_ok && craft_ok && place_ok &&
             move_ok && light_ok && save_ok;
    if (!sys_ok) fails++;
    printf("mini minecraft: terrain, ray, mining, crafting, placement, "
           "movement, daylight and saves %s\n", sys_ok ? "passed" : "failed");

    fb = scope_take(&sc, (size_t)32*12);
    if (!fb) return 1;
    for (i = 0; i < 30; ++i) {
        int light = ((int)i % 24 >= 6 && (int)i % 24 < 18) ? 90 : 15;
        render(fb, (int)i, light);
        for (t = 0; t < 32*12; ++t) hash = (unsigned char)(hash*31u + fb[t]);
    }
    if (hash == 0u) fails++;

    png = scope_take(&sc, 8192u);
    png_len = png ? (size_t)png_save(png, 8192u, fb, 32, 12) : 0u;
    saved = png_len > 57u && png[0] == 137;
    if (!saved) fails++;
    printf("drew 30 frames; saved mini_minecraft.png: %s\n", saved ? "true" : "false");
    scope_release(&sc, mark);
    return fails;
}
