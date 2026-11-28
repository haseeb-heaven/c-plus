/*issue #223: an allocation owned and freed entirely inside one arm is safe.*/
#include <stdlib.h>

void f(int cond) {
  if (cond) {
    int *p = malloc(sizeof(int));
    if (!p) return;
    *p = 42;
    free(p);
  }
}
