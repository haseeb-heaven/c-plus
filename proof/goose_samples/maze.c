/* Independent modern-C port of the "maze" Goose sample workload.
 * C11, no Goose source copied. See proof/goose_samples/README.md. */
#include "common.h"

static char grid[5][5] = {
    {'S','.','.','#','.'}, {'#','#','.','#','.'}, {'#','.','.','.','.'},
    {'#','.','#','#','#'}, {'#','.','.','.','E'} };
static int seen[5][5], found;
static void dfs(int r, int c) {
    if (r < 0 || r > 4 || c < 0 || c > 4 || grid[r][c] == '#' || seen[r][c]) return;
    seen[r][c] = 1; found++;
    dfs(r - 1, c); dfs(r + 1, c); dfs(r, c - 1); dfs(r, c + 1);
}
int main(void) {
    dfs(0, 0);
    printf("maze reachable %d\n", found);
    CHECK("maze_found", seen[4][4] == 1);
    report("MAZE"); return fails;
}
