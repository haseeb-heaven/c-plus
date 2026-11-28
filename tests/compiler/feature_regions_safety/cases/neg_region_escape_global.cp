#include "cplus/cplus.h"
#include <stdio.h>
static int *g;
int main(void) {
  region r(64) { int *p = cplus_region_alloc(r, sizeof(int)); g = p; }
  return 0;
}
