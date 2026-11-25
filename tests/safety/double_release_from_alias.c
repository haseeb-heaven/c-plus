#include "cplus/cplus.h"
int main(void) {
  int *p = cplus_new(int);
  int *alias = p;
  cplus_release(p);
  cplus_release(alias);
  return 0;
}
