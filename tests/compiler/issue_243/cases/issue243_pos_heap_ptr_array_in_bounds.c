#include <stdlib.h>

int main(void) {
  int **arr = (int **)malloc(2 * sizeof(int *));
  int *v = 0;
  arr[1] = v;
  v = arr[0];
  free(arr);
  return v != 0;
}
