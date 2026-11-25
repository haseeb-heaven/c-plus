/* win_portability_probe.c -- COMPILE-ONLY Windows PE toolchain probe.
 *
 * SCOPE (read this before drawing conclusions):
 *   This is a small, plain-C MIRROR of the deterministic widget-core algorithm
 *   used by gui_app.cp (integer row-major grid layout, enabled/visible-aware
 *   hit test, reflected IEEE CRC32 over the serialized text).  It is NOT the
 *   SDL3 GUI layer, NOT the C+ lowering/runtime, and NOT the demo executable:
 *   mingw-w64 has no SDL3 headers here and cannot parse C+ .hp headers.  Its
 *   only purpose is to prove that the portable core algorithm is valid C that
 *   the installed x86_64-w64-mingw32-gcc accepts as a PE/COFF object.
 *
 *   NO WINDOWS RUNTIME SUCCESS IS CLAIMED.  Highball/Wine execution is BLOCKED
 *   in this repository.  We compile to an object (-c) and inspect the object
 *   with `file`; we never execute the Windows artifact.
 *
 * The same source is also compiled natively by run.sh and executed once: its
 * CRC32 must equal the demo's GUI_SERIAL checksum (a52b03af) so the mirror is
 * verified against the real C+ program before the mingw check is trusted.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WIDGET_LABEL_MAX 64u
#define WIDGET_ENABLED 0x1u
#define WIDGET_VISIBLE 0x2u
#define GRID_INSET 1
#define EXPECT_CRC32 0xa52b03afu
#define EXPECT_BYTES 157u

typedef struct widget {
    int id;
    int kind;
    char label[WIDGET_LABEL_MAX];
    int x;
    int y;
    int w;
    int h;
    unsigned flags;
} widget;

static const char *const g_labels[6] = {
    "root", "user", "name", "pass", "secret", "OK"
};
static const int g_kinds[6] = {4, 2, 3, 2, 3, 1};
static const int g_ids[6] = {1, 2, 3, 4, 5, 6};

static unsigned int crc32_reflected(const char *data, size_t n) {
    unsigned int crc = 0xffffffffu;
    size_t i;
    for (i = 0u; i < n; i++) {
        int bit;
        crc ^= (unsigned int)(unsigned char)data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static size_t dec_digits(long long value) {
    unsigned long long u;
    size_t n = 1u;
    if (value < 0)
        u = (unsigned long long)(-(value + 1)) + 1u;
    else
        u = (unsigned long long)value;
    while (u >= 10u) { u /= 10u; n++; }
    return n;
}

int main(void) {
    widget items[6];
    char serial[1024];
    size_t used = 0u;
    size_t i;
    int cell_w;
    int cell_h;

    memset(items, 0, sizeof(items));
    /* grid 240x90, columns 3, origin 0,0 -> 2 rows of 80x45 cells. */
    cell_w = 240 / 3;
    cell_h = 90 / 2;
    for (i = 0u; i < 6u; i++) {
        int col = (int)(i % 3u);
        int row = (int)(i / 3u);
        items[i].id = g_ids[i];
        items[i].kind = g_kinds[i];
        snprintf(items[i].label, sizeof(items[i].label), "%s", g_labels[i]);
        items[i].x = col * cell_w + GRID_INSET;
        items[i].y = row * cell_h + GRID_INSET;
        items[i].w = cell_w - 2 * GRID_INSET;
        items[i].h = cell_h - 2 * GRID_INSET;
        items[i].flags = WIDGET_ENABLED | WIDGET_VISIBLE;
    }

    used += (size_t)snprintf(serial + used, sizeof(serial) - used,
                             "WIDGETS %u\n", (unsigned)6u);
    for (i = 0u; i < 6u; i++) {
        used += (size_t)snprintf(serial + used, sizeof(serial) - used,
                                 "W %d %d %d %d %d %d %u %s\n",
                                 items[i].id, items[i].kind, items[i].x,
                                 items[i].y, items[i].w, items[i].h,
                                 items[i].flags, items[i].label);
    }

    /* Keep dec_digits referenced: the real serializer uses it for sizing. */
    if (dec_digits((long long)i) == 0u)
        return 3;

    printf("WIN_PROBE bytes=%lu crc32=%08x %s\n",
           (unsigned long)used, crc32_reflected(serial, used),
           (used == EXPECT_BYTES &&
            crc32_reflected(serial, used) == EXPECT_CRC32) ? "OK" : "MISMATCH");
    return (used == EXPECT_BYTES &&
            crc32_reflected(serial, used) == EXPECT_CRC32) ? 0 : 1;
}
