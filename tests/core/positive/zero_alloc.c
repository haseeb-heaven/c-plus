#include "cplus/cplus.h"
int main(void) {
  void *a = cplus_alloc(0);
  void *b = cplus_zalloc(0);
  int ok = a != NULL && b != NULL;
  cplus_release(a);
  cplus_release(b);
  return ok ? 0 : 1;
}
