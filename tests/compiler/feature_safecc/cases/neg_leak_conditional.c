/* issue #180: the allocation happens only on the c arm and there is no
 * release on that path.  It is not a definite leak (one exit never owns p),
 * so the token-level pass stays silent; the clang::CFG forward must/may
 * lattice reports the conditional leak. */
#include <stdlib.h>
void f(int c) {
  int *p = 0;
  if (c) { p = malloc(4); }
  (void)p;
}
int main(void) { f(1); return 0; }
