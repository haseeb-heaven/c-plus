#include <stdlib.h>

int main(void) {
  int **arr = (int **)malloc(2 * sizeof(int *));
  arr[100] = 0;
  free(arr);
  return 0;
}
