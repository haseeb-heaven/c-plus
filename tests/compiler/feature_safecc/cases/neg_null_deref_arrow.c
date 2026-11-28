/* issue #180: member dereference of a definite NULL pointer. */
#include <stddef.h>
struct S { int x; };
int main(void) {
  struct S *p = NULL;
  return p->x;
}
