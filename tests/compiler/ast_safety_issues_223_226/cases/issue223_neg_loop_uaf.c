/* issue #223: release state must be live inside a loop body too. */
#include <stdlib.h>

void f(int n) {
  int *p = malloc(sizeof(int));
  if (!p) return;
  for (int i = 0; i < n; ++i) {
    free(p);
    *p = i;
  }
}
