/* issue #225: a null test on a freed pointer does not revive it. */
#include <stdlib.h>

int f(void) {
  int *q = malloc(sizeof(int));
  free(q);
  return q ? *q : 0;
}
