/* issue #241: reassigning the owner must not revive a dangling alias. */
#include <stdlib.h>

int main(void) {
  int *p = (int *)malloc(sizeof(int));
  int *q = p;
  free(p);
  p = NULL;
  *q = 42;
  free(q);
  return 0;
}
