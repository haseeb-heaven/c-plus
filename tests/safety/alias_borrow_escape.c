#include "cplus/cplus.h"
int *bad(void) {
  resource struct R { int x; };
  R r = {1};
  return &r.x;
}
int main(void) { (void)bad(); return 0; }
