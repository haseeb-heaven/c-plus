/* F2 static closure: a pointer laundered through uintptr_t after its owner
 * was released must not be passed to free in STRICT. No ABI change needed. */
#include <stdlib.h>
#include <stdint.h>
int main(void) {
  int *p = malloc(4);
  uintptr_t u = (uintptr_t)p;
  free(p);
  int *q = malloc(4);
  free((int *)u);
  free(q);
  return 0;
}
