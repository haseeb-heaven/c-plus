/* issue #223: free() inside an if arm must still mark the owner released,
 * so the dereference in that same arm is a guaranteed use-after-free. */
#include <stdlib.h>

void f(int cond) {
  if (cond) {
    int *p = malloc(sizeof(int));
    if (!p) return;
    free(p);
    *p = 42;
  }
}
