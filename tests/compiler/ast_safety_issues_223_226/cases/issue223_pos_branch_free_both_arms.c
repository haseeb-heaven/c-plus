/* issue #223: both arms release the owner, so there is no leak/double free. */
#include <stdlib.h>

void f(int cond) {
  int *p = malloc(sizeof(int));
  if (!p) return;
  if (cond)
    free(p);
  else
    free(p);
}
