/*
 * lc27 - global variables: initialised aggregate objects and pointer to global.
 *
 * Independent legacy-C ABI probe: file-scope struct, array and a pointer that
 * aliases a global object. Verifies static-storage initialisation and sharing.
 */
#include <stdio.h>

struct config {
    int id;
    int retries;
    unsigned long mask;
};

struct config g_cfg = { 27, 5, 0xA5A5UL };
unsigned short g_ring[6] = { 1, 2, 3, 4, 5, 6 };
struct config *g_alias = &g_cfg;

static unsigned long checksum(const unsigned short *p, int n)
{
    unsigned long sum = 0UL;
    int i;
    for (i = 0; i < n; ++i) {
        sum += (unsigned long)p[i];
    }
    return sum;
}

int main(void)
{
    unsigned long sum;

    g_alias->retries += 2;
    g_ring[5] = 9;
    sum = checksum(g_ring, 6);
    if (g_cfg.retries != 7 || g_alias->retries != 7 || g_cfg.id != 27 ||
        g_cfg.mask != 0xA5A5UL || sum != 24UL) {
        printf("lc27 result: FAIL id=%d retries=%d mask=%lx sum=%lu\n",
               g_cfg.id, g_cfg.retries, g_cfg.mask, sum);
        return 1;
    }
    printf("lc27 result: OK id=%d retries=%d mask=%lx sum=%lu\n",
           g_cfg.id, g_cfg.retries, g_cfg.mask, sum);
    return 0;
}
