#include "cplus/cplus.h"
int main(void) {
  int *a = cplus_alloc(sizeof(int));
  cplus_release(a);
  int *b = cplus_alloc(sizeof(int));
  cplus_check_live(a); /* quarantine makes this a detected dangling pointer */
  cplus_release(b);
  return 0;
}
