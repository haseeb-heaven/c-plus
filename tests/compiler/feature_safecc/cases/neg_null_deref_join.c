/* issue #180: both if/else arms assign NULL, so the merge is must-NULL. */
#include <stddef.h>
int f(int c) {
  int *p;
  if (c) p = NULL;
  else p = (int *)NULL;
  return *p;
}
int main(void) { return f(1); }
