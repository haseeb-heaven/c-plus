/* issue #180: `p && p->field` guards the member dereference on its true arm. */
#include <stddef.h>
struct S { int x; };
int f(struct S *p) {
  if (p && p->x > 0) return p->x;
  return 0;
}
int main(void) { struct S s = {1}; return f(&s); }
