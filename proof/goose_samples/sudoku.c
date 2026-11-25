/* Independent modern-C port of the "sudoku" Goose sample workload.
 * C11, no Goose source copied. See proof/goose_samples/README.md. */
#include "common.h"

static int g[4][4];
static int solve(int pos) {
    int r, c, v;
    if (pos == 16) return 1;
    r = pos / 4; c = pos % 4;
    if (g[r][c]) return solve(pos + 1);
    for (v = 1; v <= 4; ++v) {
        int i, ok = 1, br = (r / 2) * 2, bc = (c / 2) * 2;
        for (i = 0; i < 4; ++i)
            if (g[r][i] == v || g[i][c] == v ||
                g[br + i / 2][bc + i % 2] == v) ok = 0;
        if (ok) { g[r][c] = v; if (solve(pos + 1)) return 1; g[r][c] = 0; }
    }
    return 0;
}
int main(void) {
    int i, j;
    g[0][0] = 1; g[1][2] = 4; g[2][1] = 3; g[3][3] = 2;
    CHECK("sudoku_solve", solve(0));
    for (i = 0; i < 4; ++i) {
        printf("sudoku");
        for (j = 0; j < 4; ++j) printf(" %d", g[i][j]);
        printf("\n");
    }
    report("SUDOKU"); return fails;
}
