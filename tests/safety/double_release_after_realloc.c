#include "cplus/cplus.h"
int main(void) {
  int *p = cplus_alloc(8);
  int *q = cplus_realloc(p, 16);
  cplus_release(p);
  cplus_release(q);
  return 0;
}
