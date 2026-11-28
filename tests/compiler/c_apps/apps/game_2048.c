/* Original deterministic 2048 row-merge core (plain C89). */
#include <stdio.h>

static void merge(int row[4], int out[4]) {
  int tmp[4];
  int i, j = 0;
  for (i = 0; i < 4; i++)
    tmp[i] = 0;
  for (i = 0; i < 4; i++) {
    int prev;
    if (row[i] == 0)
      continue;
    prev = j - 1;
    if (prev >= 0 && tmp[prev] == row[i]) {
      tmp[prev] = row[i] + row[i];
    } else {
      if (j < 4)
        tmp[j] = row[i];
      j++;
    }
  }
  for (i = 0; i < 4; i++)
    out[i] = tmp[i];
}

int main(void) {
  int row[4];
  int out[4];
  int i;
  row[0] = 2; row[1] = 2; row[2] = 4; row[3] = 4;
  merge(row, out);
  for (i = 0; i < 4; i++)
    printf("%d ", out[i]);
  printf("\n");
  return 0;
}
