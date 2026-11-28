/* Independent modern-C port of the "life" Goose sample workload.
 * C11, no Goose source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

static int a[5][5] = {{0,0,0,0,0},{0,1,1,1,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}};
int main(void) {
    int b[5][5] = {0}, r, c, gen, alive = 0;
    for (gen = 1; gen <= 2; ++gen) {
        memset(b, 0, sizeof(b));
        for (r = 0; r < 5; ++r) for (c = 0; c < 5; ++c) {
            int n = 0, dr, dc;
            for (dr = -1; dr <= 1; ++dr) for (dc = -1; dc <= 1; ++dc) {
                int nr = r + dr, nc = c + dc;
                if ((dr || dc) && nr >= 0 && nr < 5 && nc >= 0 && nc < 5 && a[nr][nc]) n++;
            }
            b[r][c] = (a[r][c] && (n == 2 || n == 3)) || (!a[r][c] && n == 3);
        }
        memcpy(a, b, sizeof(a));
    }
    for (r = 0; r < 5; ++r) for (c = 0; c < 5; ++c) alive += a[r][c];
    printf("life alive %d\n", alive);
    CHECK("life_blinker", alive == 3); report("LIFE"); return fails;
}
