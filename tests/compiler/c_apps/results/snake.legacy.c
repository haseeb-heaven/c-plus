#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_apps/apps/snake.c"
/* Original deterministic snake movement simulator (plain C89). */
#include <stdio.h>

#define W 8
#define H 8

static int grid[H][W];
static int moves[8][2] = {{1,0},{1,0},{0,1},{0,1},{-1,0},{0,-1},{0,-1},{-1,0}};

int main(void) {
  int x = 3, y = 3, i, steps = 0;
  for (i = 0; i < H; i++) {
    int j;
    for (j = 0; j < W; j++)
      grid[i][j] = 0;
  }
  grid[y][x] = 1;
  for (i = 0; i < 8; i++) {
    int nx = x + moves[i][0];
    int ny = y + moves[i][1];
    if (nx < 0 || nx >= W || ny < 0 || ny >= H)
      continue;
    x = nx;
    y = ny;
    grid[y][x] = 1;
    steps++;
  }
  printf("steps=%d end=%d,%d\n", steps, x, y);
  return 0;
}
