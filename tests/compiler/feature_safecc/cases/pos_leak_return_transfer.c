/* issue #180: returning the pointer transfers ownership to the caller. */
#include <stdlib.h>
int *f(void) {
  int *p = malloc(8);
  return p;
}
int main(void) {
  int *q = f();
  free(q);
  return 0;
}
