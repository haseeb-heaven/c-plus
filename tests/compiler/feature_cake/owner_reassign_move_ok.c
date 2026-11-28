/* Cake _Owner: a reassignment between moves restores the owner. */
#include "cplus/cplus.h"
int main(void) {
  int a = 1, b = 2;
  int *_Owner p = &a;
  int *x = cplus_move(p);
  p = &b;
  int *y = cplus_move(p);
  return *x + *y == 3 ? 0 : 1;
}
