#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_apps/full_apps/tic_tac_toe_full.c"
/* Fully playable tic-tac-toe vs deterministic AI (plain C89).
 * stdin: human move as digit 1..9, q quits. */
#include <stdio.h>

static int cell[9];

static void draw(void) {
  int i;
  for (i = 0; i < 9; i++) {
    if (i == 3 || i == 6)
      printf("\n");
    if (cell[i] == 1) putchar('X');
    else if (cell[i] == -1) putchar('O');
    else putchar('.');
    if (i % 3 != 2) putchar('|');
  }
  printf("\n");
}

static int won(int p) {
  static const int ln[8][3] = {{0,1,2},{3,4,5},{6,7,8},
                              {0,3,6},{1,4,7},{2,5,8},
                              {0,4,8},{2,4,6}};
  int i, j, n;
  for (i = 0; i < 8; i++) {
    n = 0;
    for (j = 0; j < 3; j++)
      if (cell[ln[i][j]] == p) n++;
    if (n == 3) return 1;
  }
  return 0;
}

int main(void) {
  int c, i, turns = 0;
  for (i = 0; i < 9; i++) cell[i] = 0;
  while (turns < 9) {
    draw();
    c = getchar();
    if (c == 'q' || c == EOF) break;
    if (c >= '1' && c <= '9') {
      int at = c - '1';
      if (cell[at] == 0) {
        cell[at] = 1;
        turns++;
        if (won(1)) { draw(); printf("human wins\n"); return 0; }
      }
    }
    for (i = 0; i < 9; i++) {
      if (cell[i] == 0) {
        cell[i] = -1;
        turns++;
        break;
      }
    }
    if (won(-1)) { draw(); printf("ai wins\n"); return 0; }
  }
  draw();
  printf("draw\n");
  return 0;
}
