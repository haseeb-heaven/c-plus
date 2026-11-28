/* Fully playable deterministic Snake (plain C89, no C+ syntax).
 * Commands on stdin: w/a/s/d move, q quit. Grid and food sequence are fixed. */
#include <stdio.h>

#define W 10
#define H 10
#define MAXLEN 100

static int grid[H][W];
static int sx[MAXLEN], sy[MAXLEN];
static int len, score;
static int foodx[8] = {5, 7, 2, 8, 4, 1, 6, 3};
static int foody[8] = {5, 2, 7, 4, 1, 8, 3, 6};
static int foodn;

static void draw(void) {
  int i, j;
  for (i = 0; i < H; i++) {
    for (j = 0; j < W; j++) {
      if (i == 0 || j == 0 || i == H - 1 || j == W - 1)
        putchar('#');
      else if (grid[i][j] == 2)
        putchar('*');
      else if (grid[i][j] == 1)
        putchar('o');
      else
        putchar(' ');
    }
    putchar('\n');
  }
}

int main(void) {
  int x = 5, y = 5, i, steps = 0;
  int c;
  for (i = 0; i < H; i++) {
    int j;
    for (j = 0; j < W; j++)
      grid[i][j] = 0;
  }
  sx[0] = x; sy[0] = y; len = 1;
  grid[y][x] = 1;
  grid[foody[0]][foodx[0]] = 2;
  while (steps < 12) {
    draw();
    printf("score=%d len=%d\n", score, len);
    c = getchar();
    if (c == 'q' || c == EOF)
      break;
    if (c == 'w') y--;
    else if (c == 's') y++;
    else if (c == 'a') x--;
    else if (c == 'd') x++;
    if (x < 1 || x >= W - 1 || y < 1 || y >= H - 1) {
      printf("game over\n");
      break;
    }
    if (x == foodx[foodn % 8] && y == foody[foodn % 8]) {
      score++;
      if (len < MAXLEN) {
        sx[len] = x; sy[len] = y;
        len++;
      }
      foodn++;
      grid[foody[foodn % 8]][foodx[foodn % 8]] = 2;
    } else {
      for (i = len - 1; i > 0; i--) {
        sx[i] = sx[i - 1];
        sy[i] = sy[i - 1];
      }
      sx[0] = x; sy[0] = y;
    }
    for (i = 0; i < H; i++) {
      int j;
      for (j = 0; j < W; j++)
        grid[i][j] = 0;
    }
    for (i = 0; i < len; i++)
      grid[sy[i]][sx[i]] = 1;
    grid[foody[foodn % 8]][foodx[foodn % 8]] = 2;
    steps++;
  }
  printf("final=%d\n", score);
  return 0;
}
