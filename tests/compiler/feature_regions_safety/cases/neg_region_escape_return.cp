#include "cplus/cplus.h"
#include <stdio.h>
static int *f(void) {
  region r(64) { int *p = cplus_region_alloc(r, sizeof(int)); return p; }
  return 0;
}
int main(void) { return 0; }
