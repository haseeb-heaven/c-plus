#include <stdlib.h>
int main(void) {
  int *p = malloc(4 * sizeof(int));
  if (!p) return 2;
  volatile int idx = 4;
  p[idx] = 1;
  free(p);
  return 0;
}
