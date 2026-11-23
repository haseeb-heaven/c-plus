#include <stdlib.h>
int main(int argc, char **argv) {
  (void)argv;
  int *p = malloc(4 * sizeof(int));
  if (!p) return 2;
  volatile int idx = argc + 4;
  p[idx] = 1;
  free(p);
  return 0;
}
