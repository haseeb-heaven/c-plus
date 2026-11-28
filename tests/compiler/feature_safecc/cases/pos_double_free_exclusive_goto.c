/* issue #180: two mutually exclusive free(p) arms joined by goto cleanup are
 * not a double free.  The linear may-release heuristic cannot see that the
 * first arm jumps past the second free; the clang::CFG forward pass can. */
#include <stdlib.h>
int f(int c) {
  int *p = malloc(8);
  if (c) { free(p); goto cleanup; }
  free(p);
cleanup:
  return 0;
}
int main(void) { return f(1); }
