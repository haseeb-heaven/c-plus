#include <stdlib.h>
int main(void) {
  int *p = malloc(4 * sizeof(int));
  if (!p) return 1;
  p[4] = 1; /* provable heap out-of-bounds: C+ must reject at compile time */
  free(p);
  return 0;
}
