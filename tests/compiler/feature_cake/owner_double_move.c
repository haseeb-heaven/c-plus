/* Cake _Owner: a moved-from owner cannot be transferred a second time. */
#include "cplus/cplus.h"
int main(void) {
  int value = 7;
  int *_Owner p = &value;
  int *a = cplus_move(p);
  int *b = cplus_move(p);
  (void)a;
  (void)b;
  return 0;
}
