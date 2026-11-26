/* Independent modern-C port of the "ui_todo" sample workload.
 * C11, no upstream source was copied. Transient storage uses a one-store scope
 * arena modeled on stdlib/scoped_mem.hp; rendering is a headless ASCII buffer. */
#include "common.h"
#include <stdint.h>

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

/* Minimal grayscale PNG writer: signature, IHDR, one stored DEFLATE block, IEND. */
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
        raw[q++] = 0; /* filter: none */
        memcpy(raw+q, fb + i*(size_t)w, (size_t)w); q += (size_t)w;
    }
    zp = z;
    zp[0]=0x78; zp[1]=0x01; zp[2]=0x01; /* zlib, final stored block */
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

#define FW 24
#define FH 9
typedef struct { char name[20]; int done; } Task;
static int done_count(const Task *t, int n) {
    int i, d = 0; for (i = 0; i < n; ++i) d += t[i].done; return d;
}
static void render(unsigned char *fb, const Task *t, int n, int frame) {
    int r, c, i, len; char line[FW];
    memset(fb, ' ', (size_t)FW*FH);
    for (c = 0; c < FW; ++c) { fb[c] = '-'; fb[(FH-1)*FW+c] = '-'; }
    for (r = 0; r < FH; ++r) { fb[r*FW] = '|'; fb[r*FW+FW-1] = '|'; }
    { const char *title = "TODO";
      for (c = 0; title[c] && c+2 < FW-1; ++c) fb[FW+2+c] = (unsigned char)title[c]; }
    for (i = 0; i < n; ++i) {
        len = snprintf(line, sizeof line, " %s [%c]", t[i].name, t[i].done ? 'x' : ' ');
        for (c = 0; c < len && c < FW-2; ++c) fb[(3+i)*FW+1+c] = (unsigned char)line[c];
    }
    len = snprintf(line, sizeof line, " f%02d %d/%d", frame+1, done_count(t, n), n);
    for (c = 0; c < len && c < FW-2; ++c) fb[(FH-2)*FW+1+c] = (unsigned char)line[c];
}

int main(void) {
    Scope sc; scope_init(&sc);
    Task tasks[3] = {{"buy milk",0},{"write tests",1},{"ship build",0}};
    const int frames = 30, total = 3;
    unsigned char hash = 0, *fb, *png;
    size_t mark, png_len, i, f;
    int done = 0, saved;

    for (i = 0; i < (size_t)total; ++i) done += tasks[i].done;
    if (done != 1) fails++;

    mark = scope_mark(&sc);
    fb = scope_take(&sc, (size_t)FW*FH);
    if (!fb) return 1;
    for (f = 0; f < (size_t)frames; ++f) {
        render(fb, tasks, total, (int)f);
        for (i = 0; i < (size_t)FW*FH; ++i) hash = (unsigned char)(hash*31u + fb[i]);
    }
    if (hash == 0u) fails++;

    png = scope_take(&sc, 8192u);
    png_len = png ? (size_t)png_save(png, 8192u, fb, FW, FH) : 0u;
    saved = png_len > 57u && png[0] == 137;
    if (!saved) fails++;

    printf("drew %d frames; %d of %d tasks done; saved ui_todo.png: %s\n",
           frames, done, total, saved ? "true" : "false");
    scope_release(&sc, mark);
    return fails;
}
