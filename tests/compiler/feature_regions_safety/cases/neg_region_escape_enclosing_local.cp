#include "cplus/cplus.h"
#include <stdio.h>
int main(void) {
  int *saved;
  region r(64) { int *p = cplus_region_alloc(r, sizeof(int)); saved = p; }
  (void)saved;
  return 0;
}
