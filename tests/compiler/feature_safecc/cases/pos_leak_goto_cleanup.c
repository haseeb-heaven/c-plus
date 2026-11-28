/* issue #180: `goto cleanup` must keep working without a false leak report. */
#include <stdlib.h>
int f(void) {
  int *p = malloc(8);
  if (!p) return 1;
  goto cleanup;
cleanup:
  free(p);
  return 0;
}
int main(void) { return f(); }
