/* issue #180: a pointer that is NULL on only one arm is may-null, not must-null. */
#include <stddef.h>
int f(int c, int *nonnull) {
  int *p = nonnull;
  if (c) p = NULL;
  return *p;
}
int main(void) { int v = 3; return f(0, &v); }
