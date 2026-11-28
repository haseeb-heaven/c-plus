#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_apps/full_apps/tetris_mini.c"
/* Fully playable deterministic Tetris-mini (plain C89).
 * stdin: l/r move, d drop, q quit. Fixed piece sequence. */
#include <stdio.h>

#define H 10
#define W 8
static int grid[H][W];
static int px[4] = {2, 3, 4, 5};
static int py[4] = {0, 0, 0, 0};
static int piece_n;

static void draw(void) {
  int i, j;
  for (i = 0; i < H; i++) {
    for (j = 0; j < W; j++)
      putchar(grid[i][j] ? '#' : '.');
    printf("\n");
  }
}

static int can_move(int dx) {
  int i;
  for (i = 0; i < 4; i++) {
    int nx = px[i] + dx;
    if (nx < 0 || nx >= W || grid[py[i]][nx])
      return 0;
  }
  return 1;
}

int main(void) {
  int c, steps = 0, placed = 0;
  int i, j;
  for (i = 0; i < H; i++)
    for (j = 0; j < W; j++)
      grid[i][j] = 0;
  while (steps < 10) {
    draw();
    c = getchar();
    if (c == 'q' || c == EOF) break;
    if (c == 'l' && can_move(-1)) {
      for (i = 0; i < 4; i++) px[i]--;
    } else if (c == 'r' && can_move(1)) {
      for (i = 0; i < 4; i++) px[i]++;
    } else if (c == 'd') {
      for (i = 0; i < 4; i++) {
        if (py[i] + 1 >= H || grid[py[i] + 1][px[i]]) {
          for (j = 0; j < 4; j++)
            grid[py[j]][px[j]] = 1;
          placed++;
          piece_n++;
          px[0] = 2; px[1] = 3; px[2] = 4; px[3] = 5;
          py[0] = 0; py[1] = 0; py[2] = 0; py[3] = 0;
          goto next;
        }
      }
      for (i = 0; i < 4; i++) py[i]++;
    }
  next:
    steps++;
  }
  draw();
  printf("placed=%d\n", placed);
  return 0;
}
