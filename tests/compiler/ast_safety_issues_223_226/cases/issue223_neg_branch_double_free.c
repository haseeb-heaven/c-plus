/* issue #223: two frees inside the same if arm are a guaranteed double free. */
#include <stdlib.h>

void f(int cond) {
  int *p = malloc(sizeof(int));
  if (!p) return;
  if (cond) {
    free(p);
    free(p);
  }
}
