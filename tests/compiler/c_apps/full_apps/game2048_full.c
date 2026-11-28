/* Fully playable deterministic 2048 (plain C89). stdin: w/a/s/d/q. */
#include <stdio.h>

#define N 4
static int board[N][N];
static int spawn_x[8] = {0, 1, 2, 3, 0, 1, 2, 3};
static int spawn_y[8] = {0, 0, 0, 0, 1, 1, 1, 1};
static int spawnn;

static void draw(void) {
  int i, j;
  for (i = 0; i < N; i++) {
    for (j = 0; j < N; j++)
      printf("%4d", board[i][j]);
    printf("\n");
  }
}

static void spawn(void) {
  int x = spawn_x[spawnn % 8];
  int y = spawn_y[spawnn % 8];
  board[y][x] = 2;
  spawnn++;
}

static void squash(int row[N], int out[N]) {
  int tmp[N], i, j = 0;
  for (i = 0; i < N; i++)
    tmp[i] = 0;
  for (i = 0; i < N; i++) {
    int prev;
    if (row[i] == 0)
      continue;
    prev = j - 1;
    if (prev >= 0 && tmp[prev] == row[i])
      tmp[prev] = row[i] + row[i];
    else if (j < N) {
      tmp[j] = row[i];
      j++;
    }
  }
  for (i = 0; i < N; i++)
    out[i] = tmp[i];
}

static void move(int d) {
  int i, j;
  int row[N], out[N];
  if (d == 'a' || d == 'd') {
    for (i = 0; i < N; i++) {
      for (j = 0; j < N; j++)
        row[j] = board[i][j];
      squash(row, out);
      for (j = 0; j < N; j++)
        board[i][j] = out[j];
    }
  } else {
    for (j = 0; j < N; j++) {
      for (i = 0; i < N; i++)
        row[i] = board[i][j];
      squash(row, out);
      for (i = 0; i < N; i++)
        board[i][j] = out[i];
    }
  }
}

int main(void) {
  int c, moves = 0;
  int i, j;
  for (i = 0; i < N; i++)
    for (j = 0; j < N; j++)
      board[i][j] = 0;
  spawn(); spawn();
  while (moves < 8) {
    draw();
    c = getchar();
    if (c == 'q' || c == EOF)
      break;
    if (c == 'w' || c == 'a' || c == 's' || c == 'd') {
      move(c);
      spawn();
      moves++;
    }
  }
  draw();
  printf("moves=%d\n", moves);
  return 0;
}
