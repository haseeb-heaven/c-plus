/* issue #180: `sizeof(p->x)` is unevaluated even though p is must-NULL on
 * every path.  [CPLUS-SAFECC] must not report a null dereference here. */
#include <stddef.h>
struct S { int x; };
int f(int c) {
  struct S *p;
  if (c) p = NULL;
  else p = (struct S *)NULL;
  return (int)sizeof(p->x);
}
int main(void) { return f(1); }
