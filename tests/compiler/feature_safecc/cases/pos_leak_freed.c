/* issue #180: null guard plus free makes the allocation dead on both exits. */
#include <stdlib.h>
int f(void) {
  int *p = malloc(8);
  if (!p) return 1;
  free(p);
  return 0;
}
int main(void) { return f(); }
