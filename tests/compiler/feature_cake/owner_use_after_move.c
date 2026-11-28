/* Cake _Owner: reading the moved-from handle is a hard error. */
#include "cplus/cplus.h"
int main(void) {
  int value = 7;
  int *_Owner p = &value;
  int *a = cplus_move(p);
  *p = 9;
  (void)a;
  return value;
}
