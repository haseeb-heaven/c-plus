/* issue #180: a must-NULL struct pointer proven by the if/else join, then
 * dereferenced with ->.  This exercises [CPLUS-SAFECC] itself: the older
 * lifetime diagnostic does not see facts established by a CFG join. */
#include <stddef.h>
struct S { int x; };
int f(int c) {
  struct S *p;
  if (c) p = NULL;
  else p = (struct S *)NULL;
  return p->x;
}
int main(void) { return f(1); }
