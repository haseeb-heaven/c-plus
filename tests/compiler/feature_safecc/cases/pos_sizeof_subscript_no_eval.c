/* issue #180: `sizeof(p[0])` is unevaluated even though p is must-NULL on
 * every path.  [CPLUS-SAFECC] must not report a null dereference here. */
#include <stddef.h>
int f(int c) {
  int *p;
  if (c) p = NULL;
  else p = (int *)NULL;
  return (int)sizeof(p[0]);
}
int main(void) { return f(1); }
