/*
 * lc26 - global variables (linkage and static storage duration).
 *
 * Independent legacy-C ABI probe: exercises file-scope objects with external
 * and internal linkage, aggregate initialisation, and mutation across calls.
 * Prints one stable result line; exits 0 only when every check passes.
 */
#include <stdio.h>
#include <string.h>

/* external linkage */
unsigned long g_total = 0;
int g_epoch = 3;
const char *g_label = "lc26";

/* internal linkage */
static int s_counter = 0;
static const int s_table[4] = { 2, 4, 6, 8 };

static int advance(int by)
{
    s_counter += by;
    g_total += (unsigned long)by;
    g_epoch += 1;
    return s_counter;
}

int main(void)
{
    int i;

    for (i = 0; i < 4; ++i) {
        (void)advance(s_table[i]);
    }
    if (s_counter != 20 || g_total != 20UL || g_epoch != 7) {
        printf("lc26 result: FAIL globals counter=%d total=%lu epoch=%d\n",
               s_counter, g_total, g_epoch);
        return 1;
    }
    if (strcmp(g_label, "lc26") != 0) {
        printf("lc26 result: FAIL label\n");
        return 1;
    }
    printf("lc26 result: OK counter=%d total=%lu epoch=%d label=%s\n",
           s_counter, g_total, g_epoch, g_label);
    return 0;
}
