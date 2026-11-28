/* Cake _Owner: exactly one transfer is a clean program. */
#include "cplus/cplus.h"
int main(void) {
  int value = 7;
  int *_Owner p = &value;
  int *a = cplus_move(p);
  return *a == 7 ? 0 : 1;
}
