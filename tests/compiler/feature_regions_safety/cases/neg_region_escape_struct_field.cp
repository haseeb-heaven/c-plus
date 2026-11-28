#include "cplus/cplus.h"
#include <stdio.h>
struct Box { int *p; };
int main(void) {
  struct Box b;
  region r(64) { int *p = cplus_region_alloc(r, sizeof(int)); b.p = p; }
  (void)b;
  return 0;
}
