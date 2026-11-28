#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_apps/apps/tic_tac_toe.c"
/* Original deterministic tic-tac-toe endgame evaluator (plain C89).
 * Fixed global board keeps every subscript provably in range. */
#include <stdio.h>

static int board[9];

static int win(int p) {
  static const int lines[8][3] = {
      {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
      {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
      {0, 4, 8}, {2, 4, 6}};
  int i, j, n;
  for (i = 0; i < 8; i++) {
    n = 0;
    for (j = 0; j < 3; j++)
      if (board[lines[i][j]] == p)
        n++;
    if (n == 3)
      return 1;
  }
  return 0;
}

int main(void) {
  static const int sample[9] = {1, 1, 1, 0, -1, 0, -1, 0, 0};
  int i;
  for (i = 0; i < 9; i++)
    board[i] = sample[i];
  printf("x=%d o=%d\n", win(1), win(-1));
  return 0;
}
