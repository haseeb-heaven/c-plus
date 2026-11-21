#include "cplus/cplus.h"
int main(void) {
  int *p = cplus_alloc(8);
  int ok = cplus_alloc_bytes(p) == 4;
  cplus_release(p);
  return !ok ? 0 : 1;
}
