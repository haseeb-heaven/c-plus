/* issue #180: `if (!p) return` makes p non-null on the only fall-through path. */
#include <stddef.h>
int f(void) {
  int *p = NULL;
  if (!p) return 0;
  return *p;
}
int main(void) { return f(); }
