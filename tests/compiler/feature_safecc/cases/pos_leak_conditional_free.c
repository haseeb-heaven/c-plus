/* issue #180: freeing on only one arm is not a definite leak on all exits. */
#include <stdlib.h>
int f(int c) {
  int *p = malloc(8);
  if (c) free(p);
  return 0;
}
int main(void) { return f(1); }
