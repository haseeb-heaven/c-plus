#include "cplus/cplus.h"
int main(void) {
  int *a = cplus_alloc(sizeof(int));
  cplus_release(a);
  int *b = cplus_alloc(sizeof(int));
  volatile void *opaque = a;
  cplus_check_live((void *)opaque); /* runtime quarantine probe */
  cplus_release(b);
  return 0;
}
