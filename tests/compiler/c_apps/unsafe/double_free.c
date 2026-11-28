/* Unsafe: managed double release. STRICT must reject. */
#include "cplus/cplus.h"
int main(void) {
  int *p = cplus_alloc(sizeof(int));
  cplus_release(p);
  cplus_release(p);
  return 0;
}
