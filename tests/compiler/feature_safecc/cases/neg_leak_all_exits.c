/* issue #180: both return paths leave the allocation live. */
#include <stdlib.h>
int f(int c) {
  int *p = malloc(4);
  if (c) return 1;
  return 0;
}
int main(void) { return f(1); }
