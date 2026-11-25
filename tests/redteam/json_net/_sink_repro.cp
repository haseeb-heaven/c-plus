/* Exact reproducers for the two http_sink_write CORRUPT findings.
 * Build: ./build/c+ build tests/redteam/json_net/_sink_repro.cp \
 *          --default --sanitize=none -lcurl -o <bin>
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

#define CAP 64
#define PRE 16
#define POST 16
static char g[PRE + CAP + POST];
static char src[8];

static void dump(const char *tag, http_sink *s, size_t ret) {
    int i;
    printf("%s: ret=%zu used=%zu overflow=%d\n", tag, ret, s->used, s->overflow);
    printf("  g[PRE-1] (byte before buf) = 0x%02x (expect a5)\n",
           (unsigned char)g[PRE - 1]);
    printf("  buf[0..7] =", 0);
    for (i = 0; i < 8; i++) printf(" %02x", (unsigned char)g[PRE + i]);
    printf("\n");
    printf("  buf[CAP-1] = 0x%02x  g[PRE+CAP] (post-guard) = 0x%02x\n",
           (unsigned char)g[PRE + CAP - 1], (unsigned char)g[PRE + CAP]);
}

int main(void) {
    http_sink s;
    size_t ret;
    memset(src, 'B', sizeof src);

    /* FINDING 1: used == size. The bounds-refusal branch writes buf[used] = 0
     * at index size, one byte PAST the caller's [0, size-1] buffer. */
    memset(g, 0xA5, sizeof g);
    s.buf = g + PRE; s.size = CAP; s.used = CAP; s.overflow = 0;
    ret = http_sink_write(src, 1u, 8u, (void *)&s);
    dump("F1 used==size", &s, ret);

    /* FINDING 2: used == SIZE_MAX. The overflow branch computes
     * room = size - used - 1 and used + 1 with wraparound, then
     * memcpy(buf + used, ptr, room) writes BEFORE the buffer. */
    memset(g, 0xA5, sizeof g);
    s.buf = g + PRE; s.size = CAP; s.used = SIZE_MAX; s.overflow = 0;
    ret = http_sink_write(src, 1u, 8u, (void *)&s);
    dump("F2 used==SIZE_MAX", &s, ret);
    return 0;
}
